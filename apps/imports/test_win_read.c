#include <windows.h>

void mainCRTStartup(void)
{
    char buffer[64];
    DWORD read_count = 0;
    DWORD written = 0;
    HANDLE file = CreateFileA(
        "/home/root/pe_file_test.txt",
        GENERIC_READ,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file == INVALID_HANDLE_VALUE) {
        char failed[] = "pe read open failed\r\n";
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), failed, sizeof(failed) - 1, &written, NULL);
        ExitProcess(1);
    }

    if (ReadFile(file, buffer, sizeof(buffer) - 1, &read_count, NULL)) {
        buffer[read_count] = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buffer, read_count, &written, NULL);
    }

    CloseHandle(file);
    ExitProcess(0);
}
