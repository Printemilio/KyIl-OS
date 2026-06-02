#include <windows.h>

void mainCRTStartup(void)
{
    DWORD written = 0;
    char message[] = "pe file io works\r\n";
    HANDLE file = CreateFileA(
        "/home/root/pe_file_test.txt",
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, message, sizeof(message) - 1, &written, NULL);
        CloseHandle(file);
    }

    ExitProcess(0);
}
