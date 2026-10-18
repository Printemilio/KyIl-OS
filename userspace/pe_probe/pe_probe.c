typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef int int32_t;
typedef void *handle_t;

#define NULL ((void *)0)
#define STD_OUTPUT_HANDLE ((uint32_t)-11)
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x00000001u
#define CREATE_ALWAYS 2u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define WAIT_OBJECT_0 0u
#define INFINITE 0xffffffffu

__declspec(dllimport) handle_t GetStdHandle(uint32_t handle);
__declspec(dllimport) int32_t WriteFile(handle_t file, const void *buf, uint32_t bytes, uint32_t *written, void *overlapped);
__declspec(dllimport) void ExitProcess(uint32_t code);
__declspec(dllimport) uint32_t GetLastError(void);
__declspec(dllimport) uint32_t GetModuleFileNameA(handle_t module, char *buf, uint32_t size);
__declspec(dllimport) uint32_t GetCurrentDirectoryA(uint32_t size, char *buf);
__declspec(dllimport) uint32_t GetEnvironmentVariableA(const char *name, char *buf, uint32_t size);
__declspec(dllimport) handle_t CreateFileA(const char *name, uint32_t access, uint32_t share, void *security, uint32_t creation, uint32_t attrs, handle_t template_file);
__declspec(dllimport) int32_t CloseHandle(handle_t handle);
__declspec(dllimport) handle_t CreateEventA(void *security, int32_t manual_reset, int32_t initial_state, const char *name);
__declspec(dllimport) int32_t SetEvent(handle_t event);
__declspec(dllimport) uint32_t WaitForSingleObject(handle_t handle, uint32_t millis);
__declspec(dllimport) handle_t CreateThread(void *attrs, uint64_t stack_size, uint32_t (*start)(void *), void *param, uint32_t flags, uint32_t *thread_id);
__declspec(dllimport) void Sleep(uint32_t millis);
__declspec(dllimport) uint32_t GetTickCount(void);
__declspec(dllimport) void GetSystemTimeAsFileTime(uint64_t *filetime);
__declspec(dllimport) int32_t QueryPerformanceCounter(uint64_t *value);
__declspec(dllimport) int32_t QueryPerformanceFrequency(uint64_t *value);
__declspec(dllimport) void OutputDebugStringA(const char *text);

static uint32_t cstr_len(const char *s) {
    uint32_t n = 0;
    while (s[n] != 0) {
        ++n;
    }
    return n;
}

static void write_raw(handle_t out, const char *s) {
    uint32_t written = 0;
    WriteFile(out, s, cstr_len(s), &written, NULL);
}

static void write_u64(handle_t out, uint64_t value) {
    char buf[32];
    uint32_t pos = sizeof(buf);
    buf[--pos] = 0;
    if (value == 0) {
        buf[--pos] = '0';
    } else {
        while (value != 0 && pos > 0) {
            buf[--pos] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    write_raw(out, &buf[pos]);
}

static void write_line(handle_t out, const char *key, const char *value) {
    write_raw(out, key);
    write_raw(out, value);
    write_raw(out, "\r\n");
}

static uint32_t worker_thread(void *param) {
    uint32_t *slot = (uint32_t *)param;
    *slot = 0xC0DEF00Du;
    Sleep(1);
    return 7;
}

void mainCRTStartup(void) {
    handle_t out = GetStdHandle(STD_OUTPUT_HANDLE);
    char module[260];
    char cwd[260];
    char path[260];
    uint64_t stamp = 0;
    uint64_t qpc = 0;
    uint64_t qpf = 0;
    uint32_t thread_id = 0;
    uint32_t thread_slot = 0;
    uint32_t exit_code = 0;

    write_raw(out, "Vex PE probe: start\r\n");

    module[0] = 0;
    cwd[0] = 0;
    path[0] = 0;
    GetModuleFileNameA(NULL, module, sizeof(module));
    GetCurrentDirectoryA(sizeof(cwd), cwd);
    GetEnvironmentVariableA("PATH", path, sizeof(path));
    write_line(out, "module=", module);
    write_line(out, "cwd=", cwd);
    write_line(out, "path=", path);

    GetSystemTimeAsFileTime(&stamp);
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&qpf);
    write_raw(out, "tick=");
    write_u64(out, GetTickCount());
    write_raw(out, " filetime=");
    write_u64(out, stamp);
    write_raw(out, " qpc=");
    write_u64(out, qpc);
    write_raw(out, " qpf=");
    write_u64(out, qpf);
    write_raw(out, "\r\n");

    handle_t event = CreateEventA(NULL, 1, 0, NULL);
    if (event == NULL || !SetEvent(event) || WaitForSingleObject(event, 1000) != WAIT_OBJECT_0) {
        write_raw(out, "event=fail\r\n");
        exit_code = 2;
    } else {
        write_raw(out, "event=ok\r\n");
        CloseHandle(event);
    }

    handle_t thread = CreateThread(NULL, 0, worker_thread, &thread_slot, 0, &thread_id);
    if (thread == NULL || WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0 || thread_slot != 0xC0DEF00Du) {
        write_raw(out, "thread=fail last_error=");
        write_u64(out, GetLastError());
        write_raw(out, "\r\n");
        exit_code = 3;
    } else {
        write_raw(out, "thread=ok id=");
        write_u64(out, thread_id);
        write_raw(out, "\r\n");
        CloseHandle(thread);
    }

    handle_t file = CreateFileA("pe_probe_out.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == NULL) {
        write_raw(out, "file=fail last_error=");
        write_u64(out, GetLastError());
        write_raw(out, "\r\n");
        exit_code = 4;
    } else {
        write_raw(file, "Vex PE probe wrote this file through CreateFileA/WriteFile.\r\n");
        CloseHandle(file);
        write_raw(out, "file=ok pe_probe_out.txt\r\n");
    }

    write_raw(out, "Vex PE probe: done\r\n");
    OutputDebugStringA("Vex PE probe: done");
    ExitProcess(exit_code);
}
