#ifndef UNOS_VEX_ABI_H
#define UNOS_VEX_ABI_H

#include <stddef.h>
#include <stdint.h>

#include <unos/security.h>
#include <unos/vex_jit_abi.h>
#include <unos/vex_memory_abi.h>
#include <unos/vex_memory_view_abi.h>
#include <unos/vex_thread_abi.h>
#include <unos/vex_thread_control_abi.h>
#include <unos/vex_file_abi.h>
#include <unos/vex_file_range_abi.h>
#include <unos/vex_fs_root_abi.h>
#include <unos/vex_execution_abi.h>
#include <unos/vex_rendezvous_abi.h>
#include <unos/vex_process_abi.h>
#include <unos/vex_event_abi.h>
#include <unos/vex_net_abi.h>

/* Hosted personalities use their libc's POSIX ABI. Native structured
 * requests must not inject the legacy libc declarations into that ABI. */
#ifndef VEX_HOSTED_BUILD
#include <unos/vex_web_compat.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <poll.h>
#endif

#define VEX_SYS_BULK                1ull

#define VEX_SYS_OPEN               4ull
#define VEX_SYS_CLOSE              5ull
#define VEX_SYS_PREAD              6ull
#define VEX_SYS_PWRITE             7ull
#define VEX_SYS_LSEEK              8ull
#define VEX_SYS_FSYNC              9ull
#define VEX_SYS_STAT              10ull
#define VEX_SYS_LSTAT             11ull
#define VEX_SYS_FSTAT             12ull
#define VEX_SYS_ACCESS            13ull
#define VEX_SYS_UNLINK            14ull
#define VEX_SYS_RENAME            15ull
#define VEX_SYS_MKDIR             16ull
#define VEX_SYS_RMDIR             17ull
#define VEX_SYS_CHDIR             18ull
#define VEX_SYS_GETCWD            19ull
#define VEX_SYS_GETPID            20ull
#define VEX_SYS_GETPPID           21ull
#define VEX_SYS_GETUID            22ull
#define VEX_SYS_GETEUID           23ull
#define VEX_SYS_ISATTY            24ull
#define VEX_SYS_IOCTL             25ull
#define VEX_SYS_FCNTL             26ull
#define VEX_SYS_DUP               27ull
#define VEX_SYS_DUP2              28ull
#define VEX_SYS_PIPE              29ull
#define VEX_SYS_POLL              34ull
#define VEX_SYS_GETTIMEOFDAY      35ull
#define VEX_SYS_CLOCK_GETTIME     36ull
#define VEX_SYS_NANOSLEEP         37ull
#define VEX_SYS_SLEEP             38ull
#define VEX_SYS_USLEEP            39ull
#define VEX_SYS_FTRUNCATE         40ull
#define VEX_SYS_CHMOD             41ull
#define VEX_SYS_READLINK          42ull
#define VEX_SYS_SYMLINK           43ull
#define VEX_SYS_REALPATH          44ull
#define VEX_SYS_GETENV            45ull
#define VEX_SYS_SETENV            46ull
#define VEX_SYS_UNSETENV          47ull
#define VEX_SYS_SYSCONF           48ull
#define VEX_SYS_UNAME             49ull
#define VEX_SYS_GETRANDOM         50ull
#define VEX_SYS_SOCKET            60ull
#define VEX_SYS_CONNECT           61ull
#define VEX_SYS_SEND              62ull
#define VEX_SYS_RECV              63ull
#define VEX_SYS_CONNECT_HOSTNAME  64ull
#define VEX_SYS_BIND              65ull
#define VEX_SYS_LISTEN            66ull
#define VEX_SYS_ACCEPT            67ull
#define VEX_SYS_SETSOCKOPT        68ull
#define VEX_SYS_GETSOCKOPT        69ull
#define VEX_SYS_SOCKETPAIR        70ull
#define VEX_SYS_DISPLAY_OPEN      80ull
#define VEX_SYS_DISPLAY_RESIZE    81ull
#define VEX_SYS_DISPLAY_PRESENT   82ull
#define VEX_SYS_DISPLAY_CLOSE     83ull
#define VEX_SYS_READFD            84ull
#define VEX_SYS_WRITEFD           85ull
#define VEX_SYS_HEAP_ALLOC        86ull
#define VEX_SYS_HEAP_FREE         87ull
#define VEX_SYS_HEAP_REALLOC      88ull
#define VEX_SYS_BROWSER_COMMAND_READ 89ull
#define VEX_SYS_DEBUG_LOG         90ull
#define VEX_SYS_DEBUG_LOG_READ    91ull
#define VEX_SYS_BROWSER_RESULTS   92ull
#define VEX_SYS_MOUSE_POLL        93ull
#define VEX_SYS_OPENAT            94ull
#define VEX_SYS_DIRECTORY_LIST    95ull
#define VEX_SYS_OPEN_PATH         96ull
#define VEX_SYS_OPEN_CONTEXT      97ull
#define VEX_SYS_BROWSER_VIEWPORT  98ull

/* Native content viewport: the window/chrome and screen coordinates remain
 * owned by Vex. Consumers receive local coordinates and explicit transitions. */
#define VEX_VIEWPORT_QUERY 0u
#define VEX_VIEWPORT_POLL 1u
#define VEX_VIEWPORT_PRESENT 2u
#define VEX_VIEWPORT_STATE 3u
#define VEX_VIEWPORT_DOWNLOAD 4u /* Report one download's state to the chrome. */
#define VEX_VIEWPORT_CONFIGURE 1u
#define VEX_VIEWPORT_MOTION 2u
#define VEX_VIEWPORT_BUTTON 3u
#define VEX_VIEWPORT_WHEEL 4u
#define VEX_VIEWPORT_KEY 5u
#define VEX_VIEWPORT_FOCUS 6u
#define VEX_VIEWPORT_ENTER 7u
#define VEX_VIEWPORT_LEAVE 8u
typedef struct vex_viewport_event {
    uint32_t type,time,modifiers,pressed;
    int32_t x,y,value;
    uint32_t key,hardware,width,height;
} vex_viewport_event_t;
typedef struct vex_viewport_req {
    uint32_t operation,reserved;
    uint64_t surface;
    vex_viewport_event_t event;
    uint32_t tab_id;
    uint32_t padding;
    const struct vex_browser_state *state;
    const struct vex_download_state *download; /* VEX_VIEWPORT_DOWNLOAD only */
} vex_viewport_req_t;
#define VEX_SYS_PROCESS_SPAWN    100ull
#define VEX_SYS_PROCESS_WAIT     101ull
#define VEX_SYS_PROCESS_KILL     102ull
#define VEX_SYS_PROCESS_EXIT     103ull
#define VEX_SYS_PROCESS_YIELD    104ull
#define VEX_SYS_PROCESS_FORK     105ull
/* posix_spawn: a child that runs on the given stack inside the caller's
 * memory until it executes or exits (its descriptors are a copy). */
#define VEX_SYS_PROCESS_SPAWN_SHARED 219ull
#define VEX_SYS_CONSOLE             220ull
#define VEX_CONSOLE_ATTACH 1u
#define VEX_CONSOLE_READ   2u
#define VEX_CONSOLE_SUBMIT 3u
#define VEX_CONSOLE_BUSY   1u
#define VEX_CONSOLE_OVERRUN 2u
typedef struct vex_console_req {
    uint32_t operation,window_id;
    uint64_t token;
    char *buffer;
    size_t size;
    uint32_t flags,reserved;
} vex_console_req_t;
#define VEX_SYS_PROCESS_EXEC     106ull
#define VEX_SYS_THREAD_CREATE    110ull
#define VEX_SYS_THREAD_JOIN      111ull
#define VEX_SYS_THREAD_EXIT      112ull
#define VEX_SYS_THREAD_YIELD     113ull
#define VEX_SYS_MRAC_PUT         120ull
#define VEX_SYS_MRAC_GET         121ull
#define VEX_SYS_MRAC_REMOVE      122ull
#define VEX_SYS_MRAC_CURRENT_VERSION 123ull
#define VEX_SYS_MRAC_VERSION_COUNT   124ull
#define VEX_SYS_MRAC_VERSION_INFO    125ull
#define VEX_SYS_GPU_QUERY_INFO      130ull
#define VEX_SYS_GPU_BUFFER_CREATE   131ull
#define VEX_SYS_GPU_BUFFER_DESTROY  132ull
#define VEX_SYS_GPU_QUEUE_SUBMIT    133ull
#define VEX_SYS_GPU_PRESENT         134ull
#define VEX_SYS_GPU_SURFACE_CREATE  135ull
#define VEX_SYS_GPU_SURFACE_DESTROY 136ull
#define VEX_SYS_GPU_SURFACE_PRESENT 137ull
#define VEX_SYS_GPU_QUERY_CAPS      138ull
#define VEX_SYS_GPU_SURFACE_WAIT    139ull
#define VEX_SYS_MESSAGE_PAIR        180ull
#define VEX_SYS_MESSAGE_SEND        181ull
#define VEX_SYS_MESSAGE_RECV        182ull
#define VEX_SYS_MESSAGE_OBSERVE     232ull
#define VEX_SYS_MESSAGE_RECV_AUTH   233ull
/* Atomic, non-consuming snapshot of an endpoint's incoming queue. Prefix
 * copies payload only; attached capabilities remain owned by the message. */
typedef struct vex_message_observe_req {
    int32_t fd;
    uint32_t reserved;
    void *prefix;
    uint64_t capacity, messages, bytes, first_length, copied;
    uint32_t peer_closed, reserved_out;
} vex_message_observe_req_t;
#define VEX_SYS_SURFACE_MAP         183ull
#define VEX_SYS_ELEVATION_REQUEST   140ull
#define VEX_SYS_ELEVATION_STATUS    141ull
#define VEX_SYS_APP_WINDOW_OPEN     150ull
#define VEX_SYS_APP_WINDOW_PRESENT  151ull
#define VEX_SYS_APP_WINDOW_POLL     152ull
#define VEX_SYS_APP_WINDOW_CLOSE    153ull
#define VEX_SYS_APP_WINDOW_RESIZE   154ull
#define VEX_SYS_SHM_CREATE          160ull
#define VEX_SYS_SHM_OPEN            161ull
#define VEX_SYS_SHM_CLOSE           162ull
#define VEX_SYS_SHM_MAP             163ull
#define VEX_SYS_SURFACE_RETAIN      184ull
#define VEX_SYS_SURFACE_QUERY       185ull
#define VEX_SYS_SURFACE_UNMAP       186ull
#define VEX_SYS_GPU_QUEUE_CREATE     187ull
#define VEX_SYS_GPU_QUEUE_DESTROY    188ull
#define VEX_SYS_GPU_SUBMIT_BEGIN     189ull
#define VEX_SYS_GPU_SUBMIT_COMPLETE  190ull
#define VEX_SYS_GPU_FENCE_QUERY      191ull
#define VEX_SYS_GPU_FENCE_WAIT       192ull
#define VEX_SYS_EVENT_COUNTER_CREATE 193ull
#define VEX_SYS_EVENT_COUNTER_SIGNAL 194ull
#define VEX_SYS_EVENT_COUNTER_READ   195ull
#define VEX_SYS_EVENT_COUNTER_RELEASE 196ull
#define VEX_SYS_EVENT_REGISTER     170ull
#define VEX_SYS_EVENT_MASK         171ull
#define VEX_SYS_EVENT_POST         172ull
#define VEX_SYS_EVENT_RETURN       173ull
/* Reserved Linux-ABI extension numbers.  A Linux process cannot issue the
 * native VEX numbers directly because 160..163 are assigned Linux syscalls;
 * these explicit values bridge only the native SHM capability operations. */
#define VEX_SYS_LINUX_SHM_OPEN     0x4001ull
#define VEX_SYS_LINUX_SHM_CLOSE    0x4002ull
#define VEX_SYS_LINUX_SHM_MAP      0x4003ull

#define VEX_PROCESS_WAIT_NOHANG 0x1u
#define VEX_PROCESS_SPAWN_CAPABILITIES 0x1u
#define VEX_PROCESS_SPAWN_CAP_SHIFT 8u /* Explicit subset of the caller's capabilities. */
#define VEX_MRAC_VERSION_CURRENT 0u
#define VEX_MRAC_REMOVE_CURRENT  (1u << 0)
#define VEX_MRAC_REMOVE_ALL      (1u << 1)
#define VEX_GPU_BACKEND_NONE        0u
#define VEX_GPU_BACKEND_SOFTWARE    1u
#define VEX_GPU_BACKEND_VULKAN_PATH 2u
#define VEX_GPU_FORMAT_XRGB8888     1u
#define VEX_GPU_MEMORY_CPU_VISIBLE  (1u << 0)
#define VEX_GPU_MEMORY_SCANOUT      (1u << 1)
#define VEX_GPU_MEMORY_DEVICE_LOCAL (1u << 2)
#define VEX_GPU_MEMORY_RENDER_TARGET (1u << 3)
#define VEX_GPU_MEMORY_SAMPLED      (1u << 4)
#define VEX_GPU_CAP_CPU_MAP          (1ull << 0)
#define VEX_GPU_CAP_SCANOUT          (1ull << 1)
#define VEX_GPU_CAP_DAMAGE           (1ull << 2)
#define VEX_GPU_CAP_COMPOSITE        (1ull << 3)
#define VEX_GPU_CAP_LINEAR_FILTER    (1ull << 4)
#define VEX_GPU_CAP_TRANSFORMS       (1ull << 5)
#define VEX_GPU_CAP_DEVICE_LOCAL     (1ull << 6)
#define VEX_GPU_CAP_ASYNC_SUBMIT     (1ull << 7)
#define VEX_GPU_CAP_TIMELINE_FENCES  (1ull << 8)
#define VEX_GPU_CAP_VULKAN_1_3       (1ull << 16)
#define VEX_GPU_CAPS_ABI_VERSION     1u
#define VEX_EVENT_COUNTER_SEMAPHORE  0x1u
#define VEX_EVENT_COUNTER_NONBLOCK   0x2u
#define VEX_GPU_PRESENT_IMMEDIATE   (1u << 0)
#define VEX_GPU_SURFACE_ALPHA_OPAQUE        0u
#define VEX_GPU_SURFACE_ALPHA_PREMULTIPLIED 1u
#define VEX_GPU_SURFACE_ALPHA_STRAIGHT      2u
#define VEX_GPU_SURFACE_FILTER_NEAREST      0u
#define VEX_GPU_SURFACE_FILTER_LINEAR       1u
#define VEX_GPU_SURFACE_TRANSFORM_IDENTITY  0u
#define VEX_GPU_SURFACE_USAGE_SAMPLED       (1u << 0)
#define VEX_GPU_CMD_CLEAR           1u
#define VEX_GPU_CMD_FILL_RECT       2u
#define VEX_GPU_CMD_BLIT            3u
#define VEX_APP_WINDOW_FLAG_RESIZABLE (1u << 0)
#define VEX_APP_EVENT_NONE          0u
#define VEX_APP_EVENT_CONFIGURE     1u
#define VEX_APP_EVENT_MOUSE_CLICK   2u
#define VEX_APP_EVENT_KEY           3u
#define VEX_APP_EVENT_CLOSE         4u
#define VEX_APP_MOUSE_LEFT_DOWN     (1u << 0)
#define VEX_APP_MOUSE_LEFT_UP       (1u << 1)
#define VEX_APP_MOUSE_MOVE          (1u << 2)
#define VEX_APP_MOUSE_DRAG          (1u << 3)
/* Opt-in structured input (toolkits such as raylib). A window opened with
 * RAW_INPUT receives POINTER and KEY_RAW instead of MOUSE_CLICK and KEY:
 *   POINTER  x, y  content position (int64, may lie outside while captured)
 *            flags VEX_APP_POINTER_* buttons held, w wheel steps (int64)
 *   KEY_RAW  x     key: a character (Latin-1) or 0x110000 + scancode
 *            y     PS/2 set-1 scancode, +128 for extended (E0) keys
 *            flags bit 0 pressed, bits 8..15 modifiers (1 ctrl, 2 shift, 4 alt)
 *   FOCUS    flags 1 gained, 0 lost */
#define VEX_APP_WINDOW_FLAG_RAW_INPUT (1u << 1)
/* Opt-in close notification: retain the visible window until its owner accepts
 * the close. Poll consumes the request, allowing an app to confirm/cancel. */
#define VEX_APP_WINDOW_FLAG_CLOSE_CONFIRM (1u << 2)
#define VEX_APP_EVENT_POINTER       5u
#define VEX_APP_EVENT_KEY_RAW       6u
#define VEX_APP_EVENT_FOCUS         7u
#define VEX_APP_EVENT_NOTIFY        8u /* Coalesced resource change; flags is a tag. */
#define VEX_APP_POINTER_LEFT        (1u << 0)
#define VEX_APP_POINTER_RIGHT       (1u << 1)
#define VEX_APP_POINTER_MIDDLE      (1u << 2)
#define VEX_APP_KEY_RAW_PRESSED     (1u << 0)
#define VEX_DIRECTORY_ENTRY_NAME_MAX 96u
/* vex_directory_entry_t.type, the VexFS entry kinds. */
#define VEX_DIRECTORY_ENTRY_FILE      1u
#define VEX_DIRECTORY_ENTRY_DIRECTORY 2u
#define VEX_DIRECTORY_ENTRY_SYMLINK   3u
#define VEX_DIRECTORY_TYPE_UNKNOWN   0u
#define VEX_DIRECTORY_TYPE_FILE      1u
#define VEX_DIRECTORY_TYPE_DIRECTORY 2u
#define VEX_DIRECTORY_TYPE_SYMLINK   3u
#define VEX_OPEN_CONTEXT_FILES       1u
#define VEX_OPEN_CONTEXT_EDITOR      2u

#define VEX_BULK_PID          (1ull << 0)
#define VEX_BULK_PPID         (1ull << 1)
#define VEX_BULK_UID          (1ull << 2)
#define VEX_BULK_EUID         (1ull << 3)
#define VEX_BULK_TIME         (1ull << 4)
#define VEX_BULK_CLOCK        (1ull << 5)
#define VEX_BULK_MEMORY       (1ull << 6)
#define VEX_BULK_SCHEDULER    (1ull << 7)
#define VEX_BULK_NETWORK      (1ull << 8)
#define VEX_BULK_SYSTEM       (1ull << 9)
#define VEX_BULK_ABI          (1ull << 10)
#define VEX_BULK_ALL          (VEX_BULK_PID | VEX_BULK_PPID | VEX_BULK_UID | VEX_BULK_EUID | VEX_BULK_TIME | VEX_BULK_CLOCK | VEX_BULK_MEMORY | VEX_BULK_SCHEDULER | VEX_BULK_NETWORK | VEX_BULK_SYSTEM | VEX_BULK_ABI)

typedef struct vex_bulk_result {
    uint64_t valid_mask;
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    uint32_t euid;
    uint32_t capabilities;
    uint8_t is_admin;
    uint8_t user_mode;
    uint8_t abi;
    uint8_t reserved0;
    uint64_t monotonic_ms;
    uint64_t scheduler_ticks;
    uint64_t unix_seconds;
    uint64_t total_pages;
    uint64_t used_pages;
    uint64_t free_pages;
    uint64_t process_count;
    uint32_t local_ipv4;
    uint32_t gateway_ipv4;
    uint32_t netmask_ipv4;
    uint32_t link_up;
    char kernel_name[16];
    char machine[16];
} vex_bulk_result_t;

typedef struct vex_bulk_req {
    uint64_t mask;
    vex_bulk_result_t *result;
} vex_bulk_req_t;

typedef struct vex_event_register_req {
    uint16_t event_id;
    uint16_t reserved0;
    uint32_t reserved1;
    uint64_t handler;
    uint64_t restorer;
    uint64_t flags;
    uint64_t delivery_value;
} vex_event_register_req_t;

typedef struct vex_event_mask_req {
    uint32_t how;
    uint32_t reserved;
    uint64_t set;
    uint64_t old_mask;
} vex_event_mask_req_t;

typedef struct vex_event_post_req {
    uint32_t process_id;
    uint32_t thread_id;
    uint16_t event_id;
    uint16_t reserved0;
    uint32_t reserved1;
} vex_event_post_req_t;

typedef struct vex_directory_entry {
    char name[VEX_DIRECTORY_ENTRY_NAME_MAX];
    uint8_t type;
    uint8_t reserved[7];
} vex_directory_entry_t;

typedef struct vex_directory_list_req {
    const char *path;
    vex_directory_entry_t *entries;
    size_t capacity;
    size_t count;
} vex_directory_list_req_t;

typedef struct vex_open_path_req {
    const char *path;
    uint8_t type;
    uint8_t reserved[7];
} vex_open_path_req_t;

typedef struct vex_open_context_req {
    uint32_t application;
    uint32_t reserved;
    char *buffer;
    size_t size;
} vex_open_context_req_t;

typedef struct vex_process_spawn_req {
    const char *path;
    uint32_t flags;
    uint32_t pid;
} vex_process_spawn_req_t;

typedef struct vex_process_wait_req {
    int32_t pid;
    int *status;
    uint32_t flags;
} vex_process_wait_req_t;

typedef struct vex_process_exec_req {
    const char *path;
    char *const *argv;
    char *const *envp;
} vex_process_exec_req_t;

typedef struct vex_thread_create_req {
    void (*entry)(void *);
    void *arg;
    uint32_t flags;
    uint32_t tid;
} vex_thread_create_req_t;

typedef struct vex_mrac_put_req {
    uint64_t key_hash;
    const void *data;
    size_t size;
    uint32_t version_out;
} vex_mrac_put_req_t;

typedef struct vex_mrac_get_req {
    uint64_t key_hash;
    uint32_t version;
    void *buffer;
    size_t buffer_size;
    size_t size_out;
    uint32_t resolved_version;
} vex_mrac_get_req_t;

typedef struct vex_mrac_remove_req {
    uint64_t key_hash;
    uint32_t version;
    uint32_t flags;
} vex_mrac_remove_req_t;

typedef struct vex_mrac_version_info {
    uint32_t version;
    size_t size;
    uint8_t is_current;
} vex_mrac_version_info_t;

typedef struct vex_mrac_version_info_req {
    uint64_t key_hash;
    size_t index;
    vex_mrac_version_info_t *info;
} vex_mrac_version_info_req_t;

typedef struct vex_rw_offset_req {
    int fd;
    void *buffer;
    size_t count;
    off_t offset;
} vex_rw_offset_req_t;

typedef struct vex_fd_io_req {
    int fd;
    void *buffer;
    size_t count;
} vex_fd_io_req_t;

typedef struct vex_path_stat_req {
    const char *path;
    struct stat *st;
} vex_path_stat_req_t;

typedef struct vex_rename_req {
    const char *old_path;
    const char *new_path;
} vex_rename_req_t;

typedef struct vex_mkdir_req {
    const char *path;
    uint32_t mode;
} vex_mkdir_req_t;

typedef struct vex_getcwd_req {
    char *buffer;
    size_t size;
} vex_getcwd_req_t;

typedef struct vex_ioctl_req {
    int fd;
    unsigned long request;
    void *arg;
} vex_ioctl_req_t;

typedef struct vex_pipe_req {
    int *pipefd;
} vex_pipe_req_t;

typedef struct vex_mmap_req {
    void *addr;
    size_t length;
    int prot;
    int flags;
    int fd;
    off_t offset;
} vex_mmap_req_t;

typedef struct vex_shm_create_req {
    const char *name;
    size_t size;
    int prot;
    uint32_t flags;
    uint64_t handle;
} vex_shm_create_req_t;

typedef struct vex_shm_open_req {
    const char *name;
    uint64_t handle;
} vex_shm_open_req_t;

typedef struct vex_shm_map_req {
    uint64_t handle;
    void *preferred_addr;
    size_t length;
    size_t offset;
    int prot;
    uint32_t flags;
    void *mapped_addr;
} vex_shm_map_req_t;

typedef struct vex_heap_req {
    void *ptr;
    size_t size;
} vex_heap_req_t;

typedef struct vex_poll_req {
    struct pollfd *fds;
    nfds_t count;
    int timeout;
} vex_poll_req_t;

typedef struct vex_gettimeofday_req {
    struct timeval *tv;
    void *tz;
} vex_gettimeofday_req_t;

typedef struct vex_clock_gettime_req {
    int clock_id;
    struct timespec *ts;
} vex_clock_gettime_req_t;

typedef struct vex_nanosleep_req {
    const struct timespec *requested;
    struct timespec *remaining;
} vex_nanosleep_req_t;

typedef struct vex_readlink_req {
    const char *path;
    char *buffer;
    size_t size;
} vex_readlink_req_t;

typedef struct vex_symlink_req {
    const char *target;
    const char *link_path;
} vex_symlink_req_t;

typedef struct vex_realpath_req {
    const char *path;
    char *resolved_path;
} vex_realpath_req_t;

typedef struct vex_setenv_req {
    const char *name;
    const char *value;
    int overwrite;
} vex_setenv_req_t;

typedef struct vex_getrandom_req {
    void *buffer;
    size_t length;
    unsigned int flags;
} vex_getrandom_req_t;

typedef struct vex_sockaddr_req {
    int sockfd;
    const struct sockaddr *addr;
    uint32_t addrlen;
} vex_sockaddr_req_t;

typedef struct vex_send_recv_req {
    int sockfd;
    void *buffer;
    size_t length;
    int flags;
} vex_send_recv_req_t;

typedef struct vex_connect_hostname_req {
    int sockfd;
    const char *hostname;
    uint16_t port;
} vex_connect_hostname_req_t;

typedef struct vex_accept_req {
    int sockfd;
    struct sockaddr *addr;
    uint32_t *addrlen;
} vex_accept_req_t;

typedef struct vex_sockopt_req {
    int sockfd;
    int level;
    int option_name;
    void *option_value;
    uint32_t *option_len;
} vex_sockopt_req_t;

typedef struct vex_socketpair_req {
    int domain;
    int type;
    int protocol;
    int *sv;
} vex_socketpair_req_t;

typedef struct vex_message_pair_req {
    int *fds;
    uint32_t flags;
    uint32_t reserved;
} vex_message_pair_req_t;

#define VEX_MESSAGE_OBJECT_FD      1u
#define VEX_MESSAGE_OBJECT_SURFACE 2u
#define VEX_MESSAGE_ATTACHMENT_DUP (1u << 0)
#define VEX_SURFACE_RIGHT_QUERY     (1u << 0)
#define VEX_SURFACE_RIGHT_PRESENT   (1u << 1)
#define VEX_SURFACE_RIGHT_MAP_READ (1u << 2)
#define VEX_SURFACE_RIGHT_MAP_WRITE (1u << 3)
#define VEX_SURFACE_RIGHT_RESIZE    (1u << 4)
#define VEX_SURFACE_RIGHT_EXPORT    (1u << 5)
#define VEX_SURFACE_RIGHT_COMPOSE   (1u << 6)
#define VEX_SURFACE_RIGHT_ALL (VEX_SURFACE_RIGHT_QUERY | \
    VEX_SURFACE_RIGHT_PRESENT | VEX_SURFACE_RIGHT_MAP_READ | \
    VEX_SURFACE_RIGHT_MAP_WRITE | VEX_SURFACE_RIGHT_RESIZE | \
    VEX_SURFACE_RIGHT_EXPORT | VEX_SURFACE_RIGHT_COMPOSE)
typedef struct vex_message_attachment_req {
    uint64_t handle; /* local FD or Vex Surface capability; never transferred */
    uint32_t type;
    uint32_t rights; /* SEND: rights to export; RECV: transferred rights (output) */
} vex_message_attachment_req_t;

typedef struct vex_message_io_req {
    /* Native MESSAGE_SEND/RECV return bytes/EOF or a negative error code.
     * The legacy/Linux wrappers retain their own -1 + errno contract. */
    int fd;
    uint32_t flags;
    void *buffer;
    size_t length;
    vex_message_attachment_req_t *attachments;
    size_t attachment_count; /* SEND count; RECV input capacity/output count */
} vex_message_io_req_t;

/* Snapshot made by the kernel at SEND, delivered atomically with that exact
 * message. Payload identities, endpoint creators and later PID lookups are
 * never authentication. scope_rights is an immutable admission-capability projection (zero for
 * ordinary channels); EOF returns an empty identity. */
typedef struct vex_message_sender {uint32_t pid,tid,uid,scope_rights;} vex_message_sender_t;
typedef struct vex_message_auth_req {vex_message_io_req_t io;vex_message_sender_t sender;} vex_message_auth_req_t;

typedef struct vex_rect {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} vex_rect_t;

typedef struct vex_mouse_packet {
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
    uint8_t reserved0;
    uint16_t reserved1;
} vex_mouse_packet_t;

typedef struct vex_display_present_req {
    const uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    const vex_rect_t *dirty;
} vex_display_present_req_t;

typedef struct vex_app_window_open_req {
    const char *title;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    uint32_t window_id;
} vex_app_window_open_req_t;

typedef struct vex_app_window_present_req {
    uint32_t window_id;
    uint32_t reserved0;
    const uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    const vex_rect_t *dirty;
} vex_app_window_present_req_t;

typedef struct vex_app_window_event {
    uint32_t type;
    uint32_t flags;
    uint64_t x;
    uint64_t y;
    uint64_t w;
    uint64_t h;
    char key;
    char reserved[7];
} vex_app_window_event_t;

typedef struct vex_app_window_poll_req {
    uint32_t window_id;
    uint32_t flags;
    vex_app_window_event_t *event;
} vex_app_window_poll_req_t;
/* Optional scheduler-backed wait; flags=0 retains the legacy nonblocking poll. */
#define VEX_APP_WINDOW_POLL_WAIT 1u
/* Optional relative deadline in scheduler ticks; zero retains infinite wait.
 * Existing request layout and flags=0/WAIT binaries remain compatible. */
#define VEX_APP_WINDOW_POLL_TIMEOUT_SHIFT 16u

typedef struct vex_app_window_resize_req {
    uint32_t window_id;
    uint32_t width;
    uint32_t height;
    uint32_t reserved0;
} vex_app_window_resize_req_t;

typedef struct vex_gpu_info {
    uint32_t backend;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
    uint32_t queue_count;
    uint64_t framebuffer_address;
    uint64_t mmio_base;
    uint64_t mmio_size;
} vex_gpu_info_t;

/*
 * Versioned extension to vex_gpu_info_t. Keeping this separate preserves the
 * size of the original query structure for already-built applications.
 */
typedef struct vex_gpu_caps {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t backend;
    uint32_t reserved0;
    uint64_t capabilities;
    char driver_name[32];
} vex_gpu_caps_t;

typedef struct vex_gpu_buffer_create_req {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t flags;
    uint32_t handle;
    size_t size;
} vex_gpu_buffer_create_req_t;

typedef struct vex_gpu_command {
    uint32_t op;
    uint32_t reserved0;
    vex_rect_t rect;
    uint32_t color;
    uint32_t src_width;
    uint32_t src_height;
    uint32_t src_stride;
    const uint32_t *src_pixels;
} vex_gpu_command_t;

typedef struct vex_gpu_submit_req {
    uint32_t queue_id;
    uint32_t buffer_handle;
    uint32_t command_count;
    uint32_t flags;
    const vex_gpu_command_t *commands;
} vex_gpu_submit_req_t;

typedef struct vex_gpu_present_req {
    uint32_t buffer_handle;
    uint32_t flags;
    vex_rect_t dirty;
    int32_t dst_x;
    int32_t dst_y;
} vex_gpu_present_req_t;

typedef struct vex_gpu_surface_create_req {
    uint32_t buffer_handle;
    uint32_t alpha_mode;
    uint32_t usage;
    uint64_t surface_handle;
} vex_gpu_surface_create_req_t;

typedef struct vex_gpu_surface_present_req {
    uint64_t surface_handle;
    uint32_t flags;
    vex_rect_t source;
    vex_rect_t destination;
    const vex_rect_t *damage;
    uint32_t damage_count;
    uint32_t opacity;
    uint32_t filter;
    uint32_t transform;
    uint32_t reserved0;
    uint64_t wait_serial;
    uint64_t signal_serial;
    /* Optional, retained completion notifications for asynchronous WSI.
     * Both are process-owned event-counter capabilities. */
    uint64_t image_available_event;
    uint64_t swapchain_available_event;
} vex_gpu_surface_present_req_t;

#define VEX_GPU_SURFACE_PRESENT_ASYNC (1u << 1)

typedef struct vex_gpu_surface_wait_req {
    uint64_t surface_handle;
    uint64_t serial;
    uint64_t timeout_ticks; /* zero waits indefinitely */
} vex_gpu_surface_wait_req_t;

typedef struct vex_event_counter_read_req {
    uint64_t capability;
    uint64_t *value_out;
    uint64_t timeout_ticks; /* zero waits indefinitely */
    uint32_t flags;
    uint32_t reserved0;
} vex_event_counter_read_req_t;

typedef struct vex_surface_map_req {
    uint64_t surface_handle;
    uint64_t preferred_addr; /* zero asks VexOS to choose a free user range */
    uint64_t offset;
    uint64_t length;
    uint32_t access; /* VEX_SURFACE_MAP_READ/WRITE; execute is never allowed */
    uint32_t flags;
    uint64_t mapped_addr; /* output */
} vex_surface_map_req_t;

/* User-visible Surface metadata deliberately omits the kernel-only shared
 * backing id. Object/backing identities are diagnostic identities, not
 * capabilities; surface_handle remains the process-local capability. */
typedef struct vex_surface_query_req {
    uint64_t surface_handle;
    uint64_t object_identity;
    uint64_t backing_identity;
    uint64_t submitted_serial;
    uint64_t completed_serial;
    uint64_t backing_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride; /* uint32_t pixels per row, NOT a byte pitch */
    uint32_t format;
    uint32_t alpha_mode;
    uint32_t usage;
    uint32_t backing_type;
    uint32_t reserved0;
    uint32_t capability_rights;
    uint32_t reserved1;
} vex_surface_query_req_t;

typedef struct vex_surface_unmap_req {
    uint64_t surface_handle;
    uint64_t mapped_addr;
    uint64_t length;
    uint64_t offset;
    uint32_t access;
    uint32_t flags;
} vex_surface_unmap_req_t;

#define VEX_SURFACE_MAP_READ  (1u << 0)
#define VEX_SURFACE_MAP_WRITE (1u << 1)
#define VEX_SURFACE_RIGHT_QUERY     (1u << 0)
#define VEX_SURFACE_RIGHT_PRESENT   (1u << 1)
#define VEX_SURFACE_RIGHT_MAP_READ  (1u << 2)
#define VEX_SURFACE_RIGHT_MAP_WRITE (1u << 3)
#define VEX_SURFACE_RIGHT_RESIZE    (1u << 4)
#define VEX_SURFACE_RIGHT_EXPORT    (1u << 5)
#define VEX_SURFACE_RIGHT_COMPOSE   (1u << 6)
#define VEX_SURFACE_BACKING_CPU     1u
#define VEX_SURFACE_BACKING_DEVICE  2u

typedef struct vex_browser_command_req {
    char *buffer;
    size_t size;
} vex_browser_command_req_t;

#define VEX_BROWSER_RESULTS_BEGIN 1u
#define VEX_BROWSER_RESULTS_ADD   2u
#define VEX_BROWSER_RESULTS_SHOW  3u
#define VEX_BROWSER_RESULTS_HIDE  4u

#define VEX_ELEVATION_STATE_NONE      0u
#define VEX_ELEVATION_STATE_PENDING   1u
#define VEX_ELEVATION_STATE_APPROVED  2u
#define VEX_ELEVATION_STATE_DENIED    3u
#define VEX_ELEVATION_STATE_EXPIRED   4u
#define VEX_ELEVATION_STATE_CANCELLED 5u

#define VEX_ELEVATION_CAP_FS_ADMIN      VEX_CAP_FS_ADMIN
#define VEX_ELEVATION_CAP_PROC_ADMIN    VEX_CAP_PROC_ADMIN
#define VEX_ELEVATION_CAP_NET_ADMIN     VEX_CAP_NET_ADMIN
#define VEX_ELEVATION_CAP_USER_ADMIN    VEX_CAP_USER_ADMIN
#define VEX_ELEVATION_CAP_AUDIT_ADMIN   VEX_CAP_AUDIT_ADMIN
#define VEX_ELEVATION_CAP_SYSTEM_ADMIN  VEX_CAP_SYSTEM_ADMIN
#define VEX_ELEVATION_CAP_BROWSER_ADMIN VEX_CAP_BROWSER_ADMIN

typedef struct vex_browser_result_req {
    uint32_t op;
    char query[160];
    char title[96];
    char url[256];
    char snippet[192];
} vex_browser_result_req_t;

typedef struct vex_elevation_request_req {
    const char *reason;
    uint32_t flags;
    uint32_t request_id;
} vex_elevation_request_req_t;

typedef struct vex_elevation_status_req {
    uint32_t request_id;
    uint32_t state;
    uint32_t pid;
    uint32_t flags;
    uint64_t created_ticks;
    uint64_t expires_ticks;
    uint32_t capabilities;
    uint8_t is_admin;
    uint8_t reserved[3];
} vex_elevation_status_req_t;

#endif
