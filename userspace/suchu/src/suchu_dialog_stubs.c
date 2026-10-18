/* The open, save and choose-a-folder dialogs of Windows.

   choose_file used to ask zenity or kdialog for a picker, which is how it works on
   Linux and why on Windows it said "No file dialog available": neither exists
   there, and a program that opens files had no way to ask which. These are the
   dialogs Windows itself provides, the ones every other program uses.

   Each blocks until the person has chosen or cancelled, and gives back the path as
   UTF-8, or None for cancel -- so a program can tell "cancelled" from "chose". The
   window that asked is the owner, which keeps the dialog in front of it.

   Off Windows these do nothing and answer None; the callers use zenity there. */

#include <caml/mlvalues.h>
#include <caml/memory.h>
#include <caml/alloc.h>
#include <string.h>

#ifdef _WIN32
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <stdlib.h>

static wchar_t *wide_of_utf8(const char *text)
{
  int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
  if (size <= 0) return NULL;
  wchar_t *wide = (wchar_t *) malloc((size_t) size * sizeof(wchar_t));
  if (wide != NULL && MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, size) <= 0) {
    free(wide);
    return NULL;
  }
  return wide;
}

static char *utf8_of_wide(const wchar_t *wide)
{
  int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
  if (size <= 0) return NULL;
  char *text = (char *) malloc((size_t) size);
  if (text != NULL && WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, size, NULL, NULL) <= 0) {
    free(text);
    return NULL;
  }
  return text;
}
#endif

static value some_string(const char *text)
{
  CAMLparam0();
  CAMLlocal2(copy, result);
  copy = caml_copy_string(text);
  result = caml_alloc_some(copy);
  CAMLreturn(result);
}

/* suchu_file_dialog owner title save */
CAMLprim value suchu_file_dialog(value owner, value title, value save)
{
  CAMLparam3(owner, title, save);
  CAMLlocal1(result);
  result = Val_int(0); /* None */
#ifdef _WIN32
  wchar_t *wide_title = wide_of_utf8(String_val(title));
  wchar_t *buffer = (wchar_t *) calloc(32768, sizeof(wchar_t));
  if (buffer != NULL) {
    OPENFILENAMEW dialog;
    memset(&dialog, 0, sizeof dialog);
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = (HWND) Nativeint_val(owner);
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = 32768;
    dialog.lpstrTitle = wide_title;
    dialog.lpstrFilter = L"All files\0*.*\0";
    /* NOCHANGEDIR: the dialog must not move the program's working directory, or
       every relative path it uses afterwards would mean something else. */
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST
                   | (Bool_val(save) ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    BOOL chosen = Bool_val(save) ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (chosen) {
      char *path = utf8_of_wide(buffer);
      if (path != NULL) {
        result = some_string(path);
        free(path);
      }
    }
    free(buffer);
  }
  free(wide_title);
#endif
  CAMLreturn(result);
}

/* suchu_folder_dialog owner title */
CAMLprim value suchu_folder_dialog(value owner, value title)
{
  CAMLparam2(owner, title);
  CAMLlocal1(result);
  result = Val_int(0); /* None */
#ifdef _WIN32
  wchar_t *wide_title = wide_of_utf8(String_val(title));
  /* The program may already have initialised COM in another mode, which is fine:
     the dialog works either way, and only a successful call is undone. */
  HRESULT started = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  IFileOpenDialog *dialog = NULL;
  HRESULT status = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                    &IID_IFileOpenDialog, (void **) &dialog);
  if (SUCCEEDED(status)) {
    DWORD options = 0;
    IFileOpenDialog_GetOptions(dialog, &options);
    IFileOpenDialog_SetOptions(dialog, options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (wide_title != NULL) IFileOpenDialog_SetTitle(dialog, wide_title);
    status = IFileOpenDialog_Show(dialog, (HWND) Nativeint_val(owner));
    if (SUCCEEDED(status)) {
      IShellItem *item = NULL;
      if (SUCCEEDED(IFileOpenDialog_GetResult(dialog, &item))) {
        PWSTR wide_path = NULL;
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &wide_path))) {
          char *path = utf8_of_wide(wide_path);
          if (path != NULL) {
            result = some_string(path);
            free(path);
          }
          CoTaskMemFree(wide_path);
        }
        IShellItem_Release(item);
      }
    }
    IFileOpenDialog_Release(dialog);
  }
  if (SUCCEEDED(started)) CoUninitialize();
  free(wide_title);
#endif
  CAMLreturn(result);
}
