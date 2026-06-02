#include <unos/compat.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/scheduler.h>
#include <unos/vex_abi.h>
#include <unos/vex_syscall.h>

#define PE_LINUX_ENOSYS 38ull

#define WIN_ERROR_SUCCESS 0u
#define WIN_ERROR_FILE_NOT_FOUND 2u
#define WIN_ERROR_PATH_NOT_FOUND 3u
#define WIN_ERROR_INVALID_HANDLE 6u
#define WIN_ERROR_NOT_ENOUGH_MEMORY 8u
#define WIN_ERROR_INVALID_PARAMETER 87u
#define WIN_ERROR_CALL_NOT_IMPLEMENTED 120u
#define WIN_WAIT_OBJECT_0 0u

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

#define WIN_STD_INPUT_HANDLE  0xfffffff6u
#define WIN_STD_OUTPUT_HANDLE 0xfffffff5u
#define WIN_STD_ERROR_HANDLE  0xfffffff4u

typedef struct {
    uint8_t used;
    uint32_t pid;
    uint32_t last_error;
} pe_process_state_t;

#define PE_PROCESS_STATE_CAPACITY 64u
static pe_process_state_t pe_states[PE_PROCESS_STATE_CAPACITY];
static uint32_t pe_last_error_fallback;

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

uint64_t compat_pe_call(const char *symbol, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    (void)symbol;
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    return compat_linux_error(PE_LINUX_ENOSYS);
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

static uint32_t current_pid_or_zero(void) {
    process_t *process = scheduler_current_process();
    return process == NULL ? 0u : process->pid;
}

static pe_process_state_t *pe_state(void) {
    const uint32_t pid = current_pid_or_zero();

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

static uint64_t pe_create_file(uint64_t path, uint64_t desired_access, uint64_t share_mode, uint64_t security, uint64_t creation_disposition) {
    (void)share_mode;
    (void)security;

    const char *file_path = (const char *)(uintptr_t)path;
    uint64_t flags;

    if (file_path == NULL || file_path[0] == '\0') {
        pe_set_last_error(WIN_ERROR_PATH_NOT_FOUND);
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
        const uint64_t fd = vex_syscall_dispatch(VEX_SYS_OPEN, path, flags, 0);
        if ((int64_t)fd < 0) {
            pe_set_last_error(creation_disposition == WIN_OPEN_EXISTING ? WIN_ERROR_FILE_NOT_FOUND : WIN_ERROR_INVALID_HANDLE);
            return WIN_INVALID_HANDLE_VALUE;
        }
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return fd;
    }
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

static uint64_t pe_get_proc(uint64_t module, uint64_t name_ptr) {
    (void)module;

    const char *name = (const char *)(uintptr_t)name_ptr;
    if (name == NULL) {
        return 0;
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

    pe_set_last_error(WIN_ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}

uint64_t compat_pe_syscall(uint64_t id, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg6;

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
    case VEX_PE_WAIT_SINGLE:
        (void)arg1;
        (void)arg2;
        return WIN_WAIT_OBJECT_0;
    case VEX_PE_HEAP_ALLOC:
        return pe_heap_alloc(arg1, arg2, arg3);
    case VEX_PE_HEAP_FREE:
        return pe_heap_free(arg1, arg2, arg3);
    case VEX_PE_GET_MODULE:
    case VEX_PE_LOAD_LIBRARY:
        pe_set_last_error(WIN_ERROR_SUCCESS);
        return WIN_MODULE_KERNEL32;
    case VEX_PE_GET_PROC:
        return pe_get_proc(arg1, arg2);
    case VEX_PE_CREATE_PROCESS:
    case VEX_PE_CREATE_THREAD:
        pe_set_last_error(WIN_ERROR_CALL_NOT_IMPLEMENTED);
        return 0;
    default:
        pe_set_last_error(WIN_ERROR_CALL_NOT_IMPLEMENTED);
        return 0;
    }
}
