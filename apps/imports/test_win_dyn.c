#include <windows.h>

typedef HANDLE (WINAPI *PFN_GetStdHandle)(DWORD);
typedef BOOL (WINAPI *PFN_WriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);

void mainCRTStartup(void)
{
    DWORD written = 0;
    char message[] = "pe getproc works\r\n";
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    PFN_GetStdHandle pGetStdHandle = (PFN_GetStdHandle)GetProcAddress(kernel, "GetStdHandle");
    PFN_WriteFile pWriteFile = (PFN_WriteFile)GetProcAddress(kernel, "WriteFile");

    if (pGetStdHandle != NULL && pWriteFile != NULL) {
        pWriteFile(pGetStdHandle(STD_OUTPUT_HANDLE), message, sizeof(message) - 1, &written, NULL);
    }

    ExitProcess(0);
}
