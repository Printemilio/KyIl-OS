/* Historical prototype, excluded from the kernel build. DO NOT reactivate:
 * Win32/NT contracts now belong to userspace/windows. Retained temporarily
 * only as an inventory of old attempted APIs, not an implementation claim. */
#include <unos/compat.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/debug_log.h>
#include <unos/pe.h>
#include <unos/scheduler.h>
#include <unos/vex_abi.h>
#include <unos/vex_legacy_compat.h>
#include <unos/vex_syscall.h>

#define PE_LINUX_ENOSYS 38ull

#define WIN_ERROR_SUCCESS 0u
#define WIN_ERROR_FILE_NOT_FOUND 2u
#define WIN_ERROR_PATH_NOT_FOUND 3u
#define WIN_ERROR_INVALID_HANDLE 6u
#define WIN_ERROR_NOT_ENOUGH_MEMORY 8u
#define WIN_ERROR_NO_MORE_FILES 18u
#define WIN_ERROR_INVALID_PARAMETER 87u
#define WIN_ERROR_INSUFFICIENT_BUFFER 122u
#define WIN_ERROR_CALL_NOT_IMPLEMENTED 120u
#define WIN_ERROR_ALREADY_EXISTS 183u
#define WIN_DRIVE_NO_ROOT_DIR 1u
#define WIN_DRIVE_FIXED 3u
#define WIN_WAIT_OBJECT_0 0u
#define WIN_WAIT_TIMEOUT 258u
#define WIN_TLS_OUT_OF_INDEXES 0xffffffffu
#define WIN_INVALID_FILE_ATTRIBUTES 0xffffffffu
#define WIN_FILE_ATTRIBUTE_READONLY 0x00000001u
#define WIN_FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define WIN_FILE_ATTRIBUTE_ARCHIVE 0x00000020u
#define WIN_MAX_PATH 260u

#define WIN_CREATE_NEW 1u
#define WIN_CREATE_ALWAYS 2u
#define WIN_OPEN_EXISTING 3u
#define WIN_OPEN_ALWAYS 4u

#define WIN_GENERIC_READ  0x80000000u
#define WIN_GENERIC_WRITE 0x40000000u
#define WIN_INVALID_HANDLE_VALUE ((uint64_t)-1)
#define WIN_MODULE_KERNEL32 0x1000u
#define WIN_MEM_COMMIT 0x1000u
#define WIN_MEM_RESERVE 0x2000u
#define WIN_MEM_RELEASE 0x8000u
#define WIN_PAGE_READWRITE 0x04u
#define WIN_PAGE_EXECUTE_READWRITE 0x40u
#define WIN_CREATE_SUSPENDED 0x00000004u
#define WIN_INFINITE 0xffffffffu

#define WIN_STD_INPUT_HANDLE  0xfffffff6u
#define WIN_STD_OUTPUT_HANDLE 0xfffffff5u
#define WIN_STD_ERROR_HANDLE  0xfffffff4u

#define WIN_MODULE_KERNELBASE 0x1001u
#define WIN_MODULE_NTDLL      0x1002u

#define WIN_HANDLE_PROCESS_TAG 0x4000000000000000ull
#define WIN_HANDLE_THREAD_TAG  0x5000000000000000ull
#define WIN_HANDLE_VOLUME_FIND_TAG 0x6500000000000000ull
#define WIN_HANDLE_SYNC_TAG    0x7000000000000000ull
#define WIN_DEFAULT_THREAD_STACK (512ull * 1024ull)
#define WIN_PROCESS_HEAP_HANDLE 0x6000000000000001ull
#define WIN_FILE_TYPE_UNKNOWN 0u
#define WIN_FILE_TYPE_DISK 1u
#define WIN_FILE_TYPE_CHAR 2u
#define WIN_DEFAULT_CONSOLE_MODE 0x0007u
#define WIN_FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100u
#define PE_TICK_MS 10ull

typedef struct {
    uint32_t cb;
    char *reserved;
    char *desktop;
    char *title;
    uint32_t x;
    uint32_t y;
    uint32_t x_size;
    uint32_t y_size;
    uint32_t x_count_chars;
    uint32_t y_count_chars;
    uint32_t fill_attribute;
    uint32_t flags;
    uint16_t show_window;
    uint16_t cb_reserved2;
    uint8_t *reserved2;
    uint64_t std_input;
    uint64_t std_output;
    uint64_t std_error;
} pe_startup_info_a_t;

typedef struct {
    uint32_t cb;
    uint16_t *reserved;
    uint16_t *desktop;
    uint16_t *title;
    uint32_t x;
    uint32_t y;
    uint32_t x_size;
    uint32_t y_size;
    uint32_t x_count_chars;
    uint32_t y_count_chars;
    uint32_t fill_attribute;
    uint32_t flags;
    uint16_t show_window;
    uint16_t cb_reserved2;
    uint8_t *reserved2;
    uint64_t std_input;
    uint64_t std_output;
    uint64_t std_error;
} pe_startup_info_w_t;

typedef struct {
    uint64_t hProcess;
    uint64_t hThread;
    uint32_t dwProcessId;
    uint32_t dwThreadId;
} pe_process_information_t;

typedef struct {
    uint16_t wProcessorArchitecture;
    uint16_t wReserved;
    uint32_t dwPageSize;
    uint64_t lpMinimumApplicationAddress;
    uint64_t lpMaximumApplicationAddress;
    uint64_t dwActiveProcessorMask;
    uint32_t dwNumberOfProcessors;
    uint32_t dwProcessorType;
    uint32_t dwAllocationGranularity;
    uint16_t wProcessorLevel;
    uint16_t wProcessorRevision;
} pe_system_info_t;

typedef struct {
    uint32_t dwOSVersionInfoSize;
    uint32_t dwMajorVersion;
    uint32_t dwMinorVersion;
    uint32_t dwBuildNumber;
    uint32_t dwPlatformId;
    char szCSDVersion[128];
} pe_os_version_info_a_t;

typedef struct {
    uint32_t dwOSVersionInfoSize;
    uint32_t dwMajorVersion;
    uint32_t dwMinorVersion;
    uint32_t dwBuildNumber;
    uint32_t dwPlatformId;
    uint16_t szCSDVersion[128];
} pe_os_version_info_w_t;

typedef struct {
    uint16_t wYear;
    uint16_t wMonth;
    uint16_t wDayOfWeek;
    uint16_t wDay;
    uint16_t wHour;
    uint16_t wMinute;
    uint16_t wSecond;
    uint16_t wMilliseconds;
} pe_systemtime_t;

typedef struct {
    int16_t x;
    int16_t y;
} pe_coord_t;

typedef struct {
    int16_t left;
    int16_t top;
    int16_t right;
    int16_t bottom;
} pe_small_rect_t;

typedef struct {
    pe_coord_t size;
    pe_coord_t cursor_position;
    uint16_t attributes;
    pe_small_rect_t window;
    pe_coord_t maximum_window_size;
} pe_console_screen_buffer_info_t;

typedef struct {
    uint16_t Length;
    uint16_t MaximumLength;
    uint16_t *Buffer;
} pe_unicode_string_t;

typedef struct {
    uint8_t used;
    uint32_t pid;
    uint32_t last_error;
    uint64_t tls_bitmap;
    uint64_t tls_values[64];
} pe_process_state_t;

typedef struct {
    uint8_t used;
    uint8_t manual_reset;
    uint8_t signaled;
    int32_t count;
    int32_t maximum;
} pe_sync_object_t;

typedef struct {
    uint8_t used;
    uint8_t exhausted;
} pe_volume_find_t;

#define PE_PROCESS_STATE_CAPACITY 64u
#define PE_SYNC_OBJECT_CAPACITY 64u
#define PE_VOLUME_FIND_CAPACITY 8u
static pe_process_state_t pe_states[PE_PROCESS_STATE_CAPACITY];
static pe_sync_object_t pe_sync_objects[PE_SYNC_OBJECT_CAPACITY];
static pe_volume_find_t pe_volume_finds[PE_VOLUME_FIND_CAPACITY];
static uint32_t pe_last_error_fallback;
static char pe_command_line_a[] = "app";
static uint16_t pe_command_line_w[] = { 'a', 'p', 'p', 0u };
static char pe_env_block_a[] =
    "SystemRoot=C:\\Windows\0"
    "windir=C:\\Windows\0"
    "TEMP=C:\\Temp\0"
    "TMP=C:\\Temp\0"
    "USERPROFILE=C:\\Users\\Default\0"
    "PATH=C:\\Windows\\System32;C:\\Windows;C:\\bin\0"
    "OS=Windows_NT\0"
    "PROCESSOR_ARCHITECTURE=AMD64\0"
    "NUMBER_OF_PROCESSORS=1\0"
    "\0";
static uint16_t pe_env_block_w[] = {
    'S','y','s','t','e','m','R','o','o','t','=','C',':','\\','W','i','n','d','o','w','s',0,
    'w','i','n','d','i','r','=','C',':','\\','W','i','n','d','o','w','s',0,
    'T','E','M','P','=','C',':','\\','T','e','m','p',0,
    'T','M','P','=','C',':','\\','T','e','m','p',0,
    'U','S','E','R','P','R','O','F','I','L','E','=','C',':','\\','U','s','e','r','s','\\','D','e','f','a','u','l','t',0,
    'P','A','T','H','=','C',':','\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2',';','C',':','\\','W','i','n','d','o','w','s',';','C',':','\\','b','i','n',0,
    'O','S','=','W','i','n','d','o','w','s','_','N','T',0,
    'P','R','O','C','E','S','S','O','R','_','A','R','C','H','I','T','E','C','T','U','R','E','=','A','M','D','6','4',0,
    'N','U','M','B','E','R','_','O','F','_','P','R','O','C','E','S','S','O','R','S','=','1',0,
    0
};

static int ascii_iequals(const char *left, const char *right) {
    size_t i = 0;

    if (left == NULL || right == NULL) {
        return 0;
    }

    while (left[i] != '\0' && right[i] != '\0') {
        char a = left[i];
        char b = right[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a + ('a' - 'A'));
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b + ('a' - 'A'));
        }
        if (a != b) {
            return 0;
        }
        ++i;
    }

    return left[i] == right[i];
}

static uint64_t compat_linux_error(uint64_t err) {
    return (uint64_t)(0ull - err);
}

static size_t pe_utf16_to_ascii(char *out, size_t capacity, const uint16_t *wide) {
    size_t length = 0u;

    if (out == NULL || capacity == 0u) {
        return 0u;
    }
    if (wide == NULL) {
        out[0] = '\0';
        return 0u;
    }

    while (wide[length] != 0u && length + 1u < capacity) {
        const uint16_t ch = wide[length];
        out[length] = ch <= 0x7fu ? (char)ch : '?';
        ++length;
    }

    out[length] = '\0';
    return length;
}

static const char *pe_path_arg(uint64_t ptr, int wide, char *buffer, size_t capacity) {
    if (ptr == 0u) {
        return NULL;
    }
    if (!wide) {
        return (const char *)(uintptr_t)ptr;
    }
    pe_utf16_to_ascii(buffer, capacity, (const uint16_t *)(uintptr_t)ptr);
    return buffer;
}

static size_t pe_ascii_len(const char *text) {
    size_t length = 0u;
    if (text == NULL) {
        return 0u;
    }
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static size_t pe_utf16_len(const uint16_t *text) {
    size_t length = 0u;
    if (text == NULL) {
        return 0u;
    }
    while (text[length] != 0u) {
        ++length;
    }
    return length;
}

static void pe_ascii_to_utf16(uint16_t *dst, size_t capacity, const char *src) {
    size_t i = 0u;
    if (dst == NULL || capacity == 0u) {
        return;
    }
    if (src == NULL) {
        dst[0] = 0u;
        return;
    }
    while (i + 1u < capacity && src[i] != '\0') {
        dst[i] = (uint8_t)src[i];
        ++i;
    }
    dst[i] = 0u;
}

static void pe_copy_ascii(char *dst, size_t capacity, const char *src) {
    size_t i = 0u;
    if (dst == NULL || capacity == 0u) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    while (i + 1u < capacity && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static const char *pe_win_path_to_vex(const char *path, char *buffer, size_t capacity) {
    size_t input = 0u;
    size_t output = 0u;

    if (path == NULL || buffer == NULL || capacity == 0u) {
        return NULL;
    }

    if ((path[0] == '\\' || path[0] == '/') &&
        (path[1] == '\\' || path[1] == '/') &&
        path[2] == '?' &&
        (path[3] == '\\' || path[3] == '/')) {
        input = 4u;
    }

    if (((path[input] >= 'A' && path[input] <= 'Z') || (path[input] >= 'a' && path[input] <= 'z')) &&
        path[input + 1u] == ':') {
        input += 2u;
    }

    if (output + 1u < capacity) {
        buffer[output++] = '/';
    }

    while (path[input] == '\\' || path[input] == '/') {
        ++input;
    }

    for (; path[input] != '\0' && output + 1u < capacity; ++input) {
        char ch = path[input] == '\\' ? '/' : path[input];
        if (ch == '/' && output > 0u && buffer[output - 1u] == '/') {
            continue;
        }
        buffer[output++] = ch;
    }

    if (output > 1u && buffer[output - 1u] == '/') {
        --output;
    }
    buffer[output] = '\0';
    return buffer;
}

static int pe_pointer_looks_utf16(uint64_t ptr) {
    const uint8_t *bytes = (const uint8_t *)(uintptr_t)ptr;

    if (bytes == NULL) {
        return 0;
    }
    return bytes[0] != 0u && bytes[1] == 0u;
}

static uint64_t pe_read(uint64_t fd, uint64_t buffer, uint64_t count) {
    vex_fd_io_req_t req;
    req.fd = (int)fd;
    req.buffer = (void *)(uintptr_t)buffer;
    req.count = (size_t)count;
    return vex_syscall_dispatch(VEX_SYS_READFD, (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t pe_write(uint64_t fd, uint64_t buffer, uint64_t count) {
    vex_fd_io_req_t req;
    req.fd = (int)fd;
    req.buffer = (void *)(uintptr_t)buffer;
    req.count = (size_t)count;
    return vex_syscall_dispatch(VEX_SYS_WRITEFD, (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t pe_get_std_handle(uint64_t which) {
    const uint32_t handle = (uint32_t)which;
    if (handle == WIN_STD_INPUT_HANDLE) {
        return 0;
    }
    if (handle == WIN_STD_OUTPUT_HANDLE || handle == WIN_STD_ERROR_HANDLE) {
        return 1;
    }
    return (uint64_t)-1;
}

static uint64_t pe_thunk_address(uint64_t id) {
    return VEX_PE_THUNK_BASE + id * VEX_PE_THUNK_SIZE;
}

static uint64_t pe_make_process_handle(uint32_t pid) {
    return WIN_HANDLE_PROCESS_TAG | (uint64_t)pid;
}

static uint64_t pe_make_thread_handle(uint32_t pid) {
    return WIN_HANDLE_THREAD_TAG | (uint64_t)pid;
}

static int pe_is_process_handle(uint64_t handle) {
    return (handle & 0xffff000000000000ull) == WIN_HANDLE_PROCESS_TAG;
}

static int pe_is_thread_handle(uint64_t handle) {
    return (handle & 0xffff000000000000ull) == WIN_HANDLE_THREAD_TAG;
}

static int pe_is_volume_find_handle(uint64_t handle) {
    return (handle & 0xffff000000000000ull) == WIN_HANDLE_VOLUME_FIND_TAG;
}

static int pe_is_sync_handle(uint64_t handle) {
    return (handle & 0xffff000000000000ull) == WIN_HANDLE_SYNC_TAG;
}

static uint32_t pe_handle_pid(uint64_t handle) {
    return (uint32_t)(handle & 0xffffffffu);
}

static pe_sync_object_t *pe_sync_from_handle(uint64_t handle) {
    const uint32_t index = (uint32_t)(handle & 0xffffffffu);
    if (!pe_is_sync_handle(handle) || index == 0u || index > PE_SYNC_OBJECT_CAPACITY) {
        return NULL;
    }
    if (pe_sync_objects[index - 1u].used == 0u) {
        return NULL;
    }
    return &pe_sync_objects[index - 1u];
}

static pe_volume_find_t *pe_volume_find_from_handle(uint64_t handle) {
    const uint32_t index = (uint32_t)(handle & 0xffffffffu);
    if (!pe_is_volume_find_handle(handle) || index == 0u || index > PE_VOLUME_FIND_CAPACITY) {
        return NULL;
    }
    if (pe_volume_finds[index - 1u].used == 0u) {
        return NULL;
    }
    return &pe_volume_finds[index - 1u];
}

static uint64_t pe_make_volume_find_handle(uint8_t exhausted) {
    for (uint32_t i = 0u; i < PE_VOLUME_FIND_CAPACITY; ++i) {
        if (pe_volume_finds[i].used == 0u) {
            pe_volume_finds[i].used = 1u;
            pe_volume_finds[i].exhausted = exhausted;
            return WIN_HANDLE_VOLUME_FIND_TAG | (uint64_t)(i + 1u);
        }
    }
    return 0u;
}

static uint64_t pe_make_sync_handle(uint8_t manual_reset, uint8_t signaled, int32_t count, int32_t maximum) {
    for (uint32_t i = 0u; i < PE_SYNC_OBJECT_CAPACITY; ++i) {
        if (pe_sync_objects[i].used == 0u) {
            pe_sync_objects[i].used = 1u;
            pe_sync_objects[i].manual_reset = manual_reset;
            pe_sync_objects[i].signaled = signaled;
            pe_sync_objects[i].count = count;
            pe_sync_objects[i].maximum = maximum;
            return WIN_HANDLE_SYNC_TAG | (uint64_t)(i + 1u);
        }
    }
    return 0u;
}

static uint32_t current_pid_or_zero(void) {
    process_t *process = scheduler_current_process();
    return process == NULL ? 0u : process->pid;
}

static uint32_t current_process_id_or_zero(void) {
    return scheduler_current_process_id();
}

static uint64_t pe_ticks_to_ms(uint64_t ticks) {
    return ticks * PE_TICK_MS;
}

static pe_process_state_t *pe_state(void) {
    /* Last-error and PE process state are shared by all threads in the
       process; thread handles/IDs continue to use current_pid_or_zero(). */
    const uint32_t pid = current_process_id_or_zero();

    if (pid == 0u) {
        return NULL;
    }

    for (size_t i = 0; i < PE_PROCESS_STATE_CAPACITY; ++i) {
        if (pe_states[i].used != 0u && pe_states[i].pid == pid) {
            return &pe_states[i];
        }
    }

    for (size_t i = 0; i < PE_PROCESS_STATE_CAPACITY; ++i) {
        if (pe_states[i].used == 0u) {
            pe_states[i].used = 1u;
            pe_states[i].pid = pid;
            pe_states[i].last_error = WIN_ERROR_SUCCESS;
            return &pe_states[i];
        }
    }

    return NULL;
}

static uint32_t pe_last_error_value(void) {
    pe_process_state_t *state = pe_state();
    return state == NULL ? pe_last_error_fallback : state->last_error;
}

static void pe_set_last_error(uint32_t value) {
    pe_process_state_t *state = pe_state();
    if (state == NULL) {
        pe_last_error_fallback = value;
        return;
    }
    state->last_error = value;
}

static uint64_t pe_module_id_from_name(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return WIN_MODULE_KERNEL32;
    }
    if (ascii_iequals(name, "kernel32") || ascii_iequals(name, "kernel32.dll")) {
        return WIN_MODULE_KERNEL32;
    }
    if (ascii_iequals(name, "kernelbase") || ascii_iequals(name, "kernelbase.dll")) {
        return WIN_MODULE_KERNELBASE;
    }
    if (ascii_iequals(name, "ntdll") || ascii_iequals(name, "ntdll.dll")) {
        return WIN_MODULE_NTDLL;
    }
    return 0u;
}

static uint64_t pe_wait_single(uint64_t handle, uint64_t milliseconds) {
    pe_sync_object_t *sync = pe_sync_from_handle(handle);
    if (sync != NULL) {
        if (sync->count > 0 || sync->signaled != 0u || milliseconds != 0u) {
            if (sync->count > 0) {
                --sync->count;
            }
            if (sync->manual_reset == 0u) {
                sync->signaled = 0u;
            }
            pe_set_last_error(WIN_ERROR_SUCCESS);
            return WIN_WAIT_OBJECT_0;
        }
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return WIN_WAIT_TIMEOUT;
    }

    if (pe_is_process_handle(handle) || pe_is_thread_handle(handle)) {
        const uint32_t pid = pe_handle_pid(handle);
        uint32_t reaped_pid = 0u;

        if (!scheduler_process_alive(pid)) {
            pe_set_last_error(WIN_ERROR_SUCCESS);
            return WIN_WAIT_OBJECT_0;
        }
        if (milliseconds == 0u) {
            pe_set_last_error(WIN_ERROR_SUCCESS);
            return WIN_WAIT_TIMEOUT;
        }
        if (milliseconds != WIN_INFINITE) {
            pe_set_last_error(WIN_ERROR_CALL_NOT_IMPLEMENTED);
            return WIN_WAIT_TIMEOUT;
        }
        if (process_wait_pid((int32_t)pid, NULL, 0u, &reaped_pid) < 0) {
            pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
            return WIN_WAIT_TIMEOUT;
        }
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return WIN_WAIT_OBJECT_0;
    }

    pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
    return WIN_WAIT_TIMEOUT;
}

static uint64_t pe_write_file(uint64_t handle, uint64_t buffer, uint64_t count, uint64_t written_ptr) {
    const uint64_t written = pe_write(handle, buffer, count);
    if ((int64_t)written < 0) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0;
    }
    if (written_ptr != 0u) {
        uint32_t *out = (uint32_t *)(uintptr_t)written_ptr;
        *out = (uint32_t)written;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1;
}

static uint64_t pe_read_file(uint64_t handle, uint64_t buffer, uint64_t count, uint64_t read_ptr) {
    const uint64_t read_count = pe_read(handle, buffer, count);
    if ((int64_t)read_count < 0) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0;
    }
    if (read_ptr != 0u) {
        uint32_t *out = (uint32_t *)(uintptr_t)read_ptr;
        *out = (uint32_t)read_count;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1;
}

static uint64_t pe_create_file_impl(const char *file_path, uint64_t desired_access, uint64_t share_mode, uint64_t security, uint64_t creation_disposition) {
    (void)share_mode;
    (void)security;

    uint64_t flags;
    char vex_path[WIN_MAX_PATH];
    const char *open_path;

    if (file_path == NULL || file_path[0] == '\0') {
        pe_set_last_error(WIN_ERROR_PATH_NOT_FOUND);
        return WIN_INVALID_HANDLE_VALUE;
    }
    open_path = pe_win_path_to_vex(file_path, vex_path, sizeof(vex_path));
    if (open_path == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return WIN_INVALID_HANDLE_VALUE;
    }

    if ((desired_access & WIN_GENERIC_READ) != 0u && (desired_access & WIN_GENERIC_WRITE) == 0u) {
        flags = 0u;
    } else if ((desired_access & WIN_GENERIC_WRITE) != 0u && (desired_access & WIN_GENERIC_READ) == 0u) {
        flags = 1u;
    } else {
        flags = 2u;
    }

    if (creation_disposition == WIN_CREATE_NEW || creation_disposition == WIN_CREATE_ALWAYS || creation_disposition == WIN_OPEN_ALWAYS) {
        flags |= 0x40u;
    }
    if (creation_disposition == WIN_CREATE_ALWAYS) {
        flags |= 0x200u;
    }

    {
        const uint64_t fd = vex_syscall_dispatch(VEX_SYS_OPEN, (uint64_t)(uintptr_t)open_path, flags, 0);
        if ((int64_t)fd < 0) {
            pe_set_last_error(creation_disposition == WIN_OPEN_EXISTING ? WIN_ERROR_FILE_NOT_FOUND : WIN_ERROR_INVALID_HANDLE);
            return WIN_INVALID_HANDLE_VALUE;
        }
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return fd;
    }
}

static uint64_t pe_create_file(uint64_t path, uint64_t desired_access, uint64_t share_mode, uint64_t security, uint64_t creation_disposition) {
    char narrow[260];
    const char *file_path = pe_path_arg(path, pe_pointer_looks_utf16(path), narrow, sizeof(narrow));
    return pe_create_file_impl(file_path, desired_access, share_mode, security, creation_disposition);
}

static uint64_t pe_virtual_alloc(uint64_t address, uint64_t size, uint64_t allocation_type, uint64_t protect) {
    vex_mmap_req_t req;

    if (size == 0u || (allocation_type & (WIN_MEM_COMMIT | WIN_MEM_RESERVE)) == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0;
    }

    req.addr = (void *)(uintptr_t)address;
    req.length = (size_t)size;
    req.prot = PROT_READ;
    if (protect == WIN_PAGE_READWRITE || protect == WIN_PAGE_EXECUTE_READWRITE) {
        req.prot |= PROT_WRITE;
    }
    req.flags = 0x20;
    req.fd = -1;
    req.offset = 0;

    {
        const uint64_t result = vex_syscall_dispatch(VEX_SYS_MMAP, (uint64_t)(uintptr_t)&req, 0, 0);
        if (result == 0u || result == (uint64_t)-1) {
            pe_set_last_error(WIN_ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return result;
    }
}

static uint64_t pe_virtual_free(uint64_t address, uint64_t size, uint64_t free_type) {
    if (address == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0;
    }
    if (free_type == WIN_MEM_RELEASE) {
        size = 0u;
    }
    if (size != 0u) {
        vex_memory_req_t req;
        req.addr = (void *)(uintptr_t)address;
        req.length = (size_t)size;
        req.prot_or_advice = 0;
        (void)vex_syscall_dispatch(VEX_SYS_MUNMAP, (uint64_t)(uintptr_t)&req, 0, 0);
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1;
}

static uint64_t pe_heap_alloc(uint64_t heap, uint64_t flags, uint64_t size) {
    (void)heap;
    (void)flags;

    {
        const uint64_t result = vex_syscall_dispatch(VEX_SYS_HEAP_ALLOC, size, 0, 0);
        if (result == 0u) {
            pe_set_last_error(WIN_ERROR_NOT_ENOUGH_MEMORY);
        } else {
            pe_set_last_error(WIN_ERROR_SUCCESS);
        }
        return result;
    }
}

static uint64_t pe_heap_free(uint64_t heap, uint64_t flags, uint64_t ptr) {
    vex_heap_req_t req;

    (void)heap;
    (void)flags;
    req.ptr = (void *)(uintptr_t)ptr;
    req.size = 0u;
    if (vex_syscall_dispatch(VEX_SYS_HEAP_FREE, (uint64_t)(uintptr_t)&req, 0, 0) == 0u) {
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return 1;
    }
    pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
    return 0;
}

static uint64_t pe_local_alloc(uint64_t flags, uint64_t size) {
    return pe_heap_alloc(WIN_PROCESS_HEAP_HANDLE, flags, size);
}

static uint64_t pe_local_realloc(uint64_t ptr, uint64_t size, uint64_t flags) {
    if (ptr == 0u) {
        return pe_local_alloc(flags, size);
    }
    if (size == 0u) {
        (void)pe_heap_free(WIN_PROCESS_HEAP_HANDLE, flags, ptr);
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return 0u;
    }

    /* Minimal Win32-compatible shape: provide a valid resized allocation.
       Existing contents are not preserved until the heap API exposes sizes. */
    {
        const uint64_t new_ptr = pe_heap_alloc(WIN_PROCESS_HEAP_HANDLE, flags, size);
        if (new_ptr != 0u) {
            (void)pe_heap_free(WIN_PROCESS_HEAP_HANDLE, flags, ptr);
        }
        return new_ptr;
    }
}

static uint64_t pe_local_free(uint64_t ptr) {
    if (ptr == 0u) {
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return 0u;
    }
    return pe_heap_free(WIN_PROCESS_HEAP_HANDLE, 0u, ptr) ? 0u : ptr;
}

static uint64_t pe_set_file_pointer(uint64_t handle, uint64_t low_distance, uint64_t high_distance_ptr, uint64_t method) {
    int64_t distance = (int32_t)(uint32_t)low_distance;
    uint64_t result;

    if (high_distance_ptr != 0u) {
        const int32_t high = *(int32_t *)(uintptr_t)high_distance_ptr;
        distance = ((int64_t)high << 32) | (uint32_t)low_distance;
    }

    result = vex_syscall_dispatch(VEX_SYS_LSEEK, handle, (uint64_t)distance, method);
    if (result == (uint64_t)-1) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0xffffffffu;
    }
    if (high_distance_ptr != 0u) {
        *(int32_t *)(uintptr_t)high_distance_ptr = (int32_t)(result >> 32);
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return (uint32_t)result;
}

static uint64_t pe_set_file_pointer_ex(uint64_t handle, uint64_t distance, uint64_t new_pointer_ptr, uint64_t method) {
    const uint64_t result = vex_syscall_dispatch(VEX_SYS_LSEEK, handle, distance, method);
    if (result == (uint64_t)-1) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    if (new_pointer_ptr != 0u) {
        *(uint64_t *)(uintptr_t)new_pointer_ptr = result;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_get_file_size(uint64_t handle, uint64_t high_size_ptr) {
    const uint64_t current = vex_syscall_dispatch(VEX_SYS_LSEEK, handle, 0u, 1u);
    const uint64_t end = vex_syscall_dispatch(VEX_SYS_LSEEK, handle, 0u, 2u);
    if (current == (uint64_t)-1 || end == (uint64_t)-1) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0xffffffffu;
    }
    (void)vex_syscall_dispatch(VEX_SYS_LSEEK, handle, current, 0u);
    if (high_size_ptr != 0u) {
        *(uint32_t *)(uintptr_t)high_size_ptr = (uint32_t)(end >> 32);
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return (uint32_t)end;
}

static uint64_t pe_get_file_size_ex(uint64_t handle, uint64_t size_ptr) {
    const uint64_t current = vex_syscall_dispatch(VEX_SYS_LSEEK, handle, 0u, 1u);
    const uint64_t end = vex_syscall_dispatch(VEX_SYS_LSEEK, handle, 0u, 2u);
    if (size_ptr == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    if (current == (uint64_t)-1 || end == (uint64_t)-1) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    (void)vex_syscall_dispatch(VEX_SYS_LSEEK, handle, current, 0u);
    *(int64_t *)(uintptr_t)size_ptr = (int64_t)end;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_get_console_screen_buffer_info(uint64_t handle, uint64_t info_ptr) {
    pe_console_screen_buffer_info_t *info = (pe_console_screen_buffer_info_t *)(uintptr_t)info_ptr;
    if (info == NULL || (handle != 0u && handle != 1u && handle != 2u)) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    memset(info, 0, sizeof(*info));
    info->size.x = 120;
    info->size.y = 40;
    info->attributes = 0x0007u;
    info->window.left = 0;
    info->window.top = 0;
    info->window.right = 119;
    info->window.bottom = 39;
    info->maximum_window_size = info->size;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_lstrlen(uint64_t text_ptr, int wide) {
    if (text_ptr == 0u) {
        return 0u;
    }
    return wide ? pe_utf16_len((const uint16_t *)(uintptr_t)text_ptr) : pe_ascii_len((const char *)(uintptr_t)text_ptr);
}

static uint64_t pe_lstrcmp(uint64_t left_ptr, uint64_t right_ptr, int wide) {
    if (wide) {
        const uint16_t *left = (const uint16_t *)(uintptr_t)left_ptr;
        const uint16_t *right = (const uint16_t *)(uintptr_t)right_ptr;
        while (left != NULL && right != NULL && *left != 0u && *left == *right) {
            ++left;
            ++right;
        }
        return (uint64_t)(int64_t)((int32_t)(left == NULL ? 0 : *left) - (int32_t)(right == NULL ? 0 : *right));
    }
    {
        const unsigned char *left = (const unsigned char *)(uintptr_t)left_ptr;
        const unsigned char *right = (const unsigned char *)(uintptr_t)right_ptr;
        while (left != NULL && right != NULL && *left != 0u && *left == *right) {
            ++left;
            ++right;
        }
        return (uint64_t)(int64_t)((int32_t)(left == NULL ? 0 : *left) - (int32_t)(right == NULL ? 0 : *right));
    }
}

static uint64_t pe_lstrcpy(uint64_t dst_ptr, uint64_t src_ptr, int wide) {
    if (dst_ptr == 0u || src_ptr == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    if (wide) {
        uint16_t *dst = (uint16_t *)(uintptr_t)dst_ptr;
        const uint16_t *src = (const uint16_t *)(uintptr_t)src_ptr;
        do {
            *dst++ = *src;
        } while (*src++ != 0u);
    } else {
        char *dst = (char *)(uintptr_t)dst_ptr;
        const char *src = (const char *)(uintptr_t)src_ptr;
        do {
            *dst++ = *src;
        } while (*src++ != '\0');
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return dst_ptr;
}

static uint64_t pe_lstrcpyn(uint64_t dst_ptr, uint64_t src_ptr, uint64_t count, int wide) {
    if (dst_ptr == 0u || src_ptr == 0u || count == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    if (wide) {
        pe_ascii_to_utf16((uint16_t *)(uintptr_t)dst_ptr, (size_t)count, "");
        {
            uint16_t *dst = (uint16_t *)(uintptr_t)dst_ptr;
            const uint16_t *src = (const uint16_t *)(uintptr_t)src_ptr;
            size_t i = 0u;
            while (i + 1u < (size_t)count && src[i] != 0u) {
                dst[i] = src[i];
                ++i;
            }
            dst[i] = 0u;
        }
    } else {
        pe_copy_ascii((char *)(uintptr_t)dst_ptr, (size_t)count, (const char *)(uintptr_t)src_ptr);
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return dst_ptr;
}

static uint64_t pe_get_module_handle(uint64_t name_ptr, int wide) {
    char narrow[128];
    const char *name = pe_path_arg(name_ptr, wide || pe_pointer_looks_utf16(name_ptr), narrow, sizeof(narrow));
    const uint64_t module = pe_module_id_from_name(name);

    if (module == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return module;
}

static uint64_t pe_load_library(uint64_t name_ptr, int wide) {
    return pe_get_module_handle(name_ptr, wide);
}

static uint64_t pe_get_current_process(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return pe_make_process_handle(current_process_id_or_zero());
}

static uint64_t pe_get_current_thread(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return pe_make_thread_handle(current_pid_or_zero());
}

static uint64_t pe_get_current_process_id(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return current_process_id_or_zero();
}

static uint64_t pe_get_current_thread_id(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return current_pid_or_zero();
}

static uint64_t pe_get_process_heap(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return WIN_PROCESS_HEAP_HANDLE;
}

static uint64_t pe_heap_create(uint64_t options, uint64_t initial_size, uint64_t maximum_size) {
    (void)options;
    (void)initial_size;
    (void)maximum_size;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return WIN_PROCESS_HEAP_HANDLE;
}

static uint64_t pe_heap_destroy(uint64_t heap) {
    (void)heap;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_sleep(uint64_t milliseconds) {
    if (milliseconds == 0u) {
        process_yield();
    } else {
        process_sleep_ticks((milliseconds + (PE_TICK_MS - 1u)) / PE_TICK_MS);
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 0u;
}

static uint64_t pe_wait_single_ex(uint64_t handle, uint64_t milliseconds, uint64_t alertable) {
    (void)alertable;
    return pe_wait_single(handle, milliseconds);
}

static uint64_t pe_create_event(uint64_t manual_reset, uint64_t initial_state) {
    const uint64_t handle = pe_make_sync_handle(manual_reset != 0u, initial_state != 0u, initial_state != 0u ? 1 : 0, 1);
    pe_set_last_error(handle == 0u ? WIN_ERROR_NOT_ENOUGH_MEMORY : WIN_ERROR_SUCCESS);
    return handle;
}

static uint64_t pe_set_event(uint64_t handle) {
    pe_sync_object_t *sync = pe_sync_from_handle(handle);
    if (sync == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    sync->signaled = 1u;
    if (sync->count == 0) {
        sync->count = 1;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_reset_event(uint64_t handle) {
    pe_sync_object_t *sync = pe_sync_from_handle(handle);
    if (sync == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    sync->signaled = 0u;
    sync->count = 0;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_create_mutex(uint64_t initial_owner) {
    const uint64_t handle = pe_make_sync_handle(0u, initial_owner == 0u, initial_owner == 0u ? 1 : 0, 1);
    pe_set_last_error(handle == 0u ? WIN_ERROR_NOT_ENOUGH_MEMORY : WIN_ERROR_SUCCESS);
    return handle;
}

static uint64_t pe_release_mutex(uint64_t handle) {
    pe_sync_object_t *sync = pe_sync_from_handle(handle);
    if (sync == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    sync->signaled = 1u;
    sync->count = 1;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_create_semaphore(uint64_t initial_count, uint64_t maximum_count) {
    const int32_t max = maximum_count == 0u ? 1 : (int32_t)maximum_count;
    const uint64_t handle = pe_make_sync_handle(1u, initial_count != 0u, (int32_t)initial_count, max);
    pe_set_last_error(handle == 0u ? WIN_ERROR_NOT_ENOUGH_MEMORY : WIN_ERROR_SUCCESS);
    return handle;
}

static uint64_t pe_release_semaphore(uint64_t handle, uint64_t release_count, uint64_t previous_count_ptr) {
    pe_sync_object_t *sync = pe_sync_from_handle(handle);
    if (sync == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    if (previous_count_ptr != 0u) {
        *(int32_t *)(uintptr_t)previous_count_ptr = sync->count;
    }
    sync->count += (int32_t)release_count;
    if (sync->count > sync->maximum) {
        sync->count = sync->maximum;
    }
    sync->signaled = sync->count > 0;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_wait_multiple(uint64_t count, uint64_t handles_ptr, uint64_t wait_all, uint64_t milliseconds) {
    (void)handles_ptr;
    (void)wait_all;
    if (count == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return (uint64_t)-1;
    }
    if (milliseconds == 0u) {
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return WIN_WAIT_OBJECT_0;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return WIN_WAIT_OBJECT_0;
}

static uint64_t pe_get_command_line(int wide) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return wide ? (uint64_t)(uintptr_t)pe_command_line_w : (uint64_t)(uintptr_t)pe_command_line_a;
}

static uint64_t pe_get_startup_info(uint64_t info_ptr, int wide) {
    if (info_ptr == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }

    if (wide) {
        pe_startup_info_w_t *info = (pe_startup_info_w_t *)(uintptr_t)info_ptr;
        memset(info, 0, sizeof(*info));
        info->cb = (uint32_t)sizeof(*info);
        info->std_input = 0u;
        info->std_output = 1u;
        info->std_error = 1u;
    } else {
        pe_startup_info_a_t *info = (pe_startup_info_a_t *)(uintptr_t)info_ptr;
        memset(info, 0, sizeof(*info));
        info->cb = (uint32_t)sizeof(*info);
        info->std_input = 0u;
        info->std_output = 1u;
        info->std_error = 1u;
    }

    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 0u;
}

static uint64_t pe_get_tick_count(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return (uint32_t)pe_ticks_to_ms(scheduler_ticks_now());
}

static uint64_t pe_get_tick_count64(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return pe_ticks_to_ms(scheduler_ticks_now());
}

static uint64_t pe_query_performance_counter(uint64_t out_ptr) {
    int64_t *value = (int64_t *)(uintptr_t)out_ptr;
    if (value == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    *value = (int64_t)scheduler_ticks_now();
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_query_performance_frequency(uint64_t out_ptr) {
    int64_t *value = (int64_t *)(uintptr_t)out_ptr;
    if (value == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    *value = (int64_t)(1000u / PE_TICK_MS);
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_get_file_type(uint64_t handle) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    if (handle <= 2u) {
        return WIN_FILE_TYPE_CHAR;
    }
    if (pe_is_process_handle(handle) || pe_is_thread_handle(handle)) {
        return WIN_FILE_TYPE_UNKNOWN;
    }
    return WIN_FILE_TYPE_DISK;
}

static uint64_t pe_get_console_mode(uint64_t handle, uint64_t mode_ptr) {
    uint32_t *mode = (uint32_t *)(uintptr_t)mode_ptr;

    if ((handle > 2u && !pe_is_process_handle(handle) && !pe_is_thread_handle(handle)) || mode == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    *mode = WIN_DEFAULT_CONSOLE_MODE;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_set_handle_information(uint64_t handle, uint64_t mask, uint64_t flags) {
    (void)mask;
    (void)flags;
    if (handle == 0u || handle == WIN_INVALID_HANDLE_VALUE) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_is_debugger_present(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 0u;
}

static uint64_t pe_get_acp(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 65001u;
}

static uint64_t pe_multi_byte_to_wide(uint64_t code_page, uint64_t flags, uint64_t src_ptr, uint64_t src_len, uint64_t dst_ptr, uint64_t dst_len) {
    const char *src = (const char *)(uintptr_t)src_ptr;
    uint16_t *dst = (uint16_t *)(uintptr_t)dst_ptr;
    size_t length = 0u;
    size_t required;

    (void)code_page;
    (void)flags;
    if (src == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }

    if (src_len == (uint64_t)(uint32_t)-1) {
        while (src[length] != '\0') {
            ++length;
        }
        ++length;
    } else {
        length = (size_t)src_len;
    }
    required = length;
    if (dst == NULL || dst_len == 0u) {
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return required;
    }
    if ((size_t)dst_len < required) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }

    for (size_t i = 0u; i < length; ++i) {
        dst[i] = (uint8_t)src[i];
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return required;
}

static uint64_t pe_wide_char_to_multi(uint64_t code_page, uint64_t flags, uint64_t src_ptr, uint64_t src_len, uint64_t dst_ptr, uint64_t dst_len) {
    const uint16_t *src = (const uint16_t *)(uintptr_t)src_ptr;
    char *dst = (char *)(uintptr_t)dst_ptr;
    size_t length = 0u;
    size_t required;

    (void)code_page;
    (void)flags;
    if (src == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }

    if (src_len == (uint64_t)(uint32_t)-1) {
        while (src[length] != 0u) {
            ++length;
        }
        ++length;
    } else {
        length = (size_t)src_len;
    }
    required = length;
    if (dst == NULL || dst_len == 0u) {
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return required;
    }
    if ((size_t)dst_len < required) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }

    for (size_t i = 0u; i < length; ++i) {
        dst[i] = src[i] <= 0x7fu ? (char)src[i] : '?';
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return required;
}

static uint64_t pe_tls_alloc(void) {
    pe_process_state_t *state = pe_state();
    if (state == NULL) {
        pe_set_last_error(WIN_ERROR_NOT_ENOUGH_MEMORY);
        return WIN_TLS_OUT_OF_INDEXES;
    }
    for (uint32_t index = 0u; index < 64u; ++index) {
        if ((state->tls_bitmap & (1ull << index)) == 0u) {
            state->tls_bitmap |= 1ull << index;
            state->tls_values[index] = 0u;
            pe_set_last_error(WIN_ERROR_SUCCESS);
            return index;
        }
    }
    pe_set_last_error(WIN_ERROR_NOT_ENOUGH_MEMORY);
    return WIN_TLS_OUT_OF_INDEXES;
}

static uint64_t pe_tls_free(uint64_t index) {
    pe_process_state_t *state = pe_state();
    if (state == NULL || index >= 64u || (state->tls_bitmap & (1ull << index)) == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    state->tls_bitmap &= ~(1ull << index);
    state->tls_values[index] = 0u;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_tls_get_value(uint64_t index) {
    pe_process_state_t *state = pe_state();
    if (state == NULL || index >= 64u || (state->tls_bitmap & (1ull << index)) == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return state->tls_values[index];
}

static uint64_t pe_tls_set_value(uint64_t index, uint64_t value) {
    pe_process_state_t *state = pe_state();
    if (state == NULL || index >= 64u || (state->tls_bitmap & (1ull << index)) == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    state->tls_values[index] = value;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_critical_section(uint64_t section_ptr, uint64_t arg2) {
    (void)arg2;
    if (section_ptr != 0u) {
        uint64_t *section = (uint64_t *)(uintptr_t)section_ptr;
        section[0] = 0u;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_current_filetime_value(void) {
    return 116444736000000000ull + pe_ticks_to_ms(scheduler_ticks_now()) * 10000ull;
}

static void pe_civil_from_days(int64_t days, int *year, unsigned *month, unsigned *day) {
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp + (mp < 10 ? 3 : (unsigned)-9);
    y += m <= 2;
    *year = y;
    *month = m;
    *day = d;
}

static void pe_systemtime_from_filetime_value(uint64_t filetime, pe_systemtime_t *out) {
    uint64_t unix_100ns = filetime >= 116444736000000000ull ? filetime - 116444736000000000ull : 0u;
    uint64_t total_ms = unix_100ns / 10000ull;
    uint64_t total_seconds = total_ms / 1000ull;
    const uint32_t ms = (uint32_t)(total_ms % 1000ull);
    const uint64_t days = total_seconds / 86400ull;
    const uint32_t seconds_of_day = (uint32_t)(total_seconds % 86400ull);
    int year = 1970;
    unsigned month = 1u;
    unsigned day = 1u;

    pe_civil_from_days((int64_t)days, &year, &month, &day);
    out->wYear = (uint16_t)year;
    out->wMonth = (uint16_t)month;
    out->wDayOfWeek = (uint16_t)((days + 4u) % 7u);
    out->wDay = (uint16_t)day;
    out->wHour = (uint16_t)(seconds_of_day / 3600u);
    out->wMinute = (uint16_t)((seconds_of_day / 60u) % 60u);
    out->wSecond = (uint16_t)(seconds_of_day % 60u);
    out->wMilliseconds = (uint16_t)ms;
}

static uint64_t pe_get_system_time_as_filetime(uint64_t filetime_ptr) {
    uint64_t *filetime = (uint64_t *)(uintptr_t)filetime_ptr;
    if (filetime == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    *filetime = pe_current_filetime_value();
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 0u;
}

static uint64_t pe_filetime_to_systemtime(uint64_t filetime_ptr, uint64_t systemtime_ptr) {
    const uint64_t *filetime = (const uint64_t *)(uintptr_t)filetime_ptr;
    pe_systemtime_t *systemtime = (pe_systemtime_t *)(uintptr_t)systemtime_ptr;
    if (filetime == NULL || systemtime == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    pe_systemtime_from_filetime_value(*filetime, systemtime);
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_get_system_time(uint64_t systemtime_ptr) {
    pe_systemtime_t *systemtime = (pe_systemtime_t *)(uintptr_t)systemtime_ptr;
    if (systemtime == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    pe_systemtime_from_filetime_value(pe_current_filetime_value(), systemtime);
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 0u;
}

static uint64_t pe_get_module_handle_ex(uint64_t flags, uint64_t name_ptr, uint64_t module_ptr) {
    uint64_t module;
    (void)flags;

    if (module_ptr == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    module = pe_get_module_handle(name_ptr, pe_pointer_looks_utf16(name_ptr));
    if (module == 0u) {
        return 0u;
    }
    *(uint64_t *)(uintptr_t)module_ptr = module;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_stack_arg(uint64_t stack_args, size_t index) {
    const uint64_t *args = (const uint64_t *)(uintptr_t)stack_args;
    if (args == NULL) {
        return 0u;
    }
    return args[index];
}

static const char *pe_env_value(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    if (ascii_iequals(name, "SystemRoot") || ascii_iequals(name, "windir")) {
        return "C:\\Windows";
    }
    if (ascii_iequals(name, "TEMP") || ascii_iequals(name, "TMP")) {
        return "C:\\Temp";
    }
    if (ascii_iequals(name, "USERPROFILE")) {
        return "C:\\Users\\Default";
    }
    if (ascii_iequals(name, "PATH")) {
        return "C:\\Windows\\System32;C:\\Windows;C:\\bin";
    }
    if (ascii_iequals(name, "OS")) {
        return "Windows_NT";
    }
    if (ascii_iequals(name, "PROCESSOR_ARCHITECTURE")) {
        return "AMD64";
    }
    if (ascii_iequals(name, "NUMBER_OF_PROCESSORS")) {
        return "1";
    }
    return NULL;
}

static uint64_t pe_get_environment_variable(uint64_t name_ptr, uint64_t buffer_ptr, uint64_t size, int wide) {
    char name[128];
    const char *key = pe_path_arg(name_ptr, wide, name, sizeof(name));
    const char *value = pe_env_value(key);
    const size_t value_len = pe_ascii_len(value);

    if (key == NULL || value == NULL) {
        pe_set_last_error(WIN_ERROR_FILE_NOT_FOUND);
        return 0u;
    }

    if (wide) {
        uint16_t *buffer = (uint16_t *)(uintptr_t)buffer_ptr;
        if (buffer == NULL || size == 0u) {
            pe_set_last_error(WIN_ERROR_SUCCESS);
            return value_len;
        }
        if (size <= value_len) {
            pe_set_last_error(WIN_ERROR_INSUFFICIENT_BUFFER);
            return value_len + 1u;
        }
        pe_ascii_to_utf16(buffer, (size_t)size, value);
    } else {
        char *buffer = (char *)(uintptr_t)buffer_ptr;
        if (buffer == NULL || size == 0u) {
            pe_set_last_error(WIN_ERROR_SUCCESS);
            return value_len;
        }
        if (size <= value_len) {
            pe_set_last_error(WIN_ERROR_INSUFFICIENT_BUFFER);
            return value_len + 1u;
        }
        pe_copy_ascii(buffer, (size_t)size, value);
    }

    pe_set_last_error(WIN_ERROR_SUCCESS);
    return value_len;
}

static uint64_t pe_get_environment_strings(int wide) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return wide ? (uint64_t)(uintptr_t)pe_env_block_w : (uint64_t)(uintptr_t)pe_env_block_a;
}

static uint64_t pe_free_environment_strings(uint64_t block) {
    (void)block;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_expand_environment_strings(uint64_t src_ptr, uint64_t dst_ptr, uint64_t size, int wide) {
    if (wide) {
        const uint16_t *src = (const uint16_t *)(uintptr_t)src_ptr;
        uint16_t *dst = (uint16_t *)(uintptr_t)dst_ptr;
        const size_t needed = pe_utf16_len(src) + 1u;
        if (src == NULL) {
            pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
            return 0u;
        }
        if (dst != NULL && size != 0u) {
            size_t i = 0u;
            while (i + 1u < (size_t)size && src[i] != 0u) {
                dst[i] = src[i];
                ++i;
            }
            dst[i] = 0u;
        }
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return needed;
    }

    {
        const char *src = (const char *)(uintptr_t)src_ptr;
        char *dst = (char *)(uintptr_t)dst_ptr;
        const size_t needed = pe_ascii_len(src) + 1u;
        if (src == NULL) {
            pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
            return 0u;
        }
        if (dst != NULL && size != 0u) {
            pe_copy_ascii(dst, (size_t)size, src);
        }
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return needed;
    }
}

static uint64_t pe_get_version(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 0x00000a00u;
}

static uint64_t pe_get_version_ex(uint64_t info_ptr, int wide) {
    if (info_ptr == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    if (wide) {
        pe_os_version_info_w_t *info = (pe_os_version_info_w_t *)(uintptr_t)info_ptr;
        info->dwMajorVersion = 10u;
        info->dwMinorVersion = 0u;
        info->dwBuildNumber = 19041u;
        info->dwPlatformId = 2u;
        info->szCSDVersion[0] = 0u;
    } else {
        pe_os_version_info_a_t *info = (pe_os_version_info_a_t *)(uintptr_t)info_ptr;
        info->dwMajorVersion = 10u;
        info->dwMinorVersion = 0u;
        info->dwBuildNumber = 19041u;
        info->dwPlatformId = 2u;
        info->szCSDVersion[0] = '\0';
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_get_system_info(uint64_t info_ptr) {
    pe_system_info_t *info = (pe_system_info_t *)(uintptr_t)info_ptr;
    if (info == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    memset(info, 0, sizeof(*info));
    info->wProcessorArchitecture = 9u;
    info->dwPageSize = 4096u;
    info->lpMinimumApplicationAddress = 0x10000u;
    info->lpMaximumApplicationAddress = 0x00007ffffff000ull;
    info->dwActiveProcessorMask = 1u;
    info->dwNumberOfProcessors = 1u;
    info->dwProcessorType = 8664u;
    info->dwAllocationGranularity = 65536u;
    info->wProcessorLevel = 6u;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 0u;
}

static uint64_t pe_get_file_attributes(uint64_t path_ptr, int wide) {
    char narrow[WIN_MAX_PATH];
    char vex_path[WIN_MAX_PATH];
    struct stat st;
    const char *path = pe_path_arg(path_ptr, wide, narrow, sizeof(narrow));
    const char *resolved = pe_win_path_to_vex(path, vex_path, sizeof(vex_path));
    if (resolved == NULL || stat(resolved, &st) != 0) {
        pe_set_last_error(WIN_ERROR_FILE_NOT_FOUND);
        return WIN_INVALID_FILE_ATTRIBUTES;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    if ((st.st_mode & S_IFMT) == S_IFDIR) {
        return WIN_FILE_ATTRIBUTE_DIRECTORY;
    }
    return WIN_FILE_ATTRIBUTE_ARCHIVE;
}

static uint64_t pe_set_current_directory(uint64_t path_ptr, int wide) {
    char narrow[WIN_MAX_PATH];
    char vex_path[WIN_MAX_PATH];
    const char *path = pe_path_arg(path_ptr, wide, narrow, sizeof(narrow));
    const char *resolved = pe_win_path_to_vex(path, vex_path, sizeof(vex_path));
    if (resolved == NULL || chdir(resolved) != 0) {
        pe_set_last_error(WIN_ERROR_PATH_NOT_FOUND);
        return 0u;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_get_current_directory(uint64_t size, uint64_t buffer_ptr, int wide) {
    char cwd[WIN_MAX_PATH];
    char *got = getcwd(cwd, sizeof(cwd));
    size_t length;
    if (got == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    length = pe_ascii_len(cwd);
    if (wide) {
        uint16_t *buffer = (uint16_t *)(uintptr_t)buffer_ptr;
        if (buffer == NULL || size == 0u || size <= length) {
            pe_set_last_error(WIN_ERROR_INSUFFICIENT_BUFFER);
            return length + 1u;
        }
        pe_ascii_to_utf16(buffer, (size_t)size, cwd);
    } else {
        char *buffer = (char *)(uintptr_t)buffer_ptr;
        if (buffer == NULL || size == 0u || size <= length) {
            pe_set_last_error(WIN_ERROR_INSUFFICIENT_BUFFER);
            return length + 1u;
        }
        pe_copy_ascii(buffer, (size_t)size, cwd);
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return length;
}

static uint64_t pe_create_directory(uint64_t path_ptr, int wide) {
    char narrow[WIN_MAX_PATH];
    char vex_path[WIN_MAX_PATH];
    const char *path = pe_path_arg(path_ptr, wide, narrow, sizeof(narrow));
    const char *resolved = pe_win_path_to_vex(path, vex_path, sizeof(vex_path));
    if (resolved == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    if (mkdir(resolved, 0755u) != 0) {
        pe_set_last_error(WIN_ERROR_ALREADY_EXISTS);
        return 0u;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_remove_directory(uint64_t path_ptr, int wide) {
    char narrow[WIN_MAX_PATH];
    char vex_path[WIN_MAX_PATH];
    const char *path = pe_path_arg(path_ptr, wide, narrow, sizeof(narrow));
    const char *resolved = pe_win_path_to_vex(path, vex_path, sizeof(vex_path));
    if (resolved == NULL || rmdir(resolved) != 0) {
        pe_set_last_error(WIN_ERROR_PATH_NOT_FOUND);
        return 0u;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_delete_file(uint64_t path_ptr, int wide) {
    char narrow[WIN_MAX_PATH];
    char vex_path[WIN_MAX_PATH];
    const char *path = pe_path_arg(path_ptr, wide, narrow, sizeof(narrow));
    const char *resolved = pe_win_path_to_vex(path, vex_path, sizeof(vex_path));
    if (resolved == NULL || unlink(resolved) != 0) {
        pe_set_last_error(WIN_ERROR_FILE_NOT_FOUND);
        return 0u;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_move_file(uint64_t old_ptr, uint64_t new_ptr, int wide) {
    char old_narrow[WIN_MAX_PATH];
    char new_narrow[WIN_MAX_PATH];
    char old_vex[WIN_MAX_PATH];
    char new_vex[WIN_MAX_PATH];
    const char *old_path = pe_path_arg(old_ptr, wide, old_narrow, sizeof(old_narrow));
    const char *new_path = pe_path_arg(new_ptr, wide, new_narrow, sizeof(new_narrow));
    const char *old_resolved = pe_win_path_to_vex(old_path, old_vex, sizeof(old_vex));
    const char *new_resolved = pe_win_path_to_vex(new_path, new_vex, sizeof(new_vex));
    if (old_resolved == NULL || new_resolved == NULL || rename(old_resolved, new_resolved) != 0) {
        pe_set_last_error(WIN_ERROR_FILE_NOT_FOUND);
        return 0u;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_get_full_path_name(uint64_t path_ptr, uint64_t size, uint64_t buffer_ptr, uint64_t file_part_ptr, int wide) {
    char narrow[WIN_MAX_PATH];
    char vex_path[WIN_MAX_PATH];
    const char *path = pe_path_arg(path_ptr, wide, narrow, sizeof(narrow));
    const char *resolved = pe_win_path_to_vex(path, vex_path, sizeof(vex_path));
    size_t length;
    const char *last;
    if (resolved == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    length = pe_ascii_len(resolved);
    last = resolved;
    for (const char *p = resolved; *p != '\0'; ++p) {
        if (*p == '/') {
            last = p + 1;
        }
    }
    if (wide) {
        uint16_t *buffer = (uint16_t *)(uintptr_t)buffer_ptr;
        if (buffer != NULL && size != 0u) {
            pe_ascii_to_utf16(buffer, (size_t)size, resolved);
        }
        if (file_part_ptr != 0u) {
            *(uint16_t **)(uintptr_t)file_part_ptr = buffer != NULL ? buffer + (last - resolved) : NULL;
        }
    } else {
        char *buffer = (char *)(uintptr_t)buffer_ptr;
        if (buffer != NULL && size != 0u) {
            pe_copy_ascii(buffer, (size_t)size, resolved);
        }
        if (file_part_ptr != 0u) {
            *(char **)(uintptr_t)file_part_ptr = buffer != NULL ? buffer + (last - resolved) : NULL;
        }
    }
    pe_set_last_error(size <= length ? WIN_ERROR_INSUFFICIENT_BUFFER : WIN_ERROR_SUCCESS);
    return size <= length ? length + 1u : length;
}

static uint64_t pe_get_temp_path(uint64_t size, uint64_t buffer_ptr, int wide) {
    const char *temp = "C:\\Temp\\";
    const size_t length = pe_ascii_len(temp);
    if (wide) {
        uint16_t *buffer = (uint16_t *)(uintptr_t)buffer_ptr;
        if (buffer != NULL && size != 0u) {
            pe_ascii_to_utf16(buffer, (size_t)size, temp);
        }
    } else {
        char *buffer = (char *)(uintptr_t)buffer_ptr;
        if (buffer != NULL && size != 0u) {
            pe_copy_ascii(buffer, (size_t)size, temp);
        }
    }
    pe_set_last_error(size <= length ? WIN_ERROR_INSUFFICIENT_BUFFER : WIN_ERROR_SUCCESS);
    return size <= length ? length + 1u : length;
}

static uint64_t pe_output_debug_string(uint64_t text_ptr, int wide) {
    char narrow[256];
    const char *text = pe_path_arg(text_ptr, wide, narrow, sizeof(narrow));
    if (text != NULL) {
        debug_log_puts("pe-debug: ");
        debug_log_puts(text);
        debug_log_puts("\n");
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 0u;
}

static uint64_t pe_rtl_zero_memory(uint64_t dst, uint64_t size) {
    if (dst != 0u && size != 0u) {
        memset((void *)(uintptr_t)dst, 0, (size_t)size);
    }
    return 0u;
}

static uint64_t pe_rtl_fill_memory(uint64_t dst, uint64_t size, uint64_t value) {
    if (dst != 0u && size != 0u) {
        memset((void *)(uintptr_t)dst, (int)(uint8_t)value, (size_t)size);
    }
    return 0u;
}

static uint64_t pe_rtl_move_memory(uint64_t dst, uint64_t src, uint64_t size) {
    if (dst != 0u && src != 0u && size != 0u) {
        memmove((void *)(uintptr_t)dst, (const void *)(uintptr_t)src, (size_t)size);
    }
    return 0u;
}

static uint64_t pe_rtl_compare_memory(uint64_t left, uint64_t right, uint64_t size) {
    const uint8_t *a = (const uint8_t *)(uintptr_t)left;
    const uint8_t *b = (const uint8_t *)(uintptr_t)right;
    size_t matched = 0u;
    if (a == NULL || b == NULL) {
        return 0u;
    }
    while (matched < (size_t)size && a[matched] == b[matched]) {
        ++matched;
    }
    return matched;
}

static uint64_t pe_rtl_init_unicode_string(uint64_t dst_ptr, uint64_t src_ptr) {
    pe_unicode_string_t *dst = (pe_unicode_string_t *)(uintptr_t)dst_ptr;
    const uint16_t *src = (const uint16_t *)(uintptr_t)src_ptr;
    size_t length;
    if (dst == NULL) {
        return 0u;
    }
    length = pe_utf16_len(src);
    dst->Length = (uint16_t)(length * sizeof(uint16_t));
    dst->MaximumLength = (uint16_t)((length + 1u) * sizeof(uint16_t));
    dst->Buffer = (uint16_t *)src;
    return 0u;
}

static uint64_t pe_nt_query_information_process(uint64_t process_handle, uint64_t info_class, uint64_t info_ptr, uint64_t info_len, uint64_t return_len_ptr) {
    (void)process_handle;
    (void)info_class;
    if (info_ptr != 0u && info_len != 0u) {
        memset((void *)(uintptr_t)info_ptr, 0, (size_t)info_len);
    }
    if (return_len_ptr != 0u) {
        *(uint32_t *)(uintptr_t)return_len_ptr = (uint32_t)info_len;
    }
    return 0u;
}

static uint64_t pe_nt_delay_execution(uint64_t alertable, uint64_t interval_ptr) {
    const int64_t *interval = (const int64_t *)(uintptr_t)interval_ptr;
    uint64_t ticks = 1u;
    (void)alertable;
    if (interval != NULL && *interval < 0) {
        const uint64_t ms = (uint64_t)(-*interval / 10000);
        ticks = (ms + PE_TICK_MS - 1u) / PE_TICK_MS;
        if (ticks == 0u) {
            ticks = 1u;
        }
    }
    process_sleep_ticks(ticks);
    return 0u;
}

static uint64_t pe_find_first_file(uint64_t pattern_ptr, uint64_t find_data_ptr, int wide) {
    char narrow[WIN_MAX_PATH];
    (void)find_data_ptr;
    (void)pe_path_arg(pattern_ptr, wide, narrow, sizeof(narrow));
    pe_set_last_error(WIN_ERROR_NO_MORE_FILES);
    return WIN_INVALID_HANDLE_VALUE;
}

static uint64_t pe_find_next_file(uint64_t handle, uint64_t find_data_ptr) {
    (void)handle;
    (void)find_data_ptr;
    pe_set_last_error(WIN_ERROR_NO_MORE_FILES);
    return 0u;
}

static uint64_t pe_find_close(uint64_t handle) {
    if (handle == WIN_INVALID_HANDLE_VALUE || handle == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_interlocked_increment(uint64_t ptr) {
    volatile int32_t *value = (volatile int32_t *)(uintptr_t)ptr;
    if (value == NULL) {
        return 0u;
    }
    return (uint32_t)(++(*value));
}

static uint64_t pe_interlocked_decrement(uint64_t ptr) {
    volatile int32_t *value = (volatile int32_t *)(uintptr_t)ptr;
    if (value == NULL) {
        return 0u;
    }
    return (uint32_t)(--(*value));
}

static uint64_t pe_interlocked_exchange(uint64_t ptr, uint64_t new_value) {
    volatile int32_t *value = (volatile int32_t *)(uintptr_t)ptr;
    int32_t old;
    if (value == NULL) {
        return 0u;
    }
    old = *value;
    *value = (int32_t)new_value;
    return (uint32_t)old;
}

static uint64_t pe_interlocked_compare_exchange(uint64_t ptr, uint64_t exchange, uint64_t comparand) {
    volatile int32_t *value = (volatile int32_t *)(uintptr_t)ptr;
    int32_t old;
    if (value == NULL) {
        return 0u;
    }
    old = *value;
    if (old == (int32_t)comparand) {
        *value = (int32_t)exchange;
    }
    return (uint32_t)old;
}

static uint64_t pe_interlocked_exchange_add(uint64_t ptr, uint64_t addend) {
    volatile int32_t *value = (volatile int32_t *)(uintptr_t)ptr;
    int32_t old;
    if (value == NULL) {
        return 0u;
    }
    old = *value;
    *value = old + (int32_t)addend;
    return (uint32_t)old;
}

static uint64_t pe_interlocked_add(uint64_t ptr, uint64_t addend) {
    volatile int32_t *value = (volatile int32_t *)(uintptr_t)ptr;
    if (value == NULL) {
        return 0u;
    }
    *value += (int32_t)addend;
    return (uint32_t)(*value);
}

static uint64_t pe_noop_success(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_noop_false(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 0u;
}

static uint64_t pe_noop_zero(void) {
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 0u;
}

static uint64_t pe_write_fixed_string(uint64_t buffer_ptr, uint64_t size_ptr_or_size, const char *value, int wide, int size_is_pointer) {
    const size_t length = pe_ascii_len(value);
    size_t capacity = 0u;

    if (size_is_pointer) {
        uint32_t *size_ptr = (uint32_t *)(uintptr_t)size_ptr_or_size;
        if (size_ptr == NULL) {
            pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
            return 0u;
        }
        capacity = *size_ptr;
        if (capacity <= length) {
            *size_ptr = (uint32_t)(length + 1u);
            pe_set_last_error(WIN_ERROR_INSUFFICIENT_BUFFER);
            return 0u;
        }
        *size_ptr = (uint32_t)length;
    } else {
        capacity = (size_t)size_ptr_or_size;
        if (capacity <= length) {
            pe_set_last_error(WIN_ERROR_INSUFFICIENT_BUFFER);
            return length + 1u;
        }
    }

    if (wide) {
        pe_ascii_to_utf16((uint16_t *)(uintptr_t)buffer_ptr, capacity, value);
    } else {
        pe_copy_ascii((char *)(uintptr_t)buffer_ptr, capacity, value);
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return size_is_pointer ? 1u : length;
}

static uint64_t pe_write_win_string_bool(uint64_t buffer_ptr, uint64_t capacity, const char *value, int wide) {
    const size_t length = pe_ascii_len(value);

    if (buffer_ptr == 0u || capacity <= length) {
        pe_set_last_error(WIN_ERROR_INSUFFICIENT_BUFFER);
        return 0u;
    }
    if (wide) {
        pe_ascii_to_utf16((uint16_t *)(uintptr_t)buffer_ptr, (size_t)capacity, value);
    } else {
        pe_copy_ascii((char *)(uintptr_t)buffer_ptr, (size_t)capacity, value);
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_get_module_filename(uint64_t module, uint64_t buffer_ptr, uint64_t size, int wide) {
    (void)module;
    return pe_write_fixed_string(buffer_ptr, size, "C:\\app.exe", wide, 0);
}

static uint64_t pe_get_computer_name(uint64_t buffer_ptr, uint64_t size_ptr, int wide) {
    return pe_write_fixed_string(buffer_ptr, size_ptr, "VEXOS", wide, 1);
}

static uint64_t pe_get_user_name(uint64_t buffer_ptr, uint64_t size_ptr, int wide) {
    return pe_write_fixed_string(buffer_ptr, size_ptr, "Default", wide, 1);
}

static uint64_t pe_get_process_affinity_mask(uint64_t process, uint64_t process_mask_ptr, uint64_t system_mask_ptr) {
    (void)process;
    if (process_mask_ptr != 0u) {
        *(uint64_t *)(uintptr_t)process_mask_ptr = 1u;
    }
    if (system_mask_ptr != 0u) {
        *(uint64_t *)(uintptr_t)system_mask_ptr = 1u;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_get_exit_code(uint64_t handle, uint64_t code_ptr) {
    uint32_t *code = (uint32_t *)(uintptr_t)code_ptr;
    if (code == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    *code = (pe_is_process_handle(handle) || pe_is_thread_handle(handle)) && scheduler_process_alive(pe_handle_pid(handle)) ? 259u : 0u;
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_get_system_directory(uint64_t buffer_ptr, uint64_t size, int wide) {
    return pe_write_fixed_string(buffer_ptr, size, "C:\\Windows\\System32", wide, 0);
}

static uint64_t pe_get_windows_directory(uint64_t buffer_ptr, uint64_t size, int wide) {
    return pe_write_fixed_string(buffer_ptr, size, "C:\\Windows", wide, 0);
}

static uint64_t pe_find_first_volume(uint64_t buffer_ptr, uint64_t size, int wide) {
    uint64_t handle;

    if (!pe_write_win_string_bool(buffer_ptr, size, "\\\\?\\Volume{00000000-0000-0000-0000-000000000000}\\", wide)) {
        return WIN_INVALID_HANDLE_VALUE;
    }
    handle = pe_make_volume_find_handle(1u);
    if (handle == 0u) {
        pe_set_last_error(WIN_ERROR_NOT_ENOUGH_MEMORY);
        return WIN_INVALID_HANDLE_VALUE;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return handle;
}

static uint64_t pe_find_next_volume(uint64_t handle, uint64_t buffer_ptr, uint64_t size, int wide) {
    pe_volume_find_t *find = pe_volume_find_from_handle(handle);
    (void)buffer_ptr;
    (void)size;
    (void)wide;

    if (find == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    pe_set_last_error(WIN_ERROR_NO_MORE_FILES);
    return 0u;
}

static uint64_t pe_find_volume_close(uint64_t handle) {
    pe_volume_find_t *find = pe_volume_find_from_handle(handle);
    if (find == NULL) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    }
    memset(find, 0, sizeof(*find));
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_get_logical_drive_strings(uint64_t size, uint64_t buffer_ptr, int wide) {
    const size_t required = 5u; /* "C:\\\0\0" */

    if (buffer_ptr == 0u || size < required) {
        pe_set_last_error(WIN_ERROR_INSUFFICIENT_BUFFER);
        return required;
    }
    if (wide) {
        uint16_t *buffer = (uint16_t *)(uintptr_t)buffer_ptr;
        buffer[0] = 'C';
        buffer[1] = ':';
        buffer[2] = '\\';
        buffer[3] = 0u;
        buffer[4] = 0u;
    } else {
        char *buffer = (char *)(uintptr_t)buffer_ptr;
        buffer[0] = 'C';
        buffer[1] = ':';
        buffer[2] = '\\';
        buffer[3] = '\0';
        buffer[4] = '\0';
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return required - 1u;
}

static uint64_t pe_get_drive_type(uint64_t root_path_ptr, int wide) {
    char narrow[WIN_MAX_PATH];
    const char *root = pe_path_arg(root_path_ptr, wide, narrow, sizeof(narrow));

    if (root == NULL || root[0] == '\0') {
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return WIN_DRIVE_FIXED;
    }
    if ((root[0] == 'C' || root[0] == 'c') && root[1] == ':') {
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return WIN_DRIVE_FIXED;
    }
    if (root[0] == '\\' || root[0] == '/') {
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return WIN_DRIVE_FIXED;
    }
    pe_set_last_error(WIN_ERROR_PATH_NOT_FOUND);
    return WIN_DRIVE_NO_ROOT_DIR;
}

static uint64_t pe_get_volume_information(uint64_t root_path_ptr,
                                          uint64_t volume_name_ptr,
                                          uint64_t volume_name_size,
                                          uint64_t serial_ptr,
                                          uint64_t max_component_len_ptr,
                                          uint64_t fs_flags_ptr,
                                          uint64_t fs_name_ptr,
                                          uint64_t fs_name_size,
                                          int wide) {
    (void)root_path_ptr;

    if (volume_name_ptr != 0u && volume_name_size != 0u &&
        !pe_write_win_string_bool(volume_name_ptr, volume_name_size, "KyIlOS", wide)) {
        return 0u;
    }
    if (serial_ptr != 0u) {
        *(uint32_t *)(uintptr_t)serial_ptr = 0x56584f53u;
    }
    if (max_component_len_ptr != 0u) {
        *(uint32_t *)(uintptr_t)max_component_len_ptr = 255u;
    }
    if (fs_flags_ptr != 0u) {
        *(uint32_t *)(uintptr_t)fs_flags_ptr = 0u;
    }
    if (fs_name_ptr != 0u && fs_name_size != 0u &&
        !pe_write_win_string_bool(fs_name_ptr, fs_name_size, "VEXFS", wide)) {
        return 0u;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_get_volume_path_name(uint64_t file_name_ptr, uint64_t volume_path_ptr, uint64_t size, int wide) {
    (void)file_name_ptr;
    return pe_write_win_string_bool(volume_path_ptr, size, "C:\\", wide);
}

static uint64_t pe_get_disk_free_space_ex(uint64_t path_ptr, uint64_t free_ptr, uint64_t total_ptr, uint64_t total_free_ptr) {
    (void)path_ptr;
    if (free_ptr != 0u) {
        *(uint64_t *)(uintptr_t)free_ptr = 512ull * 1024ull * 1024ull;
    }
    if (total_ptr != 0u) {
        *(uint64_t *)(uintptr_t)total_ptr = 1024ull * 1024ull * 1024ull;
    }
    if (total_free_ptr != 0u) {
        *(uint64_t *)(uintptr_t)total_free_ptr = 512ull * 1024ull * 1024ull;
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_create_process(uint64_t application_ptr, uint64_t command_ptr, uint64_t flags, uint64_t process_info_ptr, int wide) {
    char app_narrow[WIN_MAX_PATH];
    char cmd_narrow[WIN_MAX_PATH];
    char path_vex[WIN_MAX_PATH];
    const char *app = pe_path_arg(application_ptr, wide, app_narrow, sizeof(app_narrow));
    const char *cmd = pe_path_arg(command_ptr, wide, cmd_narrow, sizeof(cmd_narrow));
    char first_token[WIN_MAX_PATH];
    const char *launch = app;
    uint32_t pid = 0u;
    pe_process_information_t *info = (pe_process_information_t *)(uintptr_t)process_info_ptr;

    if ((flags & WIN_CREATE_SUSPENDED) != 0u) {
        pe_set_last_error(WIN_ERROR_CALL_NOT_IMPLEMENTED);
        return 0u;
    }
    if (launch == NULL || launch[0] == '\0') {
        size_t i = 0u;
        size_t o = 0u;
        if (cmd == NULL || cmd[0] == '\0') {
            pe_set_last_error(WIN_ERROR_FILE_NOT_FOUND);
            return 0u;
        }
        if (cmd[i] == '"') {
            ++i;
            while (cmd[i] != '\0' && cmd[i] != '"' && o + 1u < sizeof(first_token)) {
                first_token[o++] = cmd[i++];
            }
        } else {
            while (cmd[i] != '\0' && cmd[i] != ' ' && o + 1u < sizeof(first_token)) {
                first_token[o++] = cmd[i++];
            }
        }
        first_token[o] = '\0';
        launch = first_token;
    }

    if (pe_win_path_to_vex(launch, path_vex, sizeof(path_vex)) == NULL || pe_spawn_pid(path_vex, &pid) != 0 || pid == 0u) {
        pe_set_last_error(WIN_ERROR_FILE_NOT_FOUND);
        return 0u;
    }

    if (info != NULL) {
        info->dwProcessId = pid;
        info->dwThreadId = pid;
        info->hProcess = pe_make_process_handle(pid);
        info->hThread = pe_make_thread_handle(pid);
    }
    pe_set_last_error(WIN_ERROR_SUCCESS);
    return 1u;
}

static uint64_t pe_create_thread(uint64_t stack_size, uint64_t start_address, uint64_t parameter, uint64_t creation_flags) {
    vex_mmap_req_t req;
    uint64_t stack_base;
    uint64_t child_stack;
    uint32_t tid;

    if (start_address == 0u) {
        pe_set_last_error(WIN_ERROR_INVALID_PARAMETER);
        return 0u;
    }
    if ((creation_flags & WIN_CREATE_SUSPENDED) != 0u) {
        pe_set_last_error(WIN_ERROR_CALL_NOT_IMPLEMENTED);
        return 0u;
    }

    req.addr = NULL;
    req.length = (size_t)(stack_size != 0u ? stack_size : WIN_DEFAULT_THREAD_STACK);
    req.prot = PROT_READ | PROT_WRITE;
    req.flags = MAP_PRIVATE | MAP_ANONYMOUS;
    req.fd = -1;
    req.offset = 0;

    stack_base = vex_syscall_dispatch(VEX_SYS_MMAP, (uint64_t)(uintptr_t)&req, 0, 0);
    if (stack_base == 0u || stack_base == (uint64_t)-1) {
        pe_set_last_error(WIN_ERROR_NOT_ENOUGH_MEMORY);
        return 0u;
    }

    child_stack = stack_base + req.length;
    tid = process_clone_user_thread(child_stack, start_address, parameter, scheduler_current_fs_base());
    if (tid == 0u) {
        vex_memory_req_t free_req;
        free_req.addr = (void *)(uintptr_t)stack_base;
        free_req.length = req.length;
        free_req.prot_or_advice = 0;
        (void)vex_syscall_dispatch(VEX_SYS_MUNMAP, (uint64_t)(uintptr_t)&free_req, 0, 0);
        pe_set_last_error(WIN_ERROR_CALL_NOT_IMPLEMENTED);
        return 0u;
    }

    pe_set_last_error(WIN_ERROR_SUCCESS);
    return pe_make_thread_handle(tid);
}

typedef struct pe_get_proc_alias {
    const char *name;
    uint64_t id;
} pe_get_proc_alias_t;

static const pe_get_proc_alias_t pe_get_proc_aliases[] = {
    { "HeapReAlloc", VEX_PE_HEAP_ALLOC },
    { "HeapSize", VEX_PE_NOOP_ZERO },
    { "HeapValidate", VEX_PE_NOOP_SUCCESS },
    { "VirtualProtect", VEX_PE_NOOP_SUCCESS },
    { "VirtualProtectEx", VEX_PE_NOOP_SUCCESS },
    { "VirtualQuery", VEX_PE_NOOP_ZERO },
    { "VirtualQueryEx", VEX_PE_NOOP_ZERO },
    { "VirtualAllocEx", VEX_PE_VIRTUAL_ALLOC },
    { "VirtualFreeEx", VEX_PE_VIRTUAL_FREE },
    { "FlushInstructionCache", VEX_PE_NOOP_SUCCESS },
    { "LocalAlloc", VEX_PE_LOCAL_ALLOC },
    { "LocalReAlloc", VEX_PE_LOCAL_REALLOC },
    { "LocalFree", VEX_PE_LOCAL_FREE },
    { "GlobalAlloc", VEX_PE_LOCAL_ALLOC },
    { "GlobalReAlloc", VEX_PE_LOCAL_REALLOC },
    { "GlobalFree", VEX_PE_LOCAL_FREE },
    { "GetProcessAffinityMask", VEX_PE_GET_PROCESS_AFFINITY_MASK },
    { "GetExitCodeProcess", VEX_PE_GET_EXIT_CODE_PROCESS },
    { "GetExitCodeThread", VEX_PE_GET_EXIT_CODE_THREAD },
    { "SwitchToThread", VEX_PE_NOOP_SUCCESS },
    { "SleepEx", VEX_PE_SLEEP },
    { "GetSystemTimePreciseAsFileTime", VEX_PE_GET_SYSTEM_TIME_AS_FILETIME },
    { "CreateEventExA", VEX_PE_CREATE_EVENT },
    { "CreateEventExW", VEX_PE_CREATE_EVENT },
    { "OpenEventA", VEX_PE_CREATE_EVENT },
    { "OpenEventW", VEX_PE_CREATE_EVENT },
    { "CreateMutexExA", VEX_PE_CREATE_MUTEX },
    { "CreateMutexExW", VEX_PE_CREATE_MUTEX },
    { "OpenMutexA", VEX_PE_CREATE_MUTEX },
    { "OpenMutexW", VEX_PE_CREATE_MUTEX },
    { "CreateSemaphoreExA", VEX_PE_CREATE_SEMAPHORE },
    { "CreateSemaphoreExW", VEX_PE_CREATE_SEMAPHORE },
    { "OpenSemaphoreA", VEX_PE_CREATE_SEMAPHORE },
    { "OpenSemaphoreW", VEX_PE_CREATE_SEMAPHORE },
    { "WaitForMultipleObjectsEx", VEX_PE_WAIT_MULTIPLE },
    { "FlsAlloc", VEX_PE_TLS_ALLOC },
    { "FlsFree", VEX_PE_TLS_FREE },
    { "FlsGetValue", VEX_PE_TLS_GET_VALUE },
    { "FlsSetValue", VEX_PE_TLS_SET_VALUE },
    { "InterlockedIncrementAcquire", VEX_PE_INTERLOCKED_INC },
    { "InterlockedIncrementRelease", VEX_PE_INTERLOCKED_INC },
    { "InterlockedDecrementAcquire", VEX_PE_INTERLOCKED_DEC },
    { "InterlockedDecrementRelease", VEX_PE_INTERLOCKED_DEC },
    { "InterlockedExchangePointer", VEX_PE_INTERLOCKED_EXCHANGE },
    { "InterlockedCompareExchangePointer", VEX_PE_INTERLOCKED_COMPARE_EXCHANGE },
    { "GetModuleFileNameA", VEX_PE_GET_MODULE_FILE_A },
    { "GetModuleFileNameW", VEX_PE_GET_MODULE_FILE_W },
    { "GetComputerNameA", VEX_PE_GET_COMPUTER_NAME_A },
    { "GetComputerNameW", VEX_PE_GET_COMPUTER_NAME_W },
    { "GetUserNameA", VEX_PE_GET_USER_NAME_A },
    { "GetUserNameW", VEX_PE_GET_USER_NAME_W },
    { "GetSystemDirectoryA", VEX_PE_GET_SYSTEM_DIRECTORY_A },
    { "GetSystemDirectoryW", VEX_PE_GET_SYSTEM_DIRECTORY_W },
    { "GetWindowsDirectoryA", VEX_PE_GET_WINDOWS_DIRECTORY_A },
    { "GetWindowsDirectoryW", VEX_PE_GET_WINDOWS_DIRECTORY_W },
    { "GetLogicalDrives", VEX_PE_GET_LOGICAL_DRIVES },
    { "GetLogicalDriveStringsA", VEX_PE_GET_LOGICAL_DRIVE_STRINGS_A },
    { "GetLogicalDriveStringsW", VEX_PE_GET_LOGICAL_DRIVE_STRINGS_W },
    { "GetDriveTypeA", VEX_PE_GET_DRIVE_TYPE_A },
    { "GetDriveTypeW", VEX_PE_GET_DRIVE_TYPE_W },
    { "GetDiskFreeSpaceExA", VEX_PE_GET_DISK_FREE_SPACE_EX },
    { "GetDiskFreeSpaceExW", VEX_PE_GET_DISK_FREE_SPACE_EX },
    { "FindFirstVolumeA", VEX_PE_FIND_FIRST_VOLUME_A },
    { "FindFirstVolumeW", VEX_PE_FIND_FIRST_VOLUME_W },
    { "FindNextVolumeA", VEX_PE_FIND_NEXT_VOLUME_A },
    { "FindNextVolumeW", VEX_PE_FIND_NEXT_VOLUME_W },
    { "FindVolumeClose", VEX_PE_FIND_VOLUME_CLOSE },
    { "GetVolumeInformationA", VEX_PE_GET_VOLUME_INFORMATION_A },
    { "GetVolumeInformationW", VEX_PE_GET_VOLUME_INFORMATION_W },
    { "GetVolumePathNameA", VEX_PE_GET_VOLUME_PATH_NAME_A },
    { "GetVolumePathNameW", VEX_PE_GET_VOLUME_PATH_NAME_W },
    { "SetFilePointer", VEX_PE_SET_FILE_POINTER },
    { "SetFilePointerEx", VEX_PE_SET_FILE_POINTER_EX },
    { "GetFileSize", VEX_PE_GET_FILE_SIZE },
    { "GetFileSizeEx", VEX_PE_GET_FILE_SIZE_EX },
    { "FlushFileBuffers", VEX_PE_NOOP_SUCCESS },
    { "CreateFileMappingA", VEX_PE_NOOP_ZERO },
    { "CreateFileMappingW", VEX_PE_NOOP_ZERO },
    { "MapViewOfFile", VEX_PE_NOOP_ZERO },
    { "UnmapViewOfFile", VEX_PE_NOOP_SUCCESS },
    { "ReadConsoleA", VEX_PE_READ_FILE },
    { "ReadConsoleW", VEX_PE_READ_FILE },
    { "WriteConsoleA", VEX_PE_WRITE_FILE },
    { "WriteConsoleW", VEX_PE_WRITE_FILE },
    { "SetConsoleMode", VEX_PE_NOOP_SUCCESS },
    { "GetConsoleScreenBufferInfo", VEX_PE_GET_CONSOLE_SCREEN_BUFFER_INFO },
    { "SetConsoleCtrlHandler", VEX_PE_NOOP_SUCCESS },
    { "GetConsoleCP", VEX_PE_GET_ACP },
    { "GetConsoleOutputCP", VEX_PE_GET_ACP },
    { "GetCPInfo", VEX_PE_NOOP_SUCCESS },
    { "IsValidCodePage", VEX_PE_NOOP_SUCCESS },
    { "CompareStringA", VEX_PE_NOOP_ZERO },
    { "CompareStringW", VEX_PE_NOOP_ZERO },
    { "LCMapStringA", VEX_PE_NOOP_ZERO },
    { "LCMapStringW", VEX_PE_NOOP_ZERO },
    { "GetStringTypeA", VEX_PE_NOOP_FALSE },
    { "GetStringTypeW", VEX_PE_NOOP_FALSE },
    { "lstrlenA", VEX_PE_LSTRLEN_A },
    { "lstrlenW", VEX_PE_LSTRLEN_W },
    { "lstrcmpA", VEX_PE_LSTRCMP_A },
    { "lstrcmpW", VEX_PE_LSTRCMP_W },
    { "lstrcmpiA", VEX_PE_LSTRCMP_A },
    { "lstrcmpiW", VEX_PE_LSTRCMP_W },
    { "lstrcpyA", VEX_PE_LSTRCPY_A },
    { "lstrcpyW", VEX_PE_LSTRCPY_W },
    { "lstrcpynA", VEX_PE_LSTRCPYN_A },
    { "lstrcpynW", VEX_PE_LSTRCPYN_W },
    { "FormatMessageA", VEX_PE_NOOP_ZERO },
    { "FormatMessageW", VEX_PE_NOOP_ZERO },
    { "EncodePointer", VEX_PE_NOOP_ZERO },
    { "DecodePointer", VEX_PE_NOOP_ZERO },
    { "IsProcessorFeaturePresent", VEX_PE_NOOP_FALSE },
    { "LoadLibraryExA", VEX_PE_LOAD_LIBRARY },
    { "LoadLibraryExW", VEX_PE_LOAD_LIBRARY },
    { "FreeLibrary", VEX_PE_NOOP_SUCCESS },
    { "DisableThreadLibraryCalls", VEX_PE_NOOP_SUCCESS },
    { "RtlSecureZeroMemory", VEX_PE_RTL_ZERO_MEMORY },
    { "RtlInitializeCriticalSection", VEX_PE_CRITICAL_SECTION },
    { "RtlEnterCriticalSection", VEX_PE_CRITICAL_SECTION },
    { "RtlLeaveCriticalSection", VEX_PE_CRITICAL_SECTION },
    { "RtlDeleteCriticalSection", VEX_PE_CRITICAL_SECTION },
    { "RtlTryEnterCriticalSection", VEX_PE_CRITICAL_SECTION },
    { "RtlGetNtVersionNumbers", VEX_PE_GET_VERSION },
    { "RtlQueryPerformanceCounter", VEX_PE_QUERY_PERF_COUNTER },
    { "RtlQueryPerformanceFrequency", VEX_PE_QUERY_PERF_FREQ },
    { "RtlNtStatusToDosError", VEX_PE_NOOP_ZERO },
    { "NtQuerySystemTime", VEX_PE_GET_SYSTEM_TIME_AS_FILETIME },
    { "ZwQuerySystemTime", VEX_PE_GET_SYSTEM_TIME_AS_FILETIME },
    { "NtQueryPerformanceCounter", VEX_PE_QUERY_PERF_COUNTER },
    { "ZwQueryPerformanceCounter", VEX_PE_QUERY_PERF_COUNTER },
    { "NtYieldExecution", VEX_PE_SLEEP },
    { "ZwYieldExecution", VEX_PE_SLEEP },
    { "NtCreateEvent", VEX_PE_CREATE_EVENT },
    { "ZwCreateEvent", VEX_PE_CREATE_EVENT },
    { "NtWaitForSingleObject", VEX_PE_WAIT_SINGLE },
    { "ZwWaitForSingleObject", VEX_PE_WAIT_SINGLE },
    { "NtWaitForMultipleObjects", VEX_PE_WAIT_MULTIPLE },
    { "ZwWaitForMultipleObjects", VEX_PE_WAIT_MULTIPLE },
    { "NtAllocateVirtualMemory", VEX_PE_VIRTUAL_ALLOC },
    { "ZwAllocateVirtualMemory", VEX_PE_VIRTUAL_ALLOC },
    { "NtFreeVirtualMemory", VEX_PE_VIRTUAL_FREE },
    { "ZwFreeVirtualMemory", VEX_PE_VIRTUAL_FREE },
    { "NtProtectVirtualMemory", VEX_PE_NOOP_SUCCESS },
    { "ZwProtectVirtualMemory", VEX_PE_NOOP_SUCCESS },
    { "NtQueryVirtualMemory", VEX_PE_NOOP_ZERO },
    { "ZwQueryVirtualMemory", VEX_PE_NOOP_ZERO },
    { "NtReadFile", VEX_PE_READ_FILE },
    { "ZwReadFile", VEX_PE_READ_FILE },
    { "NtWriteFile", VEX_PE_WRITE_FILE },
    { "ZwWriteFile", VEX_PE_WRITE_FILE },
    { "NtCreateFile", VEX_PE_CREATE_FILE },
    { "ZwCreateFile", VEX_PE_CREATE_FILE },
    { "NtOpenFile", VEX_PE_CREATE_FILE },
    { "ZwOpenFile", VEX_PE_CREATE_FILE },
    { NULL, 0u }
};

static uint64_t pe_get_proc_alias_id(const char *name) {
    for (size_t i = 0u; pe_get_proc_aliases[i].name != NULL; ++i) {
        if (ascii_iequals(name, pe_get_proc_aliases[i].name)) {
            return pe_get_proc_aliases[i].id;
        }
    }
    return 0u;
}

static uint64_t pe_get_proc(uint64_t module, uint64_t name_ptr) {
    const char *name = (const char *)(uintptr_t)name_ptr;
    uint64_t alias_id;
    if (name == NULL) {
        return 0;
    }
    if (module != WIN_MODULE_KERNEL32 &&
        module != WIN_MODULE_KERNELBASE &&
        module != WIN_MODULE_NTDLL) {
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0;
    }

    alias_id = pe_get_proc_alias_id(name);
    if (alias_id != 0u) {
        return pe_thunk_address(alias_id);
    }

    if (ascii_iequals(name, "GetStdHandle")) {
        return pe_thunk_address(VEX_PE_GET_STD_HANDLE);
    }
    if (ascii_iequals(name, "WriteFile")) {
        return pe_thunk_address(VEX_PE_WRITE_FILE);
    }
    if (ascii_iequals(name, "ExitProcess")) {
        return pe_thunk_address(VEX_PE_EXIT_PROCESS);
    }
    if (ascii_iequals(name, "ReadFile")) {
        return pe_thunk_address(VEX_PE_READ_FILE);
    }
    if (ascii_iequals(name, "GetLastError")) {
        return pe_thunk_address(VEX_PE_GET_LAST_ERROR);
    }
    if (ascii_iequals(name, "SetLastError")) {
        return pe_thunk_address(VEX_PE_SET_LAST_ERROR);
    }
    if (ascii_iequals(name, "CreateFileA") || ascii_iequals(name, "CreateFileW")) {
        return pe_thunk_address(VEX_PE_CREATE_FILE);
    }
    if (ascii_iequals(name, "CloseHandle")) {
        return pe_thunk_address(VEX_PE_CLOSE_HANDLE);
    }
    if (ascii_iequals(name, "VirtualAlloc")) {
        return pe_thunk_address(VEX_PE_VIRTUAL_ALLOC);
    }
    if (ascii_iequals(name, "VirtualFree")) {
        return pe_thunk_address(VEX_PE_VIRTUAL_FREE);
    }
    if (ascii_iequals(name, "CreateProcessA") || ascii_iequals(name, "CreateProcessW")) {
        return pe_thunk_address(VEX_PE_CREATE_PROCESS);
    }
    if (ascii_iequals(name, "WaitForSingleObject")) {
        return pe_thunk_address(VEX_PE_WAIT_SINGLE);
    }
    if (ascii_iequals(name, "CreateThread")) {
        return pe_thunk_address(VEX_PE_CREATE_THREAD);
    }
    if (ascii_iequals(name, "HeapAlloc")) {
        return pe_thunk_address(VEX_PE_HEAP_ALLOC);
    }
    if (ascii_iequals(name, "HeapFree")) {
        return pe_thunk_address(VEX_PE_HEAP_FREE);
    }
    if (ascii_iequals(name, "GetModuleHandleA") || ascii_iequals(name, "GetModuleHandleW")) {
        return pe_thunk_address(VEX_PE_GET_MODULE);
    }
    if (ascii_iequals(name, "LoadLibraryA") || ascii_iequals(name, "LoadLibraryW")) {
        return pe_thunk_address(VEX_PE_LOAD_LIBRARY);
    }
    if (ascii_iequals(name, "GetProcAddress")) {
        return pe_thunk_address(VEX_PE_GET_PROC);
    }
    if (ascii_iequals(name, "GetCurrentProcess")) {
        return pe_thunk_address(VEX_PE_GET_CURRENT_PROCESS);
    }
    if (ascii_iequals(name, "GetCurrentThread")) {
        return pe_thunk_address(VEX_PE_GET_CURRENT_THREAD);
    }
    if (ascii_iequals(name, "GetCurrentProcessId")) {
        return pe_thunk_address(VEX_PE_GET_CURRENT_PROCESS_ID);
    }
    if (ascii_iequals(name, "GetCurrentThreadId")) {
        return pe_thunk_address(VEX_PE_GET_CURRENT_THREAD_ID);
    }
    if (ascii_iequals(name, "GetProcessHeap")) {
        return pe_thunk_address(VEX_PE_GET_PROCESS_HEAP);
    }
    if (ascii_iequals(name, "HeapCreate")) {
        return pe_thunk_address(VEX_PE_HEAP_CREATE);
    }
    if (ascii_iequals(name, "HeapDestroy")) {
        return pe_thunk_address(VEX_PE_HEAP_DESTROY);
    }
    if (ascii_iequals(name, "Sleep")) {
        return pe_thunk_address(VEX_PE_SLEEP);
    }
    if (ascii_iequals(name, "WaitForSingleObjectEx")) {
        return pe_thunk_address(VEX_PE_WAIT_SINGLE_EX);
    }
    if (ascii_iequals(name, "GetCommandLineA")) {
        return pe_thunk_address(VEX_PE_GET_COMMAND_LINE_A);
    }
    if (ascii_iequals(name, "GetCommandLineW")) {
        return pe_thunk_address(VEX_PE_GET_COMMAND_LINE_W);
    }
    if (ascii_iequals(name, "GetStartupInfoA")) {
        return pe_thunk_address(VEX_PE_GET_STARTUP_INFO_A);
    }
    if (ascii_iequals(name, "GetStartupInfoW")) {
        return pe_thunk_address(VEX_PE_GET_STARTUP_INFO_W);
    }
    if (ascii_iequals(name, "GetTickCount")) {
        return pe_thunk_address(VEX_PE_GET_TICK_COUNT);
    }
    if (ascii_iequals(name, "GetTickCount64")) {
        return pe_thunk_address(VEX_PE_GET_TICK_COUNT64);
    }
    if (ascii_iequals(name, "QueryPerformanceCounter")) {
        return pe_thunk_address(VEX_PE_QUERY_PERF_COUNTER);
    }
    if (ascii_iequals(name, "QueryPerformanceFrequency")) {
        return pe_thunk_address(VEX_PE_QUERY_PERF_FREQ);
    }
    if (ascii_iequals(name, "GetFileType")) {
        return pe_thunk_address(VEX_PE_GET_FILE_TYPE);
    }
    if (ascii_iequals(name, "GetConsoleMode")) {
        return pe_thunk_address(VEX_PE_GET_CONSOLE_MODE);
    }
    if (ascii_iequals(name, "SetHandleInformation")) {
        return pe_thunk_address(VEX_PE_SET_HANDLE_INFO);
    }
    if (ascii_iequals(name, "IsDebuggerPresent")) {
        return pe_thunk_address(VEX_PE_IS_DEBUGGER_PRESENT);
    }
    if (ascii_iequals(name, "GetACP") || ascii_iequals(name, "GetOEMCP")) {
        return pe_thunk_address(VEX_PE_GET_ACP);
    }
    if (ascii_iequals(name, "MultiByteToWideChar")) {
        return pe_thunk_address(VEX_PE_MULTI_BYTE_TO_WIDE);
    }
    if (ascii_iequals(name, "WideCharToMultiByte")) {
        return pe_thunk_address(VEX_PE_WIDE_CHAR_TO_MULTI);
    }
    if (ascii_iequals(name, "TlsAlloc")) {
        return pe_thunk_address(VEX_PE_TLS_ALLOC);
    }
    if (ascii_iequals(name, "TlsFree")) {
        return pe_thunk_address(VEX_PE_TLS_FREE);
    }
    if (ascii_iequals(name, "TlsGetValue")) {
        return pe_thunk_address(VEX_PE_TLS_GET_VALUE);
    }
    if (ascii_iequals(name, "TlsSetValue")) {
        return pe_thunk_address(VEX_PE_TLS_SET_VALUE);
    }
    if (ascii_iequals(name, "InitializeCriticalSection") ||
        ascii_iequals(name, "InitializeCriticalSectionAndSpinCount") ||
        ascii_iequals(name, "InitializeCriticalSectionEx") ||
        ascii_iequals(name, "EnterCriticalSection") ||
        ascii_iequals(name, "LeaveCriticalSection") ||
        ascii_iequals(name, "DeleteCriticalSection") ||
        ascii_iequals(name, "TryEnterCriticalSection") ||
        ascii_iequals(name, "InitializeSRWLock") ||
        ascii_iequals(name, "AcquireSRWLockExclusive") ||
        ascii_iequals(name, "AcquireSRWLockShared") ||
        ascii_iequals(name, "ReleaseSRWLockExclusive") ||
        ascii_iequals(name, "ReleaseSRWLockShared") ||
        ascii_iequals(name, "TryAcquireSRWLockExclusive") ||
        ascii_iequals(name, "TryAcquireSRWLockShared") ||
        ascii_iequals(name, "InitializeConditionVariable") ||
        ascii_iequals(name, "WakeConditionVariable") ||
        ascii_iequals(name, "WakeAllConditionVariable") ||
        ascii_iequals(name, "SleepConditionVariableCS") ||
        ascii_iequals(name, "SleepConditionVariableSRW")) {
        return pe_thunk_address(VEX_PE_CRITICAL_SECTION);
    }
    if (ascii_iequals(name, "GetSystemTimeAsFileTime")) {
        return pe_thunk_address(VEX_PE_GET_SYSTEM_TIME_AS_FILETIME);
    }
    if (ascii_iequals(name, "GetModuleHandleExA") || ascii_iequals(name, "GetModuleHandleExW")) {
        return pe_thunk_address(VEX_PE_GET_MODULE_EX);
    }
    if (ascii_iequals(name, "GetEnvironmentVariableA")) {
        return pe_thunk_address(VEX_PE_GET_ENV_VAR_A);
    }
    if (ascii_iequals(name, "GetEnvironmentVariableW")) {
        return pe_thunk_address(VEX_PE_GET_ENV_VAR_W);
    }
    if (ascii_iequals(name, "GetEnvironmentStrings") || ascii_iequals(name, "GetEnvironmentStringsA")) {
        return pe_thunk_address(VEX_PE_GET_ENV_STRINGS_A);
    }
    if (ascii_iequals(name, "GetEnvironmentStringsW")) {
        return pe_thunk_address(VEX_PE_GET_ENV_STRINGS_W);
    }
    if (ascii_iequals(name, "FreeEnvironmentStringsA") || ascii_iequals(name, "FreeEnvironmentStringsW")) {
        return pe_thunk_address(VEX_PE_FREE_ENV_STRINGS);
    }
    if (ascii_iequals(name, "ExpandEnvironmentStringsA")) {
        return pe_thunk_address(VEX_PE_EXPAND_ENV_A);
    }
    if (ascii_iequals(name, "ExpandEnvironmentStringsW")) {
        return pe_thunk_address(VEX_PE_EXPAND_ENV_W);
    }
    if (ascii_iequals(name, "GetVersion")) {
        return pe_thunk_address(VEX_PE_GET_VERSION);
    }
    if (ascii_iequals(name, "GetVersionExA")) {
        return pe_thunk_address(VEX_PE_GET_VERSION_EX_A);
    }
    if (ascii_iequals(name, "GetVersionExW")) {
        return pe_thunk_address(VEX_PE_GET_VERSION_EX_W);
    }
    if (ascii_iequals(name, "GetSystemInfo") || ascii_iequals(name, "GetNativeSystemInfo")) {
        return pe_thunk_address(VEX_PE_GET_SYSTEM_INFO);
    }
    if (ascii_iequals(name, "GetFileAttributesA")) {
        return pe_thunk_address(VEX_PE_GET_FILE_ATTR_A);
    }
    if (ascii_iequals(name, "GetFileAttributesW")) {
        return pe_thunk_address(VEX_PE_GET_FILE_ATTR_W);
    }
    if (ascii_iequals(name, "SetCurrentDirectoryA")) {
        return pe_thunk_address(VEX_PE_SET_CURRENT_DIR_A);
    }
    if (ascii_iequals(name, "SetCurrentDirectoryW")) {
        return pe_thunk_address(VEX_PE_SET_CURRENT_DIR_W);
    }
    if (ascii_iequals(name, "GetCurrentDirectoryA")) {
        return pe_thunk_address(VEX_PE_GET_CURRENT_DIR_A);
    }
    if (ascii_iequals(name, "GetCurrentDirectoryW")) {
        return pe_thunk_address(VEX_PE_GET_CURRENT_DIR_W);
    }
    if (ascii_iequals(name, "CreateDirectoryA")) {
        return pe_thunk_address(VEX_PE_CREATE_DIR_A);
    }
    if (ascii_iequals(name, "CreateDirectoryW")) {
        return pe_thunk_address(VEX_PE_CREATE_DIR_W);
    }
    if (ascii_iequals(name, "RemoveDirectoryA")) {
        return pe_thunk_address(VEX_PE_REMOVE_DIR_A);
    }
    if (ascii_iequals(name, "RemoveDirectoryW")) {
        return pe_thunk_address(VEX_PE_REMOVE_DIR_W);
    }
    if (ascii_iequals(name, "DeleteFileA")) {
        return pe_thunk_address(VEX_PE_DELETE_FILE_A);
    }
    if (ascii_iequals(name, "DeleteFileW")) {
        return pe_thunk_address(VEX_PE_DELETE_FILE_W);
    }
    if (ascii_iequals(name, "MoveFileA") || ascii_iequals(name, "MoveFileExA")) {
        return pe_thunk_address(VEX_PE_MOVE_FILE_A);
    }
    if (ascii_iequals(name, "MoveFileW") || ascii_iequals(name, "MoveFileExW")) {
        return pe_thunk_address(VEX_PE_MOVE_FILE_W);
    }
    if (ascii_iequals(name, "GetFullPathNameA")) {
        return pe_thunk_address(VEX_PE_GET_FULL_PATH_A);
    }
    if (ascii_iequals(name, "GetFullPathNameW")) {
        return pe_thunk_address(VEX_PE_GET_FULL_PATH_W);
    }
    if (ascii_iequals(name, "GetTempPathA")) {
        return pe_thunk_address(VEX_PE_GET_TEMP_PATH_A);
    }
    if (ascii_iequals(name, "GetTempPathW")) {
        return pe_thunk_address(VEX_PE_GET_TEMP_PATH_W);
    }
    if (ascii_iequals(name, "OutputDebugStringA")) {
        return pe_thunk_address(VEX_PE_OUTPUT_DEBUG_A);
    }
    if (ascii_iequals(name, "OutputDebugStringW")) {
        return pe_thunk_address(VEX_PE_OUTPUT_DEBUG_W);
    }
    if (ascii_iequals(name, "RtlZeroMemory") || ascii_iequals(name, "ZeroMemory")) {
        return pe_thunk_address(VEX_PE_RTL_ZERO_MEMORY);
    }
    if (ascii_iequals(name, "RtlFillMemory") || ascii_iequals(name, "FillMemory")) {
        return pe_thunk_address(VEX_PE_RTL_FILL_MEMORY);
    }
    if (ascii_iequals(name, "RtlMoveMemory") || ascii_iequals(name, "RtlCopyMemory") || ascii_iequals(name, "CopyMemory")) {
        return pe_thunk_address(VEX_PE_RTL_MOVE_MEMORY);
    }
    if (ascii_iequals(name, "RtlCompareMemory")) {
        return pe_thunk_address(VEX_PE_RTL_COMPARE_MEMORY);
    }
    if (ascii_iequals(name, "RtlInitUnicodeString")) {
        return pe_thunk_address(VEX_PE_RTL_INIT_UNICODE_STRING);
    }
    if (ascii_iequals(name, "RtlGetVersion")) {
        return pe_thunk_address(VEX_PE_RTL_GET_VERSION);
    }
    if (ascii_iequals(name, "NtQueryInformationProcess") || ascii_iequals(name, "ZwQueryInformationProcess")) {
        return pe_thunk_address(VEX_PE_NT_QUERY_INFO_PROCESS);
    }
    if (ascii_iequals(name, "NtClose") || ascii_iequals(name, "ZwClose")) {
        return pe_thunk_address(VEX_PE_NT_CLOSE);
    }
    if (ascii_iequals(name, "NtDelayExecution") || ascii_iequals(name, "ZwDelayExecution")) {
        return pe_thunk_address(VEX_PE_NT_DELAY_EXECUTION);
    }
    if (ascii_iequals(name, "CreateEventA") || ascii_iequals(name, "CreateEventW")) {
        return pe_thunk_address(VEX_PE_CREATE_EVENT);
    }
    if (ascii_iequals(name, "SetEvent") || ascii_iequals(name, "PulseEvent")) {
        return pe_thunk_address(VEX_PE_SET_EVENT);
    }
    if (ascii_iequals(name, "ResetEvent")) {
        return pe_thunk_address(VEX_PE_RESET_EVENT);
    }
    if (ascii_iequals(name, "CreateMutexA") || ascii_iequals(name, "CreateMutexW")) {
        return pe_thunk_address(VEX_PE_CREATE_MUTEX);
    }
    if (ascii_iequals(name, "ReleaseMutex")) {
        return pe_thunk_address(VEX_PE_RELEASE_MUTEX);
    }
    if (ascii_iequals(name, "CreateSemaphoreA") || ascii_iequals(name, "CreateSemaphoreW")) {
        return pe_thunk_address(VEX_PE_CREATE_SEMAPHORE);
    }
    if (ascii_iequals(name, "ReleaseSemaphore")) {
        return pe_thunk_address(VEX_PE_RELEASE_SEMAPHORE);
    }
    if (ascii_iequals(name, "WaitForMultipleObjects")) {
        return pe_thunk_address(VEX_PE_WAIT_MULTIPLE);
    }
    if (ascii_iequals(name, "FindFirstFileA")) {
        return pe_thunk_address(VEX_PE_FIND_FIRST_A);
    }
    if (ascii_iequals(name, "FindFirstFileW")) {
        return pe_thunk_address(VEX_PE_FIND_FIRST_W);
    }
    if (ascii_iequals(name, "FindNextFileA") || ascii_iequals(name, "FindNextFileW")) {
        return pe_thunk_address(VEX_PE_FIND_NEXT);
    }
    if (ascii_iequals(name, "FindClose")) {
        return pe_thunk_address(VEX_PE_FIND_CLOSE);
    }
    if (ascii_iequals(name, "InterlockedIncrement")) {
        return pe_thunk_address(VEX_PE_INTERLOCKED_INC);
    }
    if (ascii_iequals(name, "InterlockedDecrement")) {
        return pe_thunk_address(VEX_PE_INTERLOCKED_DEC);
    }
    if (ascii_iequals(name, "InterlockedExchange")) {
        return pe_thunk_address(VEX_PE_INTERLOCKED_EXCHANGE);
    }
    if (ascii_iequals(name, "InterlockedCompareExchange")) {
        return pe_thunk_address(VEX_PE_INTERLOCKED_COMPARE_EXCHANGE);
    }
    if (ascii_iequals(name, "InterlockedExchangeAdd")) {
        return pe_thunk_address(VEX_PE_INTERLOCKED_EXCHANGE_ADD);
    }
    if (ascii_iequals(name, "InterlockedAdd")) {
        return pe_thunk_address(VEX_PE_INTERLOCKED_ADD);
    }

    pe_set_last_error(WIN_ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

uint64_t compat_pe_call(const char *symbol, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    if (symbol == NULL) {
        return compat_linux_error(PE_LINUX_ENOSYS);
    }
    if (ascii_iequals(symbol, "GetStdHandle")) {
        return pe_get_std_handle(arg1);
    }
    if (ascii_iequals(symbol, "WriteFile")) {
        return pe_write_file(arg1, arg2, arg3, arg4);
    }
    if (ascii_iequals(symbol, "ReadFile")) {
        return pe_read_file(arg1, arg2, arg3, arg4);
    }
    if (ascii_iequals(symbol, "GetLastError")) {
        return pe_last_error_value();
    }
    if (ascii_iequals(symbol, "SetLastError")) {
        pe_set_last_error((uint32_t)arg1);
        return 0u;
    }
    if (ascii_iequals(symbol, "VirtualAlloc")) {
        return pe_virtual_alloc(arg1, arg2, arg3, arg4);
    }
    if (ascii_iequals(symbol, "VirtualFree")) {
        return pe_virtual_free(arg1, arg2, arg3);
    }
    if (ascii_iequals(symbol, "HeapAlloc")) {
        return pe_heap_alloc(arg1, arg2, arg3);
    }
    if (ascii_iequals(symbol, "HeapFree")) {
        return pe_heap_free(arg1, arg2, arg3);
    }
    if (ascii_iequals(symbol, "GetCurrentProcess")) {
        return pe_get_current_process();
    }
    if (ascii_iequals(symbol, "GetCurrentThread")) {
        return pe_get_current_thread();
    }
    if (ascii_iequals(symbol, "GetCurrentProcessId")) {
        return pe_get_current_process_id();
    }
    if (ascii_iequals(symbol, "GetCurrentThreadId")) {
        return pe_get_current_thread_id();
    }
    if (ascii_iequals(symbol, "GetProcessHeap")) {
        return pe_get_process_heap();
    }
    if (ascii_iequals(symbol, "HeapCreate")) {
        return pe_heap_create(arg1, arg2, arg3);
    }
    if (ascii_iequals(symbol, "HeapDestroy")) {
        return pe_heap_destroy(arg1);
    }
    if (ascii_iequals(symbol, "Sleep")) {
        return pe_sleep(arg1);
    }
    if (ascii_iequals(symbol, "WaitForSingleObjectEx")) {
        return pe_wait_single_ex(arg1, arg2, arg3);
    }
    if (ascii_iequals(symbol, "GetCommandLineA")) {
        return pe_get_command_line(0);
    }
    if (ascii_iequals(symbol, "GetCommandLineW")) {
        return pe_get_command_line(1);
    }
    if (ascii_iequals(symbol, "GetStartupInfoA")) {
        return pe_get_startup_info(arg1, 0);
    }
    if (ascii_iequals(symbol, "GetStartupInfoW")) {
        return pe_get_startup_info(arg1, 1);
    }
    if (ascii_iequals(symbol, "GetTickCount")) {
        return pe_get_tick_count();
    }
    if (ascii_iequals(symbol, "GetTickCount64")) {
        return pe_get_tick_count64();
    }
    if (ascii_iequals(symbol, "QueryPerformanceCounter")) {
        return pe_query_performance_counter(arg1);
    }
    if (ascii_iequals(symbol, "QueryPerformanceFrequency")) {
        return pe_query_performance_frequency(arg1);
    }
    if (ascii_iequals(symbol, "GetFileType")) {
        return pe_get_file_type(arg1);
    }
    if (ascii_iequals(symbol, "GetConsoleMode")) {
        return pe_get_console_mode(arg1, arg2);
    }
    if (ascii_iequals(symbol, "SetHandleInformation")) {
        return pe_set_handle_information(arg1, arg2, arg3);
    }
    if (ascii_iequals(symbol, "IsDebuggerPresent")) {
        return pe_is_debugger_present();
    }
    if (ascii_iequals(symbol, "GetACP") || ascii_iequals(symbol, "GetOEMCP")) {
        return pe_get_acp();
    }
    if (ascii_iequals(symbol, "TlsAlloc")) {
        return pe_tls_alloc();
    }
    if (ascii_iequals(symbol, "TlsFree")) {
        return pe_tls_free(arg1);
    }
    if (ascii_iequals(symbol, "TlsGetValue")) {
        return pe_tls_get_value(arg1);
    }
    if (ascii_iequals(symbol, "TlsSetValue")) {
        return pe_tls_set_value(arg1, arg2);
    }
    if (ascii_iequals(symbol, "InitializeCriticalSection") ||
        ascii_iequals(symbol, "InitializeCriticalSectionAndSpinCount") ||
        ascii_iequals(symbol, "InitializeCriticalSectionEx") ||
        ascii_iequals(symbol, "EnterCriticalSection") ||
        ascii_iequals(symbol, "LeaveCriticalSection") ||
        ascii_iequals(symbol, "DeleteCriticalSection") ||
        ascii_iequals(symbol, "TryEnterCriticalSection")) {
        return pe_critical_section(arg1, arg2);
    }
    if (ascii_iequals(symbol, "GetSystemTimeAsFileTime")) {
        return pe_get_system_time_as_filetime(arg1);
    }
    if (ascii_iequals(symbol, "GetModuleHandleExA") || ascii_iequals(symbol, "GetModuleHandleExW")) {
        return pe_get_module_handle_ex(arg1, arg2, arg3);
    }
    return compat_linux_error(PE_LINUX_ENOSYS);
}

uint64_t compat_pe_syscall(uint64_t id, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t stack_args) {
    switch (id) {
    case VEX_PE_GET_STD_HANDLE:
        return pe_get_std_handle(arg1);
    case VEX_PE_WRITE_FILE:
        return pe_write_file(arg1, arg2, arg3, arg4);
    case VEX_PE_EXIT_PROCESS:
        (void)arg1;
        process_exit();
        return 0;
    case VEX_PE_READ_FILE:
        return pe_read_file(arg1, arg2, arg3, arg4);
    case VEX_PE_GET_LAST_ERROR:
        return pe_last_error_value();
    case VEX_PE_SET_LAST_ERROR:
        pe_set_last_error((uint32_t)arg1);
        return 0;
    case VEX_PE_CREATE_FILE:
        return pe_create_file(arg1, arg2, arg3, arg4, arg5);
    case VEX_PE_CLOSE_HANDLE:
        if (pe_is_process_handle(arg1) || pe_is_thread_handle(arg1)) {
            pe_set_last_error(WIN_ERROR_SUCCESS);
            return 1u;
        }
        if (pe_is_sync_handle(arg1)) {
            pe_sync_object_t *sync = pe_sync_from_handle(arg1);
            if (sync != NULL) {
                memset(sync, 0, sizeof(*sync));
            }
            pe_set_last_error(WIN_ERROR_SUCCESS);
            return 1u;
        }
        if (pe_is_volume_find_handle(arg1)) {
            return pe_find_volume_close(arg1);
        }
        if (arg1 <= 2u) {
            pe_set_last_error(WIN_ERROR_SUCCESS);
            return 1u;
        }
        if (vex_syscall_dispatch(VEX_SYS_CLOSE, arg1, 0, 0) == 0u) {
            pe_set_last_error(WIN_ERROR_SUCCESS);
            return 1u;
        }
        pe_set_last_error(WIN_ERROR_INVALID_HANDLE);
        return 0u;
    case VEX_PE_VIRTUAL_ALLOC:
        return pe_virtual_alloc(arg1, arg2, arg3, arg4);
    case VEX_PE_VIRTUAL_FREE:
        return pe_virtual_free(arg1, arg2, arg3);
    case VEX_PE_LOCAL_ALLOC:
        return pe_local_alloc(arg1, arg2);
    case VEX_PE_LOCAL_FREE:
        return pe_local_free(arg1);
    case VEX_PE_LOCAL_REALLOC:
        return pe_local_realloc(arg1, arg2, arg3);
    case VEX_PE_WAIT_SINGLE:
        return pe_wait_single(arg1, arg2);
    case VEX_PE_HEAP_ALLOC:
        return pe_heap_alloc(arg1, arg2, arg3);
    case VEX_PE_HEAP_FREE:
        return pe_heap_free(arg1, arg2, arg3);
    case VEX_PE_GET_MODULE:
        return pe_get_module_handle(arg1, pe_pointer_looks_utf16(arg1));
    case VEX_PE_LOAD_LIBRARY:
        return pe_load_library(arg1, pe_pointer_looks_utf16(arg1));
    case VEX_PE_GET_PROC:
        return pe_get_proc(arg1, arg2);
    case VEX_PE_CREATE_PROCESS:
        return pe_create_process(arg1, arg2, pe_stack_arg(stack_args, 1u), pe_stack_arg(stack_args, 5u), pe_pointer_looks_utf16(arg1) || pe_pointer_looks_utf16(arg2));
    case VEX_PE_CREATE_THREAD:
        return pe_create_thread(arg2, arg3, arg4, arg5);
    case VEX_PE_GET_CURRENT_PROCESS:
        return pe_get_current_process();
    case VEX_PE_GET_CURRENT_THREAD:
        return pe_get_current_thread();
    case VEX_PE_GET_CURRENT_PROCESS_ID:
        return pe_get_current_process_id();
    case VEX_PE_GET_CURRENT_THREAD_ID:
        return pe_get_current_thread_id();
    case VEX_PE_GET_PROCESS_HEAP:
        return pe_get_process_heap();
    case VEX_PE_HEAP_CREATE:
        return pe_heap_create(arg1, arg2, arg3);
    case VEX_PE_HEAP_DESTROY:
        return pe_heap_destroy(arg1);
    case VEX_PE_SLEEP:
        return pe_sleep(arg1);
    case VEX_PE_WAIT_SINGLE_EX:
        return pe_wait_single_ex(arg1, arg2, arg3);
    case VEX_PE_GET_COMMAND_LINE_A:
        return pe_get_command_line(0);
    case VEX_PE_GET_COMMAND_LINE_W:
        return pe_get_command_line(1);
    case VEX_PE_GET_STARTUP_INFO_A:
        return pe_get_startup_info(arg1, 0);
    case VEX_PE_GET_STARTUP_INFO_W:
        return pe_get_startup_info(arg1, 1);
    case VEX_PE_GET_TICK_COUNT:
        return pe_get_tick_count();
    case VEX_PE_GET_TICK_COUNT64:
        return pe_get_tick_count64();
    case VEX_PE_QUERY_PERF_COUNTER:
        return pe_query_performance_counter(arg1);
    case VEX_PE_QUERY_PERF_FREQ:
        return pe_query_performance_frequency(arg1);
    case VEX_PE_GET_FILE_TYPE:
        return pe_get_file_type(arg1);
    case VEX_PE_GET_CONSOLE_MODE:
        return pe_get_console_mode(arg1, arg2);
    case VEX_PE_SET_HANDLE_INFO:
        return pe_set_handle_information(arg1, arg2, arg3);
    case VEX_PE_IS_DEBUGGER_PRESENT:
        return pe_is_debugger_present();
    case VEX_PE_GET_ACP:
        return pe_get_acp();
    case VEX_PE_MULTI_BYTE_TO_WIDE:
        return pe_multi_byte_to_wide(arg1, arg2, arg3, arg4, arg5, pe_stack_arg(stack_args, 1u));
    case VEX_PE_WIDE_CHAR_TO_MULTI:
        return pe_wide_char_to_multi(arg1, arg2, arg3, arg4, arg5, pe_stack_arg(stack_args, 1u));
    case VEX_PE_TLS_ALLOC:
        return pe_tls_alloc();
    case VEX_PE_TLS_FREE:
        return pe_tls_free(arg1);
    case VEX_PE_TLS_GET_VALUE:
        return pe_tls_get_value(arg1);
    case VEX_PE_TLS_SET_VALUE:
        return pe_tls_set_value(arg1, arg2);
    case VEX_PE_CRITICAL_SECTION:
        return pe_critical_section(arg1, arg2);
    case VEX_PE_GET_SYSTEM_TIME_AS_FILETIME:
        return pe_get_system_time_as_filetime(arg1);
    case VEX_PE_FILETIME_TO_SYSTEMTIME:
        return pe_filetime_to_systemtime(arg1, arg2);
    case VEX_PE_GET_SYSTEM_TIME:
    case VEX_PE_GET_LOCAL_TIME:
        return pe_get_system_time(arg1);
    case VEX_PE_GET_MODULE_EX:
        return pe_get_module_handle_ex(arg1, arg2, arg3);
    case VEX_PE_GET_ENV_VAR_A:
        return pe_get_environment_variable(arg1, arg2, arg3, 0);
    case VEX_PE_GET_ENV_VAR_W:
        return pe_get_environment_variable(arg1, arg2, arg3, 1);
    case VEX_PE_GET_ENV_STRINGS_A:
        return pe_get_environment_strings(0);
    case VEX_PE_GET_ENV_STRINGS_W:
        return pe_get_environment_strings(1);
    case VEX_PE_FREE_ENV_STRINGS:
        return pe_free_environment_strings(arg1);
    case VEX_PE_EXPAND_ENV_A:
        return pe_expand_environment_strings(arg1, arg2, arg3, 0);
    case VEX_PE_EXPAND_ENV_W:
        return pe_expand_environment_strings(arg1, arg2, arg3, 1);
    case VEX_PE_GET_VERSION:
        return pe_get_version();
    case VEX_PE_GET_VERSION_EX_A:
        return pe_get_version_ex(arg1, 0);
    case VEX_PE_GET_VERSION_EX_W:
        return pe_get_version_ex(arg1, 1);
    case VEX_PE_RTL_GET_VERSION:
        return pe_get_version_ex(arg1, 1) ? 0u : 0xc000000dull;
    case VEX_PE_GET_SYSTEM_INFO:
        return pe_get_system_info(arg1);
    case VEX_PE_GET_FILE_ATTR_A:
        return pe_get_file_attributes(arg1, 0);
    case VEX_PE_GET_FILE_ATTR_W:
        return pe_get_file_attributes(arg1, 1);
    case VEX_PE_SET_CURRENT_DIR_A:
        return pe_set_current_directory(arg1, 0);
    case VEX_PE_SET_CURRENT_DIR_W:
        return pe_set_current_directory(arg1, 1);
    case VEX_PE_GET_CURRENT_DIR_A:
        return pe_get_current_directory(arg1, arg2, 0);
    case VEX_PE_GET_CURRENT_DIR_W:
        return pe_get_current_directory(arg1, arg2, 1);
    case VEX_PE_CREATE_DIR_A:
        return pe_create_directory(arg1, 0);
    case VEX_PE_CREATE_DIR_W:
        return pe_create_directory(arg1, 1);
    case VEX_PE_REMOVE_DIR_A:
        return pe_remove_directory(arg1, 0);
    case VEX_PE_REMOVE_DIR_W:
        return pe_remove_directory(arg1, 1);
    case VEX_PE_DELETE_FILE_A:
        return pe_delete_file(arg1, 0);
    case VEX_PE_DELETE_FILE_W:
        return pe_delete_file(arg1, 1);
    case VEX_PE_MOVE_FILE_A:
        return pe_move_file(arg1, arg2, 0);
    case VEX_PE_MOVE_FILE_W:
        return pe_move_file(arg1, arg2, 1);
    case VEX_PE_GET_FULL_PATH_A:
        return pe_get_full_path_name(arg1, arg2, arg3, arg4, 0);
    case VEX_PE_GET_FULL_PATH_W:
        return pe_get_full_path_name(arg1, arg2, arg3, arg4, 1);
    case VEX_PE_GET_TEMP_PATH_A:
        return pe_get_temp_path(arg1, arg2, 0);
    case VEX_PE_GET_TEMP_PATH_W:
        return pe_get_temp_path(arg1, arg2, 1);
    case VEX_PE_OUTPUT_DEBUG_A:
        return pe_output_debug_string(arg1, 0);
    case VEX_PE_OUTPUT_DEBUG_W:
        return pe_output_debug_string(arg1, 1);
    case VEX_PE_RTL_ZERO_MEMORY:
        return pe_rtl_zero_memory(arg1, arg2);
    case VEX_PE_RTL_FILL_MEMORY:
        return pe_rtl_fill_memory(arg1, arg2, arg3);
    case VEX_PE_RTL_MOVE_MEMORY:
        return pe_rtl_move_memory(arg1, arg2, arg3);
    case VEX_PE_RTL_COMPARE_MEMORY:
        return pe_rtl_compare_memory(arg1, arg2, arg3);
    case VEX_PE_RTL_INIT_UNICODE_STRING:
        return pe_rtl_init_unicode_string(arg1, arg2);
    case VEX_PE_NT_QUERY_INFO_PROCESS:
        return pe_nt_query_information_process(arg1, arg2, arg3, arg4, arg5);
    case VEX_PE_NT_CLOSE:
        return compat_pe_syscall(VEX_PE_CLOSE_HANDLE, arg1, 0, 0, 0, 0, 0);
    case VEX_PE_NT_DELAY_EXECUTION:
        return pe_nt_delay_execution(arg1, arg2);
    case VEX_PE_CREATE_EVENT:
        return pe_create_event(arg2, arg3);
    case VEX_PE_SET_EVENT:
        return pe_set_event(arg1);
    case VEX_PE_RESET_EVENT:
        return pe_reset_event(arg1);
    case VEX_PE_CREATE_MUTEX:
        return pe_create_mutex(arg2);
    case VEX_PE_RELEASE_MUTEX:
        return pe_release_mutex(arg1);
    case VEX_PE_CREATE_SEMAPHORE:
        return pe_create_semaphore(arg2, arg3);
    case VEX_PE_RELEASE_SEMAPHORE:
        return pe_release_semaphore(arg1, arg2, arg3);
    case VEX_PE_WAIT_MULTIPLE:
        return pe_wait_multiple(arg1, arg2, arg3, arg4);
    case VEX_PE_FIND_FIRST_A:
        return pe_find_first_file(arg1, arg2, 0);
    case VEX_PE_FIND_FIRST_W:
        return pe_find_first_file(arg1, arg2, 1);
    case VEX_PE_FIND_NEXT:
        return pe_find_next_file(arg1, arg2);
    case VEX_PE_FIND_CLOSE:
        return pe_find_close(arg1);
    case VEX_PE_INTERLOCKED_INC:
        return pe_interlocked_increment(arg1);
    case VEX_PE_INTERLOCKED_DEC:
        return pe_interlocked_decrement(arg1);
    case VEX_PE_INTERLOCKED_EXCHANGE:
        return pe_interlocked_exchange(arg1, arg2);
    case VEX_PE_INTERLOCKED_COMPARE_EXCHANGE:
        return pe_interlocked_compare_exchange(arg1, arg2, arg3);
    case VEX_PE_INTERLOCKED_EXCHANGE_ADD:
        return pe_interlocked_exchange_add(arg1, arg2);
    case VEX_PE_INTERLOCKED_ADD:
        return pe_interlocked_add(arg1, arg2);
    case VEX_PE_NOOP_SUCCESS:
        return pe_noop_success();
    case VEX_PE_NOOP_FALSE:
        return pe_noop_false();
    case VEX_PE_NOOP_ZERO:
        return pe_noop_zero();
    case VEX_PE_GET_MODULE_FILE_A:
        return pe_get_module_filename(arg1, arg2, arg3, 0);
    case VEX_PE_GET_MODULE_FILE_W:
        return pe_get_module_filename(arg1, arg2, arg3, 1);
    case VEX_PE_GET_COMPUTER_NAME_A:
        return pe_get_computer_name(arg1, arg2, 0);
    case VEX_PE_GET_COMPUTER_NAME_W:
        return pe_get_computer_name(arg1, arg2, 1);
    case VEX_PE_GET_USER_NAME_A:
        return pe_get_user_name(arg1, arg2, 0);
    case VEX_PE_GET_USER_NAME_W:
        return pe_get_user_name(arg1, arg2, 1);
    case VEX_PE_GET_PROCESS_AFFINITY_MASK:
        return pe_get_process_affinity_mask(arg1, arg2, arg3);
    case VEX_PE_GET_EXIT_CODE_PROCESS:
    case VEX_PE_GET_EXIT_CODE_THREAD:
        return pe_get_exit_code(arg1, arg2);
    case VEX_PE_QUERY_PERF_COUNTER_PRECISE:
        return pe_query_performance_counter(arg1);
    case VEX_PE_GET_SYSTEM_DIRECTORY_A:
        return pe_get_system_directory(arg1, arg2, 0);
    case VEX_PE_GET_SYSTEM_DIRECTORY_W:
        return pe_get_system_directory(arg1, arg2, 1);
    case VEX_PE_GET_WINDOWS_DIRECTORY_A:
        return pe_get_windows_directory(arg1, arg2, 0);
    case VEX_PE_GET_WINDOWS_DIRECTORY_W:
        return pe_get_windows_directory(arg1, arg2, 1);
    case VEX_PE_GET_LOGICAL_DRIVES:
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return 1u << 2u;
    case VEX_PE_GET_DISK_FREE_SPACE_EX:
        return pe_get_disk_free_space_ex(arg1, arg2, arg3, arg4);
    case VEX_PE_SET_FILE_POINTER:
        return pe_set_file_pointer(arg1, arg2, arg3, arg4);
    case VEX_PE_SET_FILE_POINTER_EX:
        return pe_set_file_pointer_ex(arg1, arg2, arg3, arg4);
    case VEX_PE_GET_FILE_SIZE:
        return pe_get_file_size(arg1, arg2);
    case VEX_PE_GET_FILE_SIZE_EX:
        return pe_get_file_size_ex(arg1, arg2);
    case VEX_PE_GET_CONSOLE_SCREEN_BUFFER_INFO:
        return pe_get_console_screen_buffer_info(arg1, arg2);
    case VEX_PE_LSTRLEN_A:
        return pe_lstrlen(arg1, 0);
    case VEX_PE_LSTRLEN_W:
        return pe_lstrlen(arg1, 1);
    case VEX_PE_LSTRCMP_A:
        return pe_lstrcmp(arg1, arg2, 0);
    case VEX_PE_LSTRCMP_W:
        return pe_lstrcmp(arg1, arg2, 1);
    case VEX_PE_LSTRCPY_A:
        return pe_lstrcpy(arg1, arg2, 0);
    case VEX_PE_LSTRCPY_W:
        return pe_lstrcpy(arg1, arg2, 1);
    case VEX_PE_LSTRCPYN_A:
        return pe_lstrcpyn(arg1, arg2, arg3, 0);
    case VEX_PE_LSTRCPYN_W:
        return pe_lstrcpyn(arg1, arg2, arg3, 1);
    case VEX_PE_FIND_FIRST_VOLUME_A:
        return pe_find_first_volume(arg1, arg2, 0);
    case VEX_PE_FIND_FIRST_VOLUME_W:
        return pe_find_first_volume(arg1, arg2, 1);
    case VEX_PE_FIND_NEXT_VOLUME_A:
        return pe_find_next_volume(arg1, arg2, arg3, 0);
    case VEX_PE_FIND_NEXT_VOLUME_W:
        return pe_find_next_volume(arg1, arg2, arg3, 1);
    case VEX_PE_FIND_VOLUME_CLOSE:
        return pe_find_volume_close(arg1);
    case VEX_PE_GET_LOGICAL_DRIVE_STRINGS_A:
        return pe_get_logical_drive_strings(arg1, arg2, 0);
    case VEX_PE_GET_LOGICAL_DRIVE_STRINGS_W:
        return pe_get_logical_drive_strings(arg1, arg2, 1);
    case VEX_PE_GET_DRIVE_TYPE_A:
        return pe_get_drive_type(arg1, 0);
    case VEX_PE_GET_DRIVE_TYPE_W:
        return pe_get_drive_type(arg1, 1);
    case VEX_PE_GET_VOLUME_INFORMATION_A:
        return pe_get_volume_information(arg1, arg2, arg3, arg4, arg5, pe_stack_arg(stack_args, 1u), pe_stack_arg(stack_args, 2u), pe_stack_arg(stack_args, 3u), 0);
    case VEX_PE_GET_VOLUME_INFORMATION_W:
        return pe_get_volume_information(arg1, arg2, arg3, arg4, arg5, pe_stack_arg(stack_args, 1u), pe_stack_arg(stack_args, 2u), pe_stack_arg(stack_args, 3u), 1);
    case VEX_PE_GET_VOLUME_PATH_NAME_A:
        return pe_get_volume_path_name(arg1, arg2, arg3, 0);
    case VEX_PE_GET_VOLUME_PATH_NAME_W:
        return pe_get_volume_path_name(arg1, arg2, arg3, 1);
    default:
        pe_set_last_error(WIN_ERROR_CALL_NOT_IMPLEMENTED);
        return 0;
    }
}
