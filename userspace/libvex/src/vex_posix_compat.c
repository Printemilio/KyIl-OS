#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>
#include <unos_syscall.h>
#include <unos/vex_abi.h>

#include "vex_syscall.h"

static int vex_errno_value;

static int set_vex_errno(int value) {
    vex_errno_value = value;
    return -1;
}

int* ladybird_errno_location(void) {
    return &vex_errno_value;
}

ladybird_fd_object_t* ladybird_fd_object_create(void* ctx, const ladybird_fd_ops_t* ops) {
    (void)ctx;
    (void)ops;
    vex_errno_value = ENOSYS;
    return 0;
}

int ladybird_fd_install(ladybird_fd_object_t* object, int preferred_fd) {
    (void)object;
    (void)preferred_fd;
    vex_errno_value = ENOSYS;
    return -1;
}

ladybird_fd_object_t* ladybird_fd_acquire(int fd) {
    (void)fd;
    vex_errno_value = ENOSYS;
    return 0;
}

int ladybird_fd_release(ladybird_fd_object_t* object) {
    (void)object;
    return 0;
}

int ladybird_fd_dup_object(int fd, int preferred_fd) {
    (void)fd;
    (void)preferred_fd;
    vex_errno_value = ENOSYS;
    return -1;
}

void ladybird_posix_init(void) {
}

int open(const char* path, int flags, ...) {
    return (int)vex_syscall2(VEX_SYS_OPEN, (uint64_t)path, (uint64_t)flags);
}

int close(int fd) {
    return (int)vex_syscall1(VEX_SYS_CLOSE, (uint64_t)fd);
}

ssize_t read(int fd, void* buffer, size_t count) {
    vex_fd_io_req_t req;
    req.fd = fd;
    req.buffer = buffer;
    req.count = count;
    return (ssize_t)vex_syscall1(VEX_SYS_READFD, (uint64_t)&req);
}

ssize_t write(int fd, const void* buffer, size_t count) {
    vex_fd_io_req_t req;
    req.fd = fd;
    req.buffer = (void*)buffer;
    req.count = count;
    return (ssize_t)vex_syscall1(VEX_SYS_WRITEFD, (uint64_t)&req);
}

ssize_t pread(int fd, void* buffer, size_t count, off_t offset) {
    vex_rw_offset_req_t req;
    req.fd = fd;
    req.buffer = buffer;
    req.count = count;
    req.offset = offset;
    return (ssize_t)vex_syscall1(VEX_SYS_PREAD, (uint64_t)&req);
}

ssize_t pwrite(int fd, const void* buffer, size_t count, off_t offset) {
    vex_rw_offset_req_t req;
    req.fd = fd;
    req.buffer = (void*)buffer;
    req.count = count;
    req.offset = offset;
    return (ssize_t)vex_syscall1(VEX_SYS_PWRITE, (uint64_t)&req);
}

off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)vex_syscall3(VEX_SYS_LSEEK, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence);
}

int fsync(int fd) {
    return (int)vex_syscall1(VEX_SYS_FSYNC, (uint64_t)fd);
}

int stat(const char* path, struct stat* st) {
    vex_path_stat_req_t req;
    req.path = path;
    req.st = st;
    return (int)vex_syscall1(VEX_SYS_STAT, (uint64_t)&req);
}

int lstat(const char* path, struct stat* st) {
    vex_path_stat_req_t req;
    req.path = path;
    req.st = st;
    return (int)vex_syscall1(VEX_SYS_LSTAT, (uint64_t)&req);
}

int fstat(int fd, struct stat* st) {
    return (int)vex_syscall2(VEX_SYS_FSTAT, (uint64_t)fd, (uint64_t)st);
}

int access(const char* path, int mode) {
    return (int)vex_syscall2(VEX_SYS_ACCESS, (uint64_t)path, (uint64_t)mode);
}

int unlink(const char* path) {
    return (int)vex_syscall1(VEX_SYS_UNLINK, (uint64_t)path);
}

int rename(const char* old_path, const char* new_path) {
    vex_rename_req_t req;
    req.old_path = old_path;
    req.new_path = new_path;
    return (int)vex_syscall1(VEX_SYS_RENAME, (uint64_t)&req);
}

int mkdir(const char* path, uint32_t mode) {
    vex_mkdir_req_t req;
    req.path = path;
    req.mode = mode;
    return (int)vex_syscall1(VEX_SYS_MKDIR, (uint64_t)&req);
}

int rmdir(const char* path) {
    return (int)vex_syscall1(VEX_SYS_RMDIR, (uint64_t)path);
}

int chdir(const char* path) {
    return (int)vex_syscall1(VEX_SYS_CHDIR, (uint64_t)path);
}

char* getcwd(char* buffer, size_t size) {
    vex_getcwd_req_t req;
    req.buffer = buffer;
    req.size = size;
    return (char*)(uintptr_t)vex_syscall1(VEX_SYS_GETCWD, (uint64_t)&req);
}

int getpid(void) {
    return (int)vex_syscall0(VEX_SYS_GETPID);
}

int getppid(void) {
    return (int)vex_syscall0(VEX_SYS_GETPPID);
}

vex_uid_t getuid(void) {
    return (vex_uid_t)vex_syscall0(VEX_SYS_GETUID);
}

vex_uid_t geteuid(void) {
    return (vex_uid_t)vex_syscall0(VEX_SYS_GETEUID);
}

int isatty(int fd) {
    return (int)vex_syscall1(VEX_SYS_ISATTY, (uint64_t)fd);
}

int ioctl(int fd, unsigned long request, void* arg) {
    vex_ioctl_req_t req;
    req.fd = fd;
    req.request = request;
    req.arg = arg;
    return (int)vex_syscall1(VEX_SYS_IOCTL, (uint64_t)&req);
}

int fcntl(int fd, int command, int arg) {
    return (int)vex_syscall3(VEX_SYS_FCNTL, (uint64_t)fd, (uint64_t)command, (uint64_t)arg);
}

int dup(int fd) {
    return (int)vex_syscall1(VEX_SYS_DUP, (uint64_t)fd);
}

int dup2(int oldfd, int newfd) {
    return (int)vex_syscall2(VEX_SYS_DUP2, (uint64_t)oldfd, (uint64_t)newfd);
}

int pipe(int pipefd[2]) {
    vex_pipe_req_t req;
    req.pipefd = pipefd;
    return (int)vex_syscall1(VEX_SYS_PIPE, (uint64_t)&req);
}

int pipe2(int pipefd[2], int flags) {
    if ((flags & ~(O_NONBLOCK | O_CLOEXEC)) != 0) {
        return set_vex_errno(EINVAL);
    }
    if (pipe(pipefd) != 0) {
        return -1;
    }
    if ((flags & O_NONBLOCK) != 0) {
        (void)fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        (void)fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
    }
    if ((flags & O_CLOEXEC) != 0) {
        if (fcntl(pipefd[0], F_SETFD, FD_CLOEXEC) != 0 ||
            fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) != 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            return -1;
        }
    }
    return 0;
}

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    vex_mmap_req_t req;
    req.addr = addr;
    req.length = length;
    req.prot = prot;
    req.flags = flags;
    req.fd = fd;
    req.offset = offset;
    return (void*)(uintptr_t)vex_syscall1(VEX_SYS_MMAP, (uint64_t)&req);
}

int munmap(void* addr, size_t length) {
    vex_memory_req_t req;
    req.addr = addr;
    req.length = length;
    req.prot_or_advice = 0;
    return (int)vex_syscall1(VEX_SYS_MUNMAP, (uint64_t)&req);
}

int mprotect(void* addr, size_t length, int prot) {
    vex_memory_req_t req;
    req.addr = addr;
    req.length = length;
    req.prot_or_advice = prot;
    return (int)vex_syscall1(VEX_SYS_MPROTECT, (uint64_t)&req);
}

int madvise(void* addr, size_t length, int advice) {
    vex_memory_req_t req;
    req.addr = addr;
    req.length = length;
    req.prot_or_advice = advice;
    return (int)vex_syscall1(VEX_SYS_MADVISE, (uint64_t)&req);
}

int poll(struct pollfd* fds, nfds_t count, int timeout) {
    vex_poll_req_t req;
    req.fds = fds;
    req.count = count;
    req.timeout = timeout;
    return (int)vex_syscall1(VEX_SYS_POLL, (uint64_t)&req);
}

void exit(int status) {
    (void)vex_syscall1(VEX_SYS_PROCESS_EXIT, (uint64_t)status);
    for (;;) {
    }
}

void _exit(int status) {
    exit(status);
}

int wait(int* status) {
    return waitpid(-1, status, 0);
}

int waitpid(int pid, int* status, int options) {
    vex_process_wait_req_t req;
    int result;

    if ((options & ~WNOHANG) != 0) {
        return set_vex_errno(EINVAL);
    }

    req.pid = pid;
    req.status = status;
    req.flags = (uint32_t)options;
    result = (int)vex_syscall1(VEX_SYS_PROCESS_WAIT, (uint64_t)&req);
    if (result < 0) {
        return set_vex_errno(ECHILD);
    }
    return result;
}

int fork(void) {
    const int result = (int)vex_syscall0(VEX_SYS_PROCESS_FORK);
    if (result < 0) {
        return set_vex_errno(EAGAIN);
    }
    return result;
}

int exec(const char* path) {
    return execve(path, (char* const*)0, (char* const*)0);
}

int execve(const char* path, char* const argv[], char* const envp[]) {
    vex_process_exec_req_t req;
    int result;

    if (path == 0) {
        return set_vex_errno(EINVAL);
    }

    req.path = path;
    req.argv = argv;
    req.envp = envp;
    result = (int)vex_syscall1(VEX_SYS_PROCESS_EXEC, (uint64_t)&req);
    if (result < 0) {
        return set_vex_errno(EINVAL);
    }
    return result;
}

int kill(int pid, int sig) {
    if (sig != 0 && sig != SIGKILL && sig != SIGTERM) {
        return set_vex_errno(ENOSYS);
    }
    if (pid <= 0) {
        return set_vex_errno(EINVAL);
    }
    if (sig == 0) {
        return 0;
    }
    if ((int)vex_syscall1(VEX_SYS_PROCESS_KILL, (uint64_t)pid) != 0) {
        return set_vex_errno(ESRCH);
    }
    return 0;
}

int sigaction(int sig, const struct sigaction* action, struct sigaction* old_action) {
    (void)sig;
    (void)action;
    (void)old_action;
    return set_vex_errno(ENOSYS);
}

int gettimeofday(struct timeval* tv, void* tz) {
    vex_gettimeofday_req_t req;
    req.tv = tv;
    req.tz = tz;
    return (int)vex_syscall1(VEX_SYS_GETTIMEOFDAY, (uint64_t)&req);
}

int clock_gettime(int clock_id, struct timespec* ts) {
    vex_clock_gettime_req_t req;
    req.clock_id = clock_id;
    req.ts = ts;
    return (int)vex_syscall1(VEX_SYS_CLOCK_GETTIME, (uint64_t)&req);
}

int nanosleep(const struct timespec* requested, struct timespec* remaining) {
    vex_nanosleep_req_t req;
    req.requested = requested;
    req.remaining = remaining;
    return (int)vex_syscall1(VEX_SYS_NANOSLEEP, (uint64_t)&req);
}

unsigned int sleep(unsigned int seconds) {
    return (unsigned int)vex_syscall1(VEX_SYS_SLEEP, (uint64_t)seconds);
}

int usleep(unsigned int usec) {
    return (int)vex_syscall1(VEX_SYS_USLEEP, (uint64_t)usec);
}

int ftruncate(int fd, off_t length) {
    return (int)vex_syscall2(VEX_SYS_FTRUNCATE, (uint64_t)fd, (uint64_t)length);
}

int chmod(const char* path, uint32_t mode) {
    return (int)vex_syscall2(VEX_SYS_CHMOD, (uint64_t)path, (uint64_t)mode);
}

ssize_t readlink(const char* path, char* buffer, size_t size) {
    vex_readlink_req_t req;
    req.path = path;
    req.buffer = buffer;
    req.size = size;
    return (ssize_t)vex_syscall1(VEX_SYS_READLINK, (uint64_t)&req);
}

int symlink(const char* target, const char* link_path) {
    vex_symlink_req_t req;
    req.target = target;
    req.link_path = link_path;
    return (int)vex_syscall1(VEX_SYS_SYMLINK, (uint64_t)&req);
}

char* realpath(const char* path, char* resolved_path) {
    vex_realpath_req_t req;
    req.path = path;
    req.resolved_path = resolved_path;
    return (char*)(uintptr_t)vex_syscall1(VEX_SYS_REALPATH, (uint64_t)&req);
}

char* getenv(const char* name) {
    return (char*)(uintptr_t)vex_syscall1(VEX_SYS_GETENV, (uint64_t)name);
}

int setenv(const char* name, const char* value, int overwrite) {
    vex_setenv_req_t req;
    req.name = name;
    req.value = value;
    req.overwrite = overwrite;
    return (int)vex_syscall1(VEX_SYS_SETENV, (uint64_t)&req);
}

int unsetenv(const char* name) {
    return (int)vex_syscall1(VEX_SYS_UNSETENV, (uint64_t)name);
}

long sysconf(int name) {
    return (long)vex_syscall1(VEX_SYS_SYSCONF, (uint64_t)name);
}

int uname(void* buffer) {
    return (int)vex_syscall1(VEX_SYS_UNAME, (uint64_t)buffer);
}

int getrandom(void* buffer, size_t length, unsigned int flags) {
    vex_getrandom_req_t req;
    req.buffer = buffer;
    req.length = length;
    req.flags = flags;
    return (int)vex_syscall1(VEX_SYS_GETRANDOM, (uint64_t)&req);
}

int atoi(const char* str) {
    if (!str) return 0;
    while (*str == ' ' || *str == '\t') str++;
    int sign = 1;
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') { str++; }
    int result = 0;
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return sign * result;
}
