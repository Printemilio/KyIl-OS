/* A file name in the form raylib can open.

   OCaml hands a file name over as UTF-8, and OCaml's own file functions pass it on
   to Windows as it should be. raylib does not: it opens files with fopen, which
   reads a name in the machine's ANSI code page, so "Café été.mp3" arrives as
   mojibake and the file is "not found". A music collection is full of names like
   that.

   This turns a UTF-8 name into one fopen reads correctly. When every character of
   it exists in the ANSI code page -- every accented letter of western Europe does
   -- that is the name itself, written in that code page. When one does not (a
   Cyrillic title on a French machine), it falls back to the 8.3 short name, which
   is plain ASCII, and which exists unless the volume has switched them off.

   None when neither works, and off Windows, where fopen takes UTF-8 and there is
   nothing to convert. */

#include <caml/mlvalues.h>
#include <caml/memory.h>
#include <caml/alloc.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>

/* The wide string as ANSI bytes, or NULL if some character does not fit. */
static char *to_ansi(const wchar_t *wide)
{
  BOOL used_default = FALSE;
  int size = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide, -1, NULL, 0, NULL, &used_default);
  if (size <= 0 || used_default) return NULL;
  char *text = (char *) malloc((size_t) size);
  if (text == NULL) return NULL;
  used_default = FALSE;
  if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide, -1, text, size, NULL, &used_default) <= 0
      || used_default) {
    free(text);
    return NULL;
  }
  return text;
}
#endif

CAMLprim value suchu_ansi_path(value utf8)
{
  CAMLparam1(utf8);
  CAMLlocal2(result, text);
  result = Val_int(0); /* None */
#ifdef _WIN32
  int wide_size = MultiByteToWideChar(CP_UTF8, 0, String_val(utf8), -1, NULL, 0);
  if (wide_size > 0) {
    wchar_t *wide = (wchar_t *) malloc((size_t) wide_size * sizeof(wchar_t));
    if (wide != NULL && MultiByteToWideChar(CP_UTF8, 0, String_val(utf8), -1, wide, wide_size) > 0) {
      char *ansi = to_ansi(wide);
      if (ansi == NULL) {
        wchar_t *short_name = (wchar_t *) malloc(32768 * sizeof(wchar_t));
        if (short_name != NULL) {
          DWORD length = GetShortPathNameW(wide, short_name, 32768);
          if (length > 0 && length < 32768) ansi = to_ansi(short_name);
          free(short_name);
        }
      }
      if (ansi != NULL) {
        text = caml_copy_string(ansi);
        result = caml_alloc_some(text);
        free(ansi);
      }
    }
    free(wide);
  }
#endif
  CAMLreturn(result);
}
