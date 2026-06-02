#include <unos/compat.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/elf.h>
#include <unos/scheduler.h>
#include <unos/vex_abi.h>
#include <unos/vex_syscall.h>

#define LINUX_SYS_READ       0ull
#define LINUX_SYS_WRITE      1ull
#define LINUX_SYS_OPEN       2ull
#define LINUX_SYS_CLOSE      3ull
#define LINUX_SYS_STAT       4ull
#define LINUX_SYS_FSTAT      5ull
#define LINUX_SYS_LSTAT      6ull
#define LINUX_SYS_LSEEK      8ull
#define LINUX_SYS_MPROTECT   10ull
#define LINUX_SYS_RT_SIGPROCMASK 14ull
#define LINUX_SYS_MMAP       9ull
#define LINUX_SYS_MUNMAP     11ull
#define LINUX_SYS_BRK        12ull
#define LINUX_SYS_RT_SIGACTION 13ull
#define LINUX_SYS_IOCTL      16ull
#define LINUX_SYS_PREAD64    17ull
#define LINUX_SYS_PWRITE64   18ull
#define LINUX_SYS_PIPE       22ull
#define LINUX_SYS_SOCKET     41ull
#define LINUX_SYS_CONNECT    42ull
#define LINUX_SYS_ACCEPT     43ull
#define LINUX_SYS_SENDTO     44ull
#define LINUX_SYS_RECVFROM   45ull
#define LINUX_SYS_BIND       49ull
#define LINUX_SYS_LISTEN     50ull
#define LINUX_SYS_SETSOCKOPT 54ull
#define LINUX_SYS_GETSOCKOPT 55ull
#define LINUX_SYS_CLONE      56ull
#define LINUX_SYS_FORK       57ull
#define LINUX_SYS_EXECVE     59ull
#define LINUX_SYS_GETPID     39ull
#define LINUX_SYS_GETTID     186ull
#define LINUX_SYS_FUTEX      202ull
#define LINUX_SYS_SET_TID_ADDRESS 218ull
#define LINUX_SYS_CLOCK_GETTIME 228ull
#define LINUX_SYS_OPENAT     257ull
#define LINUX_SYS_READLINKAT 267ull
#define LINUX_SYS_SET_ROBUST_LIST 273ull
#define LINUX_SYS_PRLIMIT64  302ull
#define LINUX_SYS_GETRANDOM  318ull
#define LINUX_SYS_RSEQ       334ull
#define LINUX_SYS_ARCH_PRCTL 158ull
#define LINUX_SYS_GETPPID    110ull
#define LINUX_SYS_EXIT       60ull
#define LINUX_SYS_WAIT4      61ull
#define LINUX_SYS_FCNTL      72ull
#define LINUX_SYS_GETCWD     79ull
#define LINUX_SYS_CHDIR      80ull
#define LINUX_SYS_RENAME     82ull
#define LINUX_SYS_MKDIR      83ull
#define LINUX_SYS_RMDIR      84ull
#define LINUX_SYS_UNLINK     87ull
#define LINUX_SYS_GETUID     102ull
#define LINUX_SYS_GETGID     104ull
#define LINUX_SYS_GETEUID    107ull
#define LINUX_SYS_GETEGID    108ull
#define LINUX_SYS_NEWFSTATAT 262ull
#define LINUX_SYS_PIPE2      293ull
#define LINUX_SYS_EXIT_GROUP 231ull
#define LINUX_SYS_POLL       7ull
#define LINUX_SYS_ACCESS     21ull
#define LINUX_SYS_SCHED_YIELD 24ull
#define LINUX_SYS_MSYNC      26ull
#define LINUX_SYS_MADVISE    28ull
#define LINUX_SYS_DUP        32ull
#define LINUX_SYS_DUP2       33ull
#define LINUX_SYS_NANOSLEEP  35ull
#define LINUX_SYS_SOCKETPAIR 53ull
#define LINUX_SYS_KILL       62ull
#define LINUX_SYS_UNAME      63ull
#define LINUX_SYS_FSYNC      74ull
#define LINUX_SYS_FDATASYNC  75ull
#define LINUX_SYS_FTRUNCATE  77ull
#define LINUX_SYS_UMASK      95ull
#define LINUX_SYS_GETRLIMIT  97ull
#define LINUX_SYS_SYSINFO    99ull
#define LINUX_SYS_SETPGID    109ull
#define LINUX_SYS_GETPGRP    111ull
#define LINUX_SYS_SETSID     112ull
#define LINUX_SYS_GETPGID    121ull
#define LINUX_SYS_SIGALTSTACK 131ull
#define LINUX_SYS_PRCTL      157ull
#define LINUX_SYS_SETRLIMIT  160ull
#define LINUX_SYS_TKILL      200ull
#define LINUX_SYS_SCHED_SETAFFINITY 203ull
#define LINUX_SYS_SCHED_GETAFFINITY 204ull
#define LINUX_SYS_GETDENTS64 217ull
#define LINUX_SYS_CLOCK_GETRES 229ull
#define LINUX_SYS_CLOCK_NANOSLEEP 230ull
#define LINUX_SYS_TGKILL     234ull
#define LINUX_SYS_FACCESSAT  269ull
#define LINUX_SYS_PPOLL      271ull
#define LINUX_SYS_DUP3       292ull
#define LINUX_SYS_MEMBARRIER 324ull
#define LINUX_SYS_STATX      332ull
#define LINUX_SYS_FACCESSAT2 439ull

#define LINUX_AT_EMPTY_PATH 0x1000u

#define LINUX_EINVAL 22ull
#define LINUX_EAGAIN 11ull
#define LINUX_ENOSYS 38ull

/* futex operations (see linux/futex.h) */
#define LINUX_FUTEX_WAIT          0u
#define LINUX_FUTEX_WAKE          1u
#define LINUX_FUTEX_WAIT_BITSET   9u
#define LINUX_FUTEX_WAKE_BITSET   10u
#define LINUX_FUTEX_PRIVATE_FLAG  128u
#define LINUX_FUTEX_CLOCK_REALTIME 256u
#define LINUX_FUTEX_CMD_MASK      (~(LINUX_FUTEX_PRIVATE_FLAG | LINUX_FUTEX_CLOCK_REALTIME))

/* clone() thread flag (see linux/sched.h) */
#define LINUX_CLONE_VM 0x00000100ull

#define LINUX_ARCH_SET_FS 0x1002ull
#define LINUX_ARCH_GET_FS 0x1003ull

#define LINUX_AT_FDCWD ((uint64_t)-100ll)
#define LINUX_STAT_MODE_REG 0100000u
#define LINUX_STAT_MODE_DIR 0040000u
#define LINUX_STAT_MODE_PERM 0644u

typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime;
    int64_t st_atime_nsec;
    int64_t st_mtime;
    int64_t st_mtime_nsec;
    int64_t st_ctime;
    int64_t st_ctime_nsec;
    int64_t __unused[3];
} linux_stat_t;

typedef struct {
    int64_t tv_sec;
    uint32_t tv_nsec;
    int32_t __reserved;
} linux_statx_timestamp_t;

typedef struct {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __spare0;
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    linux_statx_timestamp_t stx_atime;
    linux_statx_timestamp_t stx_btime;
    linux_statx_timestamp_t stx_ctime;
    linux_statx_timestamp_t stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t stx_mnt_id;
    uint64_t __spare2;
    uint64_t __spare3[12];
} linux_statx_t;

#define LINUX_STATX_BASIC_STATS 0x000007ffu

typedef struct {
    int64_t uptime;
    uint64_t loads[3];
    uint64_t totalram;
    uint64_t freeram;
    uint64_t sharedram;
    uint64_t bufferram;
    uint64_t totalswap;
    uint64_t freeswap;
    uint16_t procs;
    uint16_t pad;
    uint64_t totalhigh;
    uint64_t freehigh;
    uint32_t mem_unit;
    char __reserved[256];
} linux_sysinfo_t;

static uint64_t linux_error(uint64_t err) {
    return (uint64_t)(0ull - err);
}

static uint64_t linux_arch_prctl(uint64_t code, uint64_t addr) {
    switch (code) {
    case LINUX_ARCH_SET_FS:
        scheduler_set_current_fs_base(addr);
        return 0u;
    case LINUX_ARCH_GET_FS:
        if (addr == 0u) {
            return linux_error(LINUX_EINVAL);
        }
        *(uint64_t *)(uintptr_t)addr = scheduler_current_fs_base();
        return 0u;
    default:
        return linux_error(LINUX_ENOSYS);
    }
}

static uint64_t linux_result(uint64_t value) {
    return (value == (uint64_t)-1) ? linux_error(LINUX_EINVAL) : value;
}

static uint64_t linux_bulk_identity(uint64_t mask, uint64_t fallback_syscall) {
    vex_bulk_result_t result;
    vex_bulk_req_t req;

    req.mask = mask;
    req.result = &result;
    if (vex_syscall_dispatch(VEX_SYS_BULK, (uint64_t)(uintptr_t)&req, 0, 0) != 0u) {
        return vex_syscall_dispatch(fallback_syscall, 0, 0, 0);
    }

    if ((mask & VEX_BULK_PID) != 0u && (result.valid_mask & VEX_BULK_PID) != 0u) {
        return result.pid;
    }
    if ((mask & VEX_BULK_UID) != 0u && (result.valid_mask & VEX_BULK_UID) != 0u) {
        return result.uid;
    }
    if ((mask & VEX_BULK_EUID) != 0u && (result.valid_mask & VEX_BULK_EUID) != 0u) {
        return result.euid;
    }

    return 0u;
}

static uint64_t linux_read(uint64_t fd, uint64_t buffer, uint64_t count) {
    vex_fd_io_req_t req;
    req.fd = (int)fd;
    req.buffer = (void *)(uintptr_t)buffer;
    req.count = (size_t)count;
    return vex_syscall_dispatch(VEX_SYS_READFD, (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t linux_write(uint64_t fd, uint64_t buffer, uint64_t count) {
    vex_fd_io_req_t req;
    req.fd = (int)fd;
    req.buffer = (void *)(uintptr_t)buffer;
    req.count = (size_t)count;
    return vex_syscall_dispatch(VEX_SYS_WRITEFD, (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t linux_mmap(uint64_t addr, uint64_t length, uint64_t prot, uint64_t flags, uint64_t fd, uint64_t offset) {
    vex_mmap_req_t req;
    req.addr = (void *)(uintptr_t)addr;
    req.length = (size_t)length;
    req.prot = (int)prot;
    req.flags = (int)flags;
    req.fd = (int)fd;
    req.offset = (off_t)offset;
    return vex_syscall_dispatch(VEX_SYS_MMAP, (uint64_t)(uintptr_t)&req, 0, 0);
}

static void fill_linux_stat_from_vex(linux_stat_t *linux_st, const struct stat *vex_st) {
    if (linux_st == NULL || vex_st == NULL) {
        return;
    }

    linux_st->st_dev = 1u;
    linux_st->st_ino = 1u;
    linux_st->st_nlink = 1u;
    linux_st->st_mode = (vex_st->st_mode != 0u ? vex_st->st_mode : LINUX_STAT_MODE_REG) | LINUX_STAT_MODE_PERM;
    linux_st->st_uid = vex_st->st_uid;
    linux_st->st_gid = vex_st->st_gid;
    linux_st->st_rdev = 0u;
    linux_st->st_size = (int64_t)vex_st->st_size;
    linux_st->st_blksize = vex_st->st_blksize != 0u ? (int64_t)vex_st->st_blksize : 4096;
    linux_st->st_blocks = vex_st->st_blocks != 0u ? (int64_t)vex_st->st_blocks : (int64_t)((vex_st->st_size + 511u) / 512u);
}

static uint64_t linux_stat_path(uint64_t path, uint64_t st, uint64_t which) {
    struct stat vex_st;
    linux_stat_t *linux_st = (linux_stat_t *)(uintptr_t)st;
    vex_path_stat_req_t req;
    req.path = (const char *)(uintptr_t)path;
    req.st = &vex_st;

    if (linux_st == NULL || req.path == NULL) {
        return linux_error(LINUX_EINVAL);
    }

    if (vex_syscall_dispatch(which, (uint64_t)(uintptr_t)&req, 0, 0) == (uint64_t)-1) {
        return linux_error(LINUX_EINVAL);
    }

    fill_linux_stat_from_vex(linux_st, &vex_st);
    return 0;
}

static uint64_t linux_fstat(uint64_t fd, uint64_t st) {
    struct stat vex_st;
    linux_stat_t *linux_st = (linux_stat_t *)(uintptr_t)st;

    if (linux_st == NULL) {
        return linux_error(LINUX_EINVAL);
    }

    if (vex_syscall_dispatch(VEX_SYS_FSTAT, fd, (uint64_t)(uintptr_t)&vex_st, 0) == (uint64_t)-1) {
        return linux_error(LINUX_EINVAL);
    }

    fill_linux_stat_from_vex(linux_st, &vex_st);
    return 0;
}

static uint64_t linux_pipe(uint64_t pipefd) {
    vex_pipe_req_t req;
    req.pipefd = (int *)(uintptr_t)pipefd;
    return vex_syscall_dispatch(VEX_SYS_PIPE, (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t linux_pread(uint64_t fd, uint64_t buffer, uint64_t count, uint64_t offset) {
    vex_rw_offset_req_t req;
    req.fd = (int)fd;
    req.buffer = (void *)(uintptr_t)buffer;
    req.count = (size_t)count;
    req.offset = (off_t)offset;
    return vex_syscall_dispatch(VEX_SYS_PREAD, (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t linux_pwrite(uint64_t fd, uint64_t buffer, uint64_t count, uint64_t offset) {
    vex_rw_offset_req_t req;
    req.fd = (int)fd;
    req.buffer = (void *)(uintptr_t)buffer;
    req.count = (size_t)count;
    req.offset = (off_t)offset;
    return vex_syscall_dispatch(VEX_SYS_PWRITE, (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t linux_mprotect(uint64_t addr, uint64_t length, uint64_t prot) {
    vex_memory_req_t req;
    req.addr = (void *)(uintptr_t)addr;
    req.length = (size_t)length;
    req.prot_or_advice = (int)prot;
    return vex_syscall_dispatch(VEX_SYS_MPROTECT, (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t linux_sockaddr_call(uint64_t fd, uint64_t addr, uint64_t addrlen, uint64_t which) {
    vex_sockaddr_req_t req;
    req.sockfd = (int)fd;
    req.addr = (const struct sockaddr *)(uintptr_t)addr;
    req.addrlen = (uint32_t)addrlen;
    return vex_syscall_dispatch(which, (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t linux_send_recv(uint64_t fd, uint64_t buffer, uint64_t length, uint64_t flags, uint64_t which) {
    vex_send_recv_req_t req;
    req.sockfd = (int)fd;
    req.buffer = (void *)(uintptr_t)buffer;
    req.length = (size_t)length;
    req.flags = (int)flags;
    return vex_syscall_dispatch(which, (uint64_t)(uintptr_t)&req, 0, 0);
}

static void compat_zero(void *dst, size_t length) {
    uint8_t *bytes = (uint8_t *)dst;
    for (size_t i = 0u; i < length; ++i) {
        bytes[i] = 0u;
    }
}

static uint64_t linux_poll(uint64_t fds, uint64_t nfds, int timeout) {
    vex_poll_req_t req;
    req.fds = (struct pollfd *)(uintptr_t)fds;
    req.count = (nfds_t)nfds;
    req.timeout = timeout;
    return vex_syscall_dispatch(VEX_SYS_POLL, (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t linux_nanosleep(uint64_t requested, uint64_t remaining) {
    vex_nanosleep_req_t req;
    req.requested = (const struct timespec *)(uintptr_t)requested;
    req.remaining = (struct timespec *)(uintptr_t)remaining;
    return linux_result(vex_syscall_dispatch(VEX_SYS_NANOSLEEP, (uint64_t)(uintptr_t)&req, 0, 0));
}

/* Only lethal signals are acted upon; signal delivery is otherwise unsupported,
   so non-fatal signals (including pthread cancellation) are quietly accepted. */
static uint64_t linux_signal_target(uint32_t pid, int sig) {
    switch (sig) {
    case 0:
        return 0u;
    case 3:
    case 6:
    case 9:
    case 11:
    case 15:
        return process_kill(pid) == 0 ? 0u : linux_error(LINUX_EINVAL);
    default:
        return 0u;
    }
}

static uint64_t linux_statx(uint64_t dirfd, uint64_t path_addr, uint64_t flags, uint64_t buf) {
    struct stat vex_st;
    linux_statx_t *sx = (linux_statx_t *)(uintptr_t)buf;
    const char *path = (const char *)(uintptr_t)path_addr;
    int ok = 0;

    if (sx == NULL) {
        return linux_error(LINUX_EINVAL);
    }

    if ((flags & LINUX_AT_EMPTY_PATH) != 0u || (path != NULL && path[0] == '\0')) {
        ok = vex_syscall_dispatch(VEX_SYS_FSTAT, dirfd, (uint64_t)(uintptr_t)&vex_st, 0) != (uint64_t)-1;
    } else if (dirfd == LINUX_AT_FDCWD && path != NULL) {
        vex_path_stat_req_t req;
        req.path = path;
        req.st = &vex_st;
        ok = vex_syscall_dispatch(VEX_SYS_STAT, (uint64_t)(uintptr_t)&req, 0, 0) != (uint64_t)-1;
    }

    if (!ok) {
        return linux_error(LINUX_EINVAL);
    }

    compat_zero(sx, sizeof(*sx));
    sx->stx_mask = LINUX_STATX_BASIC_STATS;
    sx->stx_blksize = vex_st.st_blksize != 0u ? (uint32_t)vex_st.st_blksize : 4096u;
    sx->stx_nlink = 1u;
    sx->stx_uid = vex_st.st_uid;
    sx->stx_gid = vex_st.st_gid;
    sx->stx_mode = (uint16_t)((vex_st.st_mode != 0u ? vex_st.st_mode : LINUX_STAT_MODE_REG) | LINUX_STAT_MODE_PERM);
    sx->stx_ino = 1u;
    sx->stx_size = (uint64_t)vex_st.st_size;
    sx->stx_blocks = vex_st.st_blocks != 0u ? (uint64_t)vex_st.st_blocks : (uint64_t)((vex_st.st_size + 511) / 512);
    return 0u;
}

uint64_t compat_linux_syscall(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    switch (number) {
    case LINUX_SYS_READ:
        return linux_result(linux_read(arg1, arg2, arg3));
    case LINUX_SYS_WRITE:
        return linux_result(linux_write(arg1, arg2, arg3));
    case LINUX_SYS_OPEN:
        return linux_result(vex_syscall_dispatch(VEX_SYS_OPEN, arg1, arg2, 0));
    case LINUX_SYS_CLOSE:
        return linux_result(vex_syscall_dispatch(VEX_SYS_CLOSE, arg1, 0, 0));
    case LINUX_SYS_STAT:
        return linux_stat_path(arg1, arg2, VEX_SYS_STAT);
    case LINUX_SYS_FSTAT:
        return linux_fstat(arg1, arg2);
    case LINUX_SYS_LSTAT:
        return linux_stat_path(arg1, arg2, VEX_SYS_LSTAT);
    case LINUX_SYS_LSEEK:
        return linux_result(vex_syscall_dispatch(VEX_SYS_LSEEK, arg1, arg2, arg3));
    case LINUX_SYS_MMAP:
        return linux_mmap(arg1, arg2, arg3, arg4, arg5, arg6);
    case LINUX_SYS_MUNMAP:
        {
            vex_memory_req_t req;
            req.addr = (void *)(uintptr_t)arg1;
            req.length = (size_t)arg2;
            req.prot_or_advice = 0;
            return vex_syscall_dispatch(VEX_SYS_MUNMAP, (uint64_t)(uintptr_t)&req, 0, 0);
        }
    case LINUX_SYS_MPROTECT:
        return linux_mprotect(arg1, arg2, arg3);
    case LINUX_SYS_BRK:
        return arg1 == 0u ? 0u : linux_error(LINUX_EINVAL);
    case LINUX_SYS_RT_SIGACTION:
    case LINUX_SYS_RT_SIGPROCMASK:
        return 0;
    case LINUX_SYS_IOCTL:
        {
            vex_ioctl_req_t req;
            req.fd = (int)arg1;
            req.request = (unsigned long)arg2;
            req.arg = (void *)(uintptr_t)arg3;
            return vex_syscall_dispatch(VEX_SYS_IOCTL, (uint64_t)(uintptr_t)&req, 0, 0);
        }
    case LINUX_SYS_PREAD64:
        return linux_pread(arg1, arg2, arg3, arg4);
    case LINUX_SYS_PWRITE64:
        return linux_pwrite(arg1, arg2, arg3, arg4);
    case LINUX_SYS_PIPE:
    case LINUX_SYS_PIPE2:
        return linux_result(linux_pipe(arg1));
    case LINUX_SYS_SOCKET:
        return linux_result(vex_syscall_dispatch(VEX_SYS_SOCKET, arg1, arg2, arg3));
    case LINUX_SYS_CONNECT:
        return linux_sockaddr_call(arg1, arg2, arg3, VEX_SYS_CONNECT);
    case LINUX_SYS_BIND:
        return linux_sockaddr_call(arg1, arg2, arg3, VEX_SYS_BIND);
    case LINUX_SYS_ACCEPT:
        {
            vex_accept_req_t req;
            req.sockfd = (int)arg1;
            req.addr = (struct sockaddr *)(uintptr_t)arg2;
            req.addrlen = (uint32_t *)(uintptr_t)arg3;
            return vex_syscall_dispatch(VEX_SYS_ACCEPT, (uint64_t)(uintptr_t)&req, 0, 0);
        }
    case LINUX_SYS_SENDTO:
        return linux_send_recv(arg1, arg2, arg3, arg4, VEX_SYS_SEND);
    case LINUX_SYS_RECVFROM:
        return linux_send_recv(arg1, arg2, arg3, arg4, VEX_SYS_RECV);
    case LINUX_SYS_LISTEN:
        return linux_result(vex_syscall_dispatch(VEX_SYS_LISTEN, arg1, arg2, 0));
    case LINUX_SYS_SETSOCKOPT:
    case LINUX_SYS_GETSOCKOPT:
        {
            vex_sockopt_req_t req;
            req.sockfd = (int)arg1;
            req.level = (int)arg2;
            req.option_name = (int)arg3;
            req.option_value = (void *)(uintptr_t)arg4;
            req.option_len = (uint32_t *)(uintptr_t)arg5;
            return vex_syscall_dispatch(number == LINUX_SYS_SETSOCKOPT ? VEX_SYS_SETSOCKOPT : VEX_SYS_GETSOCKOPT, (uint64_t)(uintptr_t)&req, 0, 0);
        }
    case LINUX_SYS_CLONE:
        if ((arg1 & LINUX_CLONE_VM) != 0u) {
            const uint32_t tid = process_clone_thread(arg2, arg1, arg3, arg4, arg5);
            return tid == 0u ? linux_error(LINUX_EINVAL) : (uint64_t)tid;
        }
    case LINUX_SYS_FORK:
        {
            const uint32_t child_pid = process_fork_current();
            return child_pid == 0u ? linux_error(LINUX_EINVAL) : (uint64_t)child_pid;
        }
    case LINUX_SYS_EXECVE:
        return elf_exec_current((const char *)(uintptr_t)arg1) == 0 ? 0u : linux_error(LINUX_EINVAL);
    case LINUX_SYS_ARCH_PRCTL:
        return linux_arch_prctl(arg1, arg2);
    case LINUX_SYS_GETPID:
        return linux_bulk_identity(VEX_BULK_PID, VEX_SYS_GETPID);
    case LINUX_SYS_GETTID:
        return scheduler_current_pid();
    case LINUX_SYS_GETPPID:
        return vex_syscall_dispatch(VEX_SYS_GETPPID, 0, 0, 0);
    case LINUX_SYS_GETUID:
    case LINUX_SYS_GETGID:
        return linux_bulk_identity(VEX_BULK_UID, VEX_SYS_GETUID);
    case LINUX_SYS_GETEUID:
    case LINUX_SYS_GETEGID:
        return linux_bulk_identity(VEX_BULK_EUID, VEX_SYS_GETEUID);
    case LINUX_SYS_WAIT4:
        {
            int *status = (int *)(uintptr_t)arg2;
            uint32_t reaped_pid = 0u;
            const int result = process_wait_pid((int32_t)arg1, status, (uint32_t)arg3, &reaped_pid);
            return result < 0 ? linux_error(LINUX_EINVAL) : (uint64_t)result;
        }
    case LINUX_SYS_FCNTL:
        return linux_result(vex_syscall_dispatch(VEX_SYS_FCNTL, arg1, arg2, arg3));
    case LINUX_SYS_FUTEX:
        {
            const uint32_t op = (uint32_t)arg2 & LINUX_FUTEX_CMD_MASK;
            switch (op) {
            case LINUX_FUTEX_WAIT:
            case LINUX_FUTEX_WAIT_BITSET:
                return futex_wait(arg1, (uint32_t)arg3) == 0 ? 0u : linux_error(LINUX_EAGAIN);
            case LINUX_FUTEX_WAKE:
            case LINUX_FUTEX_WAKE_BITSET:
                return (uint64_t)futex_wake(arg1, (uint32_t)arg3);
            default:
                return 0u;
            }
        }
    case LINUX_SYS_GETCWD:
        {
            vex_getcwd_req_t req;
            req.buffer = (char *)(uintptr_t)arg1;
            req.size = (size_t)arg2;
            return vex_syscall_dispatch(VEX_SYS_GETCWD, (uint64_t)(uintptr_t)&req, 0, 0) == 0u ? linux_error(LINUX_EINVAL) : arg1;
        }
    case LINUX_SYS_CHDIR:
        return linux_result(vex_syscall_dispatch(VEX_SYS_CHDIR, arg1, 0, 0));
    case LINUX_SYS_CLOCK_GETTIME:
        {
            struct timespec *ts = (struct timespec *)(uintptr_t)arg2;
            return clock_gettime((int)arg1, ts) == 0 ? 0u : linux_error(LINUX_EINVAL);
        }
    case LINUX_SYS_OPENAT:
        if (arg1 == LINUX_AT_FDCWD) {
            return linux_result(vex_syscall_dispatch(VEX_SYS_OPEN, arg2, arg3, 0));
        }
        return linux_error(LINUX_ENOSYS);
    case LINUX_SYS_RENAME:
        {
            vex_rename_req_t req;
            req.old_path = (const char *)(uintptr_t)arg1;
            req.new_path = (const char *)(uintptr_t)arg2;
            return linux_result(vex_syscall_dispatch(VEX_SYS_RENAME, (uint64_t)(uintptr_t)&req, 0, 0));
        }
    case LINUX_SYS_MKDIR:
        {
            vex_mkdir_req_t req;
            req.path = (const char *)(uintptr_t)arg1;
            req.mode = (uint32_t)arg2;
            return linux_result(vex_syscall_dispatch(VEX_SYS_MKDIR, (uint64_t)(uintptr_t)&req, 0, 0));
        }
    case LINUX_SYS_RMDIR:
        return linux_result(vex_syscall_dispatch(VEX_SYS_RMDIR, arg1, 0, 0));
    case LINUX_SYS_UNLINK:
        return linux_result(vex_syscall_dispatch(VEX_SYS_UNLINK, arg1, 0, 0));
    case LINUX_SYS_READLINKAT:
        if (arg1 == LINUX_AT_FDCWD) {
            vex_readlink_req_t req;
            req.path = (const char *)(uintptr_t)arg2;
            req.buffer = (char *)(uintptr_t)arg3;
            req.size = (size_t)arg4;
            return linux_result(vex_syscall_dispatch(VEX_SYS_READLINK, (uint64_t)(uintptr_t)&req, 0, 0));
        }
        return linux_error(LINUX_ENOSYS);
    case LINUX_SYS_NEWFSTATAT:
        return linux_stat_path(arg2, arg3, arg1 == LINUX_AT_FDCWD ? VEX_SYS_STAT : VEX_SYS_STAT);
    case LINUX_SYS_SET_TID_ADDRESS:
        process_set_clear_child_tid(arg1);
        return scheduler_current_pid();
    case LINUX_SYS_SET_ROBUST_LIST:
    case LINUX_SYS_PRLIMIT64:
        return 0u;
    case LINUX_SYS_GETRANDOM:
        {
            vex_getrandom_req_t req;
            req.buffer = (void *)(uintptr_t)arg1;
            req.length = (size_t)arg2;
            req.flags = (unsigned int)arg3;
            return linux_result(vex_syscall_dispatch(VEX_SYS_GETRANDOM, (uint64_t)(uintptr_t)&req, 0, 0));
        }
    case LINUX_SYS_RSEQ:
        return 0u;
    case LINUX_SYS_EXIT:
        process_exit_status((int)arg1);
        return 0;
    case LINUX_SYS_EXIT_GROUP:
        process_exit_group((int)arg1);
        return 0;
    case LINUX_SYS_POLL:
        return linux_result(linux_poll(arg1, arg2, (int)arg3));
    case LINUX_SYS_PPOLL:
        {
            int timeout = -1;
            const struct timespec *ts = (const struct timespec *)(uintptr_t)arg3;
            if (ts != NULL) {
                timeout = (int)(ts->tv_sec * 1000 + ts->tv_nsec / 1000000);
            }
            return linux_result(linux_poll(arg1, arg2, timeout));
        }
    case LINUX_SYS_MADVISE:
        {
            vex_memory_req_t req;
            req.addr = (void *)(uintptr_t)arg1;
            req.length = (size_t)arg2;
            req.prot_or_advice = (int)arg3;
            (void)vex_syscall_dispatch(VEX_SYS_MADVISE, (uint64_t)(uintptr_t)&req, 0, 0);
            return 0u;
        }
    case LINUX_SYS_MSYNC:
        return 0u;
    case LINUX_SYS_DUP:
        return linux_result(vex_syscall_dispatch(VEX_SYS_DUP, arg1, 0, 0));
    case LINUX_SYS_DUP2:
        return linux_result(vex_syscall_dispatch(VEX_SYS_DUP2, arg1, arg2, 0));
    case LINUX_SYS_DUP3:
        return linux_result(vex_syscall_dispatch(VEX_SYS_DUP2, arg1, arg2, 0));
    case LINUX_SYS_NANOSLEEP:
        return linux_nanosleep(arg1, arg2);
    case LINUX_SYS_CLOCK_NANOSLEEP:
        return linux_nanosleep(arg3, arg4);
    case LINUX_SYS_SCHED_YIELD:
        process_yield();
        return 0u;
    case LINUX_SYS_ACCESS:
        return linux_result(vex_syscall_dispatch(VEX_SYS_ACCESS, arg1, arg2, 0));
    case LINUX_SYS_FACCESSAT:
        if (arg1 == LINUX_AT_FDCWD) {
            return linux_result(vex_syscall_dispatch(VEX_SYS_ACCESS, arg2, arg3, 0));
        }
        return linux_error(LINUX_ENOSYS);
    case LINUX_SYS_FACCESSAT2:
        if (arg1 == LINUX_AT_FDCWD) {
            return linux_result(vex_syscall_dispatch(VEX_SYS_ACCESS, arg2, arg3, 0));
        }
        return linux_error(LINUX_ENOSYS);
    case LINUX_SYS_FTRUNCATE:
        return linux_result(vex_syscall_dispatch(VEX_SYS_FTRUNCATE, arg1, arg2, 0));
    case LINUX_SYS_FSYNC:
    case LINUX_SYS_FDATASYNC:
        return linux_result(vex_syscall_dispatch(VEX_SYS_FSYNC, arg1, 0, 0));
    case LINUX_SYS_UNAME:
        return linux_result(vex_syscall_dispatch(VEX_SYS_UNAME, arg1, 0, 0));
    case LINUX_SYS_SOCKETPAIR:
        {
            vex_socketpair_req_t req;
            req.domain = (int)arg1;
            req.type = (int)arg2;
            req.protocol = (int)arg3;
            req.sv = (int *)(uintptr_t)arg4;
            return linux_result(vex_syscall_dispatch(VEX_SYS_SOCKETPAIR, (uint64_t)(uintptr_t)&req, 0, 0));
        }
    case LINUX_SYS_SCHED_GETAFFINITY:
        {
            uint8_t *mask = (uint8_t *)(uintptr_t)arg3;
            size_t size = (size_t)arg2;
            if (mask == NULL || size == 0u) {
                return linux_error(LINUX_EINVAL);
            }
            if (size > 8u) {
                size = 8u;
            }
            compat_zero(mask, size);
            mask[0] = 0x1u;
            return (uint64_t)size;
        }
    case LINUX_SYS_SCHED_SETAFFINITY:
        return 0u;
    case LINUX_SYS_KILL:
        return linux_signal_target((uint32_t)arg1, (int)arg2);
    case LINUX_SYS_TKILL:
        return linux_signal_target((uint32_t)arg1, (int)arg2);
    case LINUX_SYS_TGKILL:
        return linux_signal_target((uint32_t)arg2, (int)arg3);
    case LINUX_SYS_PRCTL:
    case LINUX_SYS_SIGALTSTACK:
    case LINUX_SYS_MEMBARRIER:
    case LINUX_SYS_UMASK:
    case LINUX_SYS_SETPGID:
    case LINUX_SYS_SETSID:
        return 0u;
    case LINUX_SYS_GETPGRP:
    case LINUX_SYS_GETPGID:
        return scheduler_current_pid();
    case LINUX_SYS_GETRLIMIT:
    case LINUX_SYS_SETRLIMIT:
        {
            uint64_t *rlimit = (uint64_t *)(uintptr_t)arg2;
            if (number == LINUX_SYS_GETRLIMIT && rlimit != NULL) {
                rlimit[0] = ~0ull;
                rlimit[1] = ~0ull;
            }
            return 0u;
        }
    case LINUX_SYS_SYSINFO:
        {
            linux_sysinfo_t *info = (linux_sysinfo_t *)(uintptr_t)arg1;
            if (info == NULL) {
                return linux_error(LINUX_EINVAL);
            }
            compat_zero(info, sizeof(*info));
            info->totalram = 256ull * 1024ull * 1024ull;
            info->freeram = 128ull * 1024ull * 1024ull;
            info->procs = 1u;
            info->mem_unit = 1u;
            return 0u;
        }
    case LINUX_SYS_CLOCK_GETRES:
        {
            struct timespec *ts = (struct timespec *)(uintptr_t)arg2;
            if (ts != NULL) {
                ts->tv_sec = 0;
                ts->tv_nsec = 1;
            }
            return 0u;
        }
    case LINUX_SYS_GETDENTS64:
        return 0u;
    case LINUX_SYS_STATX:
        return linux_statx(arg1, arg2, arg3, arg5);
    default:
        return linux_error(LINUX_ENOSYS);
    }
}
