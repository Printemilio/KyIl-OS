#ifndef UNOS_VEX_ABI_H
#define UNOS_VEX_ABI_H

#include <stddef.h>
#include <stdint.h>

#include <unos/vex_web_compat.h>

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
#define VEX_SYS_MMAP              30ull
#define VEX_SYS_MUNMAP            31ull
#define VEX_SYS_MPROTECT          32ull
#define VEX_SYS_MADVISE           33ull
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
#define VEX_SYS_PROCESS_SPAWN    100ull
#define VEX_SYS_PROCESS_WAIT     101ull
#define VEX_SYS_PROCESS_KILL     102ull
#define VEX_SYS_PROCESS_EXIT     103ull
#define VEX_SYS_PROCESS_YIELD    104ull
#define VEX_SYS_PROCESS_FORK     105ull
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
#define VEX_SYS_JIT_ARENA_CREATE    126ull
#define VEX_SYS_JIT_ARENA_DESTROY   127ull
#define VEX_SYS_GPU_QUERY_INFO      130ull
#define VEX_SYS_GPU_BUFFER_CREATE   131ull
#define VEX_SYS_GPU_BUFFER_DESTROY  132ull
#define VEX_SYS_GPU_QUEUE_SUBMIT    133ull
#define VEX_SYS_GPU_PRESENT         134ull

#define VEX_PROCESS_WAIT_NOHANG 0x1u
#define VEX_MRAC_VERSION_CURRENT 0u
#define VEX_MRAC_REMOVE_CURRENT  (1u << 0)
#define VEX_MRAC_REMOVE_ALL      (1u << 1)
#define VEX_JIT_ARENA_FLAG_DISABLE_QUOTA (1u << 0)
#define VEX_JIT_ARENA_FLAG_DISABLE_LOCKS (1u << 1)
#define VEX_JIT_ARENA_FLAG_FULL_OPEN     (1u << 2)
#define VEX_GPU_BACKEND_NONE        0u
#define VEX_GPU_BACKEND_SOFTWARE    1u
#define VEX_GPU_BACKEND_VULKAN_PATH 2u
#define VEX_GPU_FORMAT_XRGB8888     1u
#define VEX_GPU_MEMORY_CPU_VISIBLE  (1u << 0)
#define VEX_GPU_MEMORY_SCANOUT      (1u << 1)
#define VEX_GPU_PRESENT_IMMEDIATE   (1u << 0)
#define VEX_GPU_CMD_CLEAR           1u
#define VEX_GPU_CMD_FILL_RECT       2u
#define VEX_GPU_CMD_BLIT            3u

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

typedef struct vex_jit_arena_create_req {
    void *preferred_rw_addr;
    void *preferred_rx_addr;
    size_t size;
    uint64_t quota_bytes;
    uint32_t flags;
    uint32_t arena_id;
    void *rw_base;
    void *rx_base;
    uint64_t source_namespace_id;
} vex_jit_arena_create_req_t;

typedef struct vex_jit_arena_destroy_req {
    void *addr;
    uint32_t arena_id;
    uint32_t flags;
} vex_jit_arena_destroy_req_t;

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

typedef struct vex_memory_req {
    void *addr;
    size_t length;
    int prot_or_advice;
} vex_memory_req_t;

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

typedef struct vex_browser_command_req {
    char *buffer;
    size_t size;
} vex_browser_command_req_t;

#define VEX_BROWSER_RESULTS_BEGIN 1u
#define VEX_BROWSER_RESULTS_ADD   2u
#define VEX_BROWSER_RESULTS_SHOW  3u
#define VEX_BROWSER_RESULTS_HIDE  4u

typedef struct vex_browser_result_req {
    uint32_t op;
    char query[160];
    char title[96];
    char url[256];
    char snippet[192];
} vex_browser_result_req_t;

#endif
