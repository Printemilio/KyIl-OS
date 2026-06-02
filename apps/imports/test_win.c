#include <windows.h>

void mainCRTStartup(void)
{
    DWORD written = 0;
    char message[] = "hello from windows pe\r\n";
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, sizeof(message) - 1, &written, NULL);
    ExitProcess(0);
}
