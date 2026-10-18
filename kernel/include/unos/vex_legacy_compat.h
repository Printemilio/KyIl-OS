#ifndef UNOS_VEX_LEGACY_COMPAT_H
#define UNOS_VEX_LEGACY_COMPAT_H

/*
 * Shared compatibility substrate for the native runtime, browser bridge,
 * and ABI support code.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef VEX_HOSTED_BUILD
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
typedef uid_t vex_uid_t;
#else
typedef long long ssize_t;
typedef long long off_t;
typedef long long time_t;
typedef int vex_uid_t;
struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};
#endif
typedef long long nfds_t;

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NONBLOCK  0x0800
#define O_CLOEXEC   0x80000
#define MSG_DONTWAIT 0x40

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void *)(uintptr_t)-1)

#define AF_UNSPEC 0
#define AF_UNIX   1
#define AF_INET   2
#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif

#define SOL_SOCKET 1

#define SOCK_STREAM 1
#define SOCK_SEQPACKET 5
#define SOCK_NONBLOCK 0x800
#define SOCK_CLOEXEC  0x80000

#define SO_TYPE 3
#define SO_ERROR 4
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21

#define POLLIN  0x0001
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLNVAL 0x0020
#define POLLHUP 0x0010

#define F_GETFL 3
#define F_SETFL 4
#define F_GETFD 1
#define F_SETFD 2
#define F_DUPFD 0
#define F_DUPFD_CLOEXEC 1030
#define FD_CLOEXEC 1

#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_NPROCESSORS_ONLN 84
#define _SC_OPEN_MAX 4

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define S_IFMT  0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFLNK 0120000
#define S_IFCHR 0020000
#define S_IFSOCK 0140000

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

#define EPERM         1
#define ENOENT        2
#define ESRCH         3
#define EINTR         4
#define EIO           5
#define E2BIG         7
#define EBADF         9
#define ECHILD       10
#define EAGAIN       11
#define ENOMEM       12
#define EACCES       13
#define EFAULT       14
#define EBUSY        16
#define EEXIST       17
#define EXDEV        18
#define ENODEV       19
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define ENOTTY       25
#define ENFILE       23
#define EMFILE       24
#define ENOSPC       28
#define ESPIPE       29
#define ERANGE       34
#define ENAMETOOLONG 36
#define ENOTEMPTY    39
#define EROFS        30
#define EPIPE        32
#define ENOLCK       37
#define ENOSYS       38
#define ENOTSUP      95
#define ENOTSOCK 88
#define EISCONN 106
#define EALREADY 114
#define EINPROGRESS 115
#define EAFNOSUPPORT 97
#define EMSGSIZE      90
#define EADDRNOTAVAIL 99
#define ENETUNREACH 101
#define ECONNRESET  104
#define ENOBUFS     105
#define ENOTCONN    107
#define ETIMEDOUT   110
#define ECANCELED   125
#define ECONNREFUSED 111

#define WNOHANG 0x1

#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGTERM 15
#define SIGCHLD 17

typedef void (*sighandler_t)(int);

struct sigaction {
    sighandler_t sa_handler;
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    unsigned long sa_mask;
};

struct stat {
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
    uint64_t st_blksize;
    uint64_t st_blocks;
};

#ifndef VEX_HOSTED_BUILD
struct timeval {
    time_t tv_sec;
    long tv_usec;
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#endif

struct pollfd {
    int fd;
    short events;
    short revents;
};

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

struct in_addr {
    uint32_t s_addr;
};

struct sockaddr {
    uint16_t sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    struct in_addr sin_addr;
    unsigned char sin_zero[8];
};

typedef struct vex_web_fd_object vex_web_fd_object_t;
typedef struct vex_message_attachment vex_message_attachment_t;

typedef struct vex_web_fd_ops {
    ssize_t (*read)(void *ctx, void *buffer, size_t count);
    ssize_t (*write)(void *ctx, const void *buffer, size_t count);
    int (*close)(void *ctx);
    int (*fstat)(void *ctx, struct stat *st);
    int (*seek)(void *ctx, off_t offset, int whence, off_t *result);
    int (*fsync)(void *ctx);
    int (*poll)(void *ctx, short events, short *revents);
    /* Positional I/O must not mutate the open-file-description cursor.  A
     * missing callback means that the object does not support pread/pwrite. */
    ssize_t (*pread)(void *ctx, void *buffer, size_t count, off_t offset);
    ssize_t (*pwrite)(void *ctx, const void *buffer, size_t count, off_t offset);
} vex_web_fd_ops_t;

struct vex_web_fd_object {
    uint32_t refcount;
    void *ctx;
    const vex_web_fd_ops_t *ops;
    /* Kernel-private sparse shared pages, owned by the object, not a mapping.
     * No FD number or process identity participates in page ownership. */
    void *shared_mapping_pages;
};

#ifdef __cplusplus
extern "C" {
#endif

int *vex_web_errno_location(void);
int vex_poll_kernel(struct pollfd *fds,nfds_t count,int timeout);

vex_web_fd_object_t *vex_web_fd_object_create(void *ctx, const vex_web_fd_ops_t *ops);
int vex_web_fd_install(vex_web_fd_object_t *object, int preferred_fd);
int vex_web_fd_install_flags(vex_web_fd_object_t *object, int preferred_fd,
                             uint8_t close_on_exec);
vex_web_fd_object_t *vex_web_fd_acquire(int fd);
int vex_web_fd_release(vex_web_fd_object_t *object);
/* Diagnostics: the path of a file object ("iso:<lba>" for a streamed ISO file),
 * empty when it has none. */
void vex_web_fd_describe(const vex_web_fd_object_t *object, char *out, size_t capacity);
ssize_t vex_web_fd_pread_object(vex_web_fd_object_t *object, void *buffer,
                                size_t count, off_t offset);
int vex_web_fd_is_wk2_shared_memory(int fd);
int vex_web_fd_dup_object(int fd, int preferred_fd);
void vex_web_posix_init(void);
int vex_web_eventfd_create(uint64_t initial, uint32_t flags);
int vex_native_message_pair(uint32_t flags, int sv[2]);
int vex_socketpair_fcntl(vex_web_fd_object_t *object, int command, int arg);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

int open(const char *path, int flags, ...);
int openat(int dirfd, const char *path, int flags);
/* open() with an explicit O_CREAT mode (VEX_SYS_OPEN_EX). */
int vex_open_mode(const char *path, int flags, uint32_t create_mode);
int vex_web_resolve_dirfd_path(int dirfd, const char *path, char *buffer, size_t size);
ssize_t vex_web_getdents64(int fd, void *buffer, size_t count);
int close(int fd);
ssize_t read(int fd, void *buffer, size_t count);
ssize_t write(int fd, const void *buffer, size_t count);
ssize_t pread(int fd, void *buffer, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buffer, size_t count, off_t offset);
off_t lseek(int fd, off_t offset, int whence);
int fsync(int fd);
int stat(const char *path, struct stat *st);
int lstat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
/* VEX_SYS_STAT_EX backends: 0 or a negated errno. */
struct vex_stat_ex;
int vex_stat_ex_path(const char *path, int follow, struct vex_stat_ex *out);
int vex_stat_ex_fd(int fd, struct vex_stat_ex *out);
int vex_stat_ex_object(int fd, struct vex_stat_ex *out);
struct vex_set_times_req;
int vex_set_times(const struct vex_set_times_req *req);
int vex_fchmod(int fd, uint32_t mode);
/* Kernel-private metadata helper; no syscall or public native ABI change. */
int vex_web_fd_times(int fd, void *times);
uint64_t ladybird_fd_inode_hint(int fd);
int access(const char *path, int mode);
int unlink(const char *path);
int rename(const char *old_path, const char *new_path);
int mkdir(const char *path, uint32_t mode);
int rmdir(const char *path);
int chdir(const char *path);
char *getcwd(char *buffer, size_t size);
int getpid(void);
int getppid(void);
vex_uid_t getuid(void);
vex_uid_t geteuid(void);
int isatty(int fd);
int ioctl(int fd, unsigned long request, void *arg);
int fcntl(int fd, int command, int arg);
int dup(int fd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);
int pipe2(int pipefd[2], int flags);
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t length, int prot);
int madvise(void *addr, size_t length, int advice);
int poll(struct pollfd *fds, nfds_t count, int timeout);
void exit(int status);
void _exit(int status);
int wait(int *status);
int waitpid(int pid, int *status, int options);
int fork(void);
int exec(const char *path);
int execve(const char *path, char *const argv[], char *const envp[]);
int kill(int pid, int sig);
int sigaction(int sig, const struct sigaction *action, struct sigaction *old_action);
int gettimeofday(struct timeval *tv, void *tz);
int clock_gettime(int clock_id, struct timespec *ts);
int nanosleep(const struct timespec *requested, struct timespec *remaining);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
int ftruncate(int fd, off_t length);
int chmod(const char *path, uint32_t mode);
ssize_t readlink(const char *path, char *buffer, size_t size);
int symlink(const char *target, const char *link_path);
char *realpath(const char *path, char *resolved_path);
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
long sysconf(int name);
int uname(void *buffer);
int getrandom(void *buffer, size_t length, unsigned int flags);
int posix_memalign(void **memptr, size_t alignment, size_t size);
size_t malloc_good_size(size_t size);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

int socket(int domain, int type, int protocol);
int connect(int sockfd, const struct sockaddr *addr, uint32_t addrlen);
ssize_t send(int sockfd, const void *buffer, size_t length, int flags);
ssize_t recv(int sockfd, void *buffer, size_t length, int flags);
ssize_t vex_socketpair_send_attached(int sockfd, const void *buffer,
                                     size_t length, int flags,
                                     vex_message_attachment_t *attachments,
                                     size_t attachment_count);
ssize_t vex_socketpair_receive_attached(int sockfd, void *buffer,
                                        size_t length, int flags,
                                        vex_message_attachment_t *attachments,
                                        size_t *attachment_count);
int vex_web_connect_hostname(int sockfd, const char *hostname, uint16_t port);
int bind(int sockfd, const struct sockaddr *addr, uint32_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, uint32_t *addrlen);
int setsockopt(int sockfd, int level, int option_name, const void *option_value, uint32_t option_len);
int getsockopt(int sockfd, int level, int option_name, void *option_value, uint32_t *option_len);
int socketpair(int domain, int type, int protocol, int sv[2]);
/* Linux metadata for native Vex message endpoints; no AF_UNIX pathname is
 * created or exposed. */
int vex_socket_network_fd(int fd);
ssize_t vex_socket_sendto(int fd,const void *buffer,size_t length,int flags,const struct sockaddr *address,uint32_t address_length);
ssize_t vex_socket_recvfrom(int fd,void *buffer,size_t length,int flags,struct sockaddr *address,uint32_t capacity,uint32_t *actual);
int vex_socketpair_getsockname(int sockfd, struct sockaddr *address,
                               uint32_t capacity, uint32_t *actual_length);
int vex_socketpair_getpeername(int sockfd, struct sockaddr *address,
                               uint32_t capacity, uint32_t *actual_length);

typedef struct vex_web_display_surface {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t *pixels;
} vex_web_display_surface_t;

int vex_web_display_open(uint32_t width, uint32_t height);
void vex_web_display_resize(uint32_t width, uint32_t height);
vex_web_display_surface_t *vex_web_display_surface(void);
void vex_web_display_present(void);
void vex_web_display_refresh(void);
void vex_web_display_close(void);
void vex_web_display_pump(void);
void vex_web_test_partial_present(int x, int y, int w, int h);
void vex_web_display_set_pending_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void vex_web_display_clear_pending_dirty(void);
void vex_web_debug_log(const char *text);
void vex_web_browser_yield_to_process(void);
uint32_t vex_web_browser_process_pid(void);
uint32_t vex_web_browser_parent_pid(void);
int vex_web_browser_command_push(const char *command);
int vex_web_browser_command_pop(char *buffer, size_t size);
void vex_web_browser_results_begin(const char *query);
void vex_web_browser_results_add(const char *title, const char *url, const char *snippet);
void vex_web_browser_results_show(void);
void vex_web_browser_results_hide(void);
uint8_t vex_web_browser_results_visible(void);
uint32_t vex_web_browser_results_count(void);
const char *vex_web_browser_results_query(void);
const char *vex_web_browser_result_title(uint32_t index);
const char *vex_web_browser_result_url(uint32_t index);
const char *vex_web_browser_result_snippet(uint32_t index);

/*
 * Backward-compatibility aliases kept while the remaining kernel/userspace
 * internals are migrated off the historical browser namespace.
 */
#define ladybird_fd_object_t vex_web_fd_object_t
#define ladybird_fd_ops_t vex_web_fd_ops_t
#define ladybird_display_surface_t vex_web_display_surface_t
#define ladybird_errno_location vex_web_errno_location
#define ladybird_fd_object_create vex_web_fd_object_create
#define ladybird_fd_install vex_web_fd_install
#define ladybird_fd_install_flags vex_web_fd_install_flags
#define ladybird_fd_acquire vex_web_fd_acquire
#define ladybird_fd_release vex_web_fd_release
#define ladybird_fd_dup_object vex_web_fd_dup_object
#define ladybird_resolve_dirfd_path vex_web_resolve_dirfd_path
#define ladybird_getdents64 vex_web_getdents64
#define ladybird_posix_init vex_web_posix_init
#define ladybird_connect_hostname vex_web_connect_hostname
#define ladybird_display_open vex_web_display_open
#define ladybird_display_resize vex_web_display_resize
#define ladybird_display_surface vex_web_display_surface
#define ladybird_display_present vex_web_display_present
#define ladybird_display_refresh vex_web_display_refresh
#define ladybird_display_close vex_web_display_close
#define ladybird_display_pump vex_web_display_pump
#define ladybird_test_partial_present vex_web_test_partial_present
#define ladybird_display_set_pending_dirty vex_web_display_set_pending_dirty
#define ladybird_display_clear_pending_dirty vex_web_display_clear_pending_dirty
#define ladybird_debug_log vex_web_debug_log
#define ladybird_browser_yield_to_process vex_web_browser_yield_to_process
#define ladybird_browser_process_pid vex_web_browser_process_pid
#define ladybird_browser_parent_pid vex_web_browser_parent_pid
#define ladybird_browser_command_push vex_web_browser_command_push
#define ladybird_browser_command_pop vex_web_browser_command_pop
#define ladybird_browser_results_begin vex_web_browser_results_begin
#define ladybird_browser_results_add vex_web_browser_results_add
#define ladybird_browser_results_show vex_web_browser_results_show
#define ladybird_browser_results_hide vex_web_browser_results_hide
#define ladybird_browser_results_visible vex_web_browser_results_visible
#define ladybird_browser_results_count vex_web_browser_results_count
#define ladybird_browser_results_query vex_web_browser_results_query
#define ladybird_browser_result_title vex_web_browser_result_title
#define ladybird_browser_result_url vex_web_browser_result_url
#define ladybird_browser_result_snippet vex_web_browser_result_snippet

#ifdef __cplusplus
}
#endif

#endif
