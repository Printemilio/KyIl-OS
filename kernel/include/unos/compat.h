#ifndef UNOS_COMPAT_H
#define UNOS_COMPAT_H

#include <stdint.h>
int vex_fd_sync_all(void);
uint64_t vex_fd_close_failures(void);
uint32_t vex_fd_live_count(void);

/* Internal representation used by the Linux timerfd compatibility layer.
 * This is not part of the native Vex syscall ABI. */
typedef struct vex_linux_itimerspec {
    int64_t interval_sec;
    int64_t interval_nsec;
    int64_t value_sec;
    int64_t value_nsec;
} vex_linux_itimerspec_t;

/* Linux x86-64 struct flock. Keep this boundary explicit: fcntl lock
 * commands pass a userspace pointer, unlike scalar descriptor commands. */
typedef struct vex_linux_flock {
    int16_t type;
    int16_t whence;
    int64_t start;
    int64_t length;
    int32_t pid;
    int32_t reserved;
} vex_linux_flock_t;

struct interrupt_frame;
struct vex_sync_fault;

#define VEX_ABI_NATIVE 0u
#define VEX_ABI_LINUX  1u
#include <stddef.h>
size_t compat_linux_async_context_size(void);
void compat_linux_build_async_context(void *destination,const uint64_t *syscall_frame,uint64_t result,uint64_t signal);
#define VEX_ABI_PE     2u

#define VEX_PE_SYSCALL_CALL 0x766578504543414cull
#define VEX_PE_THUNK_BASE 0x0000007000000000ull
#define VEX_PE_THUNK_SIZE 64ull

#define VEX_PE_GET_STD_HANDLE 1ull
#define VEX_PE_WRITE_FILE     2ull
#define VEX_PE_EXIT_PROCESS   3ull
#define VEX_PE_READ_FILE      4ull
#define VEX_PE_GET_LAST_ERROR 5ull
#define VEX_PE_SET_LAST_ERROR 6ull
#define VEX_PE_CREATE_FILE    7ull
#define VEX_PE_CLOSE_HANDLE   8ull
#define VEX_PE_VIRTUAL_ALLOC  9ull
#define VEX_PE_VIRTUAL_FREE   10ull
#define VEX_PE_CREATE_PROCESS 11ull
#define VEX_PE_WAIT_SINGLE    12ull
#define VEX_PE_CREATE_THREAD  13ull
#define VEX_PE_HEAP_ALLOC     14ull
#define VEX_PE_HEAP_FREE      15ull
#define VEX_PE_GET_MODULE     16ull
#define VEX_PE_LOAD_LIBRARY   17ull
#define VEX_PE_GET_PROC       18ull
#define VEX_PE_GET_CURRENT_PROCESS 19ull
#define VEX_PE_GET_CURRENT_THREAD  20ull
#define VEX_PE_GET_CURRENT_PROCESS_ID 21ull
#define VEX_PE_GET_CURRENT_THREAD_ID  22ull
#define VEX_PE_GET_PROCESS_HEAP   23ull
#define VEX_PE_HEAP_CREATE        24ull
#define VEX_PE_HEAP_DESTROY       25ull
#define VEX_PE_SLEEP              26ull
#define VEX_PE_WAIT_SINGLE_EX     27ull
#define VEX_PE_GET_COMMAND_LINE_A 28ull
#define VEX_PE_GET_COMMAND_LINE_W 29ull
#define VEX_PE_GET_STARTUP_INFO_A 30ull
#define VEX_PE_GET_STARTUP_INFO_W 31ull
#define VEX_PE_GET_TICK_COUNT     32ull
#define VEX_PE_GET_TICK_COUNT64   33ull
#define VEX_PE_QUERY_PERF_COUNTER 34ull
#define VEX_PE_QUERY_PERF_FREQ    35ull
#define VEX_PE_GET_FILE_TYPE      36ull
#define VEX_PE_GET_CONSOLE_MODE   37ull
#define VEX_PE_SET_HANDLE_INFO    38ull
#define VEX_PE_IS_DEBUGGER_PRESENT 39ull
#define VEX_PE_GET_ACP            40ull
#define VEX_PE_MULTI_BYTE_TO_WIDE 41ull
#define VEX_PE_WIDE_CHAR_TO_MULTI 42ull
#define VEX_PE_TLS_ALLOC          43ull
#define VEX_PE_TLS_FREE           44ull
#define VEX_PE_TLS_GET_VALUE      45ull
#define VEX_PE_TLS_SET_VALUE      46ull
#define VEX_PE_CRITICAL_SECTION   47ull
#define VEX_PE_GET_SYSTEM_TIME_AS_FILETIME 48ull
#define VEX_PE_GET_MODULE_EX      49ull
#define VEX_PE_GET_ENV_VAR_A      50ull
#define VEX_PE_GET_ENV_VAR_W      51ull
#define VEX_PE_GET_ENV_STRINGS_A  52ull
#define VEX_PE_GET_ENV_STRINGS_W  53ull
#define VEX_PE_FREE_ENV_STRINGS   54ull
#define VEX_PE_EXPAND_ENV_A       55ull
#define VEX_PE_EXPAND_ENV_W       56ull
#define VEX_PE_GET_VERSION        57ull
#define VEX_PE_GET_VERSION_EX_A   58ull
#define VEX_PE_GET_VERSION_EX_W   59ull
#define VEX_PE_GET_SYSTEM_INFO    60ull
#define VEX_PE_GET_FILE_ATTR_A    61ull
#define VEX_PE_GET_FILE_ATTR_W    62ull
#define VEX_PE_SET_CURRENT_DIR_A  63ull
#define VEX_PE_SET_CURRENT_DIR_W  64ull
#define VEX_PE_GET_CURRENT_DIR_A  65ull
#define VEX_PE_GET_CURRENT_DIR_W  66ull
#define VEX_PE_CREATE_DIR_A       67ull
#define VEX_PE_CREATE_DIR_W       68ull
#define VEX_PE_REMOVE_DIR_A       69ull
#define VEX_PE_REMOVE_DIR_W       70ull
#define VEX_PE_DELETE_FILE_A      71ull
#define VEX_PE_DELETE_FILE_W      72ull
#define VEX_PE_MOVE_FILE_A        73ull
#define VEX_PE_MOVE_FILE_W        74ull
#define VEX_PE_GET_FULL_PATH_A    75ull
#define VEX_PE_GET_FULL_PATH_W    76ull
#define VEX_PE_GET_TEMP_PATH_A    77ull
#define VEX_PE_GET_TEMP_PATH_W    78ull
#define VEX_PE_OUTPUT_DEBUG_A     79ull
#define VEX_PE_OUTPUT_DEBUG_W     80ull
#define VEX_PE_RTL_ZERO_MEMORY    81ull
#define VEX_PE_RTL_FILL_MEMORY    82ull
#define VEX_PE_RTL_MOVE_MEMORY    83ull
#define VEX_PE_RTL_COMPARE_MEMORY 84ull
#define VEX_PE_RTL_INIT_UNICODE_STRING 85ull
#define VEX_PE_RTL_GET_VERSION    86ull
#define VEX_PE_NT_QUERY_INFO_PROCESS 87ull
#define VEX_PE_NT_CLOSE           88ull
#define VEX_PE_NT_DELAY_EXECUTION 89ull
#define VEX_PE_CREATE_EVENT       90ull
#define VEX_PE_SET_EVENT          91ull
#define VEX_PE_RESET_EVENT        92ull
#define VEX_PE_CREATE_MUTEX       93ull
#define VEX_PE_RELEASE_MUTEX      94ull
#define VEX_PE_CREATE_SEMAPHORE   95ull
#define VEX_PE_RELEASE_SEMAPHORE  96ull
#define VEX_PE_WAIT_MULTIPLE      97ull
#define VEX_PE_FIND_FIRST_A       98ull
#define VEX_PE_FIND_FIRST_W       99ull
#define VEX_PE_FIND_NEXT          100ull
#define VEX_PE_FIND_CLOSE         101ull
#define VEX_PE_INTERLOCKED_INC    102ull
#define VEX_PE_INTERLOCKED_DEC    103ull
#define VEX_PE_INTERLOCKED_EXCHANGE 104ull
#define VEX_PE_INTERLOCKED_COMPARE_EXCHANGE 105ull
#define VEX_PE_INTERLOCKED_EXCHANGE_ADD 106ull
#define VEX_PE_INTERLOCKED_ADD    107ull
#define VEX_PE_NOOP_SUCCESS       108ull
#define VEX_PE_NOOP_FALSE         109ull
#define VEX_PE_NOOP_ZERO          110ull
#define VEX_PE_GET_MODULE_FILE_A  111ull
#define VEX_PE_GET_MODULE_FILE_W  112ull
#define VEX_PE_GET_COMPUTER_NAME_A 113ull
#define VEX_PE_GET_COMPUTER_NAME_W 114ull
#define VEX_PE_GET_USER_NAME_A    115ull
#define VEX_PE_GET_USER_NAME_W    116ull
#define VEX_PE_GET_PROCESS_AFFINITY_MASK 117ull
#define VEX_PE_GET_EXIT_CODE_PROCESS 118ull
#define VEX_PE_GET_EXIT_CODE_THREAD 119ull
#define VEX_PE_QUERY_PERF_COUNTER_PRECISE 120ull
#define VEX_PE_GET_SYSTEM_DIRECTORY_A 121ull
#define VEX_PE_GET_SYSTEM_DIRECTORY_W 122ull
#define VEX_PE_GET_WINDOWS_DIRECTORY_A 123ull
#define VEX_PE_GET_WINDOWS_DIRECTORY_W 124ull
#define VEX_PE_GET_LOGICAL_DRIVES 125ull
#define VEX_PE_GET_DISK_FREE_SPACE_EX 126ull
#define VEX_PE_FIND_FIRST_VOLUME_A 127ull
#define VEX_PE_FIND_FIRST_VOLUME_W 128ull
#define VEX_PE_FIND_NEXT_VOLUME_A 129ull
#define VEX_PE_FIND_NEXT_VOLUME_W 130ull
#define VEX_PE_FIND_VOLUME_CLOSE  131ull
#define VEX_PE_GET_LOGICAL_DRIVE_STRINGS_A 132ull
#define VEX_PE_GET_LOGICAL_DRIVE_STRINGS_W 133ull
#define VEX_PE_GET_DRIVE_TYPE_A   134ull
#define VEX_PE_GET_DRIVE_TYPE_W   135ull
#define VEX_PE_GET_VOLUME_INFORMATION_A 136ull
#define VEX_PE_GET_VOLUME_INFORMATION_W 137ull
#define VEX_PE_GET_VOLUME_PATH_NAME_A 138ull
#define VEX_PE_GET_VOLUME_PATH_NAME_W 139ull
#define VEX_PE_LOCAL_ALLOC      140ull
#define VEX_PE_LOCAL_FREE       141ull
#define VEX_PE_LOCAL_REALLOC    142ull
#define VEX_PE_SET_FILE_POINTER 143ull
#define VEX_PE_SET_FILE_POINTER_EX 144ull
#define VEX_PE_GET_FILE_SIZE    145ull
#define VEX_PE_GET_FILE_SIZE_EX 146ull
#define VEX_PE_GET_CONSOLE_SCREEN_BUFFER_INFO 147ull
#define VEX_PE_FILETIME_TO_SYSTEMTIME 148ull
#define VEX_PE_GET_SYSTEM_TIME  149ull
#define VEX_PE_GET_LOCAL_TIME   150ull
#define VEX_PE_LSTRLEN_A        151ull
#define VEX_PE_LSTRLEN_W        152ull
#define VEX_PE_LSTRCMP_A        153ull
#define VEX_PE_LSTRCMP_W        154ull
#define VEX_PE_LSTRCPY_A        155ull
#define VEX_PE_LSTRCPY_W        156ull
#define VEX_PE_LSTRCPYN_A       157ull
#define VEX_PE_LSTRCPYN_W       158ull

#define VEX_PE_LAST_THUNK_ID VEX_PE_LSTRCPYN_W

#define VEX_PE_STATE_KEY 0x7665785045535441ull
#define VEX_PE_THREAD_BOOTSTRAP_SLOT 191ull
#define VEX_PE_THREAD_BOOTSTRAP_ADDR (VEX_PE_THUNK_BASE + VEX_PE_THREAD_BOOTSTRAP_SLOT * VEX_PE_THUNK_SIZE)

uint64_t compat_linux_syscall(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);
int compat_linux_deliver_sync_fault(struct interrupt_frame *frame,
                                    const struct vex_sync_fault *fault);
int vex_web_timerfd_create(int clock_id, uint32_t flags);
int vex_web_timerfd_settime(int fd, uint32_t flags,
                            const vex_linux_itimerspec_t *new_value,
                            vex_linux_itimerspec_t *old_value);
int vex_web_timerfd_gettime(int fd, vex_linux_itimerspec_t *current_value);
int vex_linux_fcntl_lock(int fd, int command, vex_linux_flock_t *user_lock);
int vex_fd_group_range(int fd,unsigned mode,uint64_t offset,uint64_t length,int wait);
int vex_fd_description_range(int fd,unsigned mode,uint64_t offset,uint64_t length,int wait);
void vex_file_range_thread_stopped(uint32_t tid);

#endif
