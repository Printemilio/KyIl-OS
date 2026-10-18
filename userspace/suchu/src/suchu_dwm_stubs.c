/* DwmSetWindowAttribute, for the title bar.

   Windows draws a window's frame, not the program, and the one call that changes
   how it looks lives in dwmapi.dll. It is asked for by name at the moment it is
   used rather than linked, so a machine without it -- anything before Windows
   Vista, or a stripped-down one -- opens a window with the ordinary frame instead
   of a program that will not start.

   This used to go through ctypes-foreign, which for one call dragged libffi into
   every program with a window, and a DLL that has to travel with the program is
   exactly what a compiled program is supposed not to need. Six lines of C do the
   same without it.

   Off Windows there is no such frame to change, and this does nothing. */

#include <caml/mlvalues.h>
#include <caml/memory.h>

#ifdef _WIN32
#include <windows.h>

typedef HRESULT (WINAPI *set_attribute_fn)(HWND, DWORD, LPCVOID, DWORD);
#endif

/* Returns 0 when the attribute was set, anything else when it was not -- which is
   an ordinary answer, not an error: an attribute newer than the Windows it is
   asked of is simply refused. The value is a four-byte BOOL or COLORREF. */
CAMLprim value suchu_dwm_set_attribute(value handle, value attribute, value setting)
{
  CAMLparam3(handle, attribute, setting);
  int result = -1;
#ifdef _WIN32
  HMODULE dwm = LoadLibraryA("dwmapi.dll");
  if (dwm != NULL) {
    set_attribute_fn set =
      (set_attribute_fn) (void *) GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (set != NULL) {
      int cell = (int) Long_val(setting);
      result = (int) set((HWND) Nativeint_val(handle), (DWORD) Long_val(attribute), &cell,
                         (DWORD) sizeof cell);
    }
    FreeLibrary(dwm);
  }
#endif
  CAMLreturn(Val_int(result));
}
