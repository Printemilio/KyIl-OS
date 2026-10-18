#include <unos/compat.h>
#include <unos/vex_legacy_compat.h>

#ifndef VEX_BROWSER_DIAGNOSTIC
#define VEX_BROWSER_DIAGNOSTIC 0
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/debug_log.h>
#include <unos/boot_requests.h>
#include <unos/events.h>
#include <unos/disk.h>
#include <unos/elf.h>
#include <unos/fs.h>
#include <unos/heap.h>
#include <unos/irq.h>
#include <unos/message_ipc.h>
#include <unos/pmm.h>
#include <unos/process_memory.h>
#include <unos/usercopy.h>
#include <unos/security.h>
#include <unos/scheduler.h>
#include <unos/compat_linux_futex_pi.h>
#include <unos/vex_abi.h>
#include <unos/vex_syscall.h>
#include <unos/vmm.h>
#include <unos/memory_diagnostic.h>
#include <unos/udp.h>
extern int vex_socket_receive_fault(int fd,udp_transport_fault_t *fault);
extern int vex_socket_fault_errno(const udp_transport_fault_t *fault);

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
#define LINUX_SYS_RT_SIGRETURN 15ull
#define LINUX_SYS_IOCTL      16ull
#define LINUX_SYS_PREAD64    17ull
#define LINUX_SYS_PWRITE64   18ull
#define LINUX_SYS_PIPE       22ull
#define LINUX_SYS_SOCKET     41ull
#define LINUX_SYS_GETSOCKNAME 51ull
#define LINUX_SYS_GETPEERNAME 52ull
#define LINUX_SYS_CONNECT    42ull
#define LINUX_SYS_ACCEPT     43ull
#define LINUX_SYS_SENDTO     44ull
#define LINUX_SYS_RECVFROM   45ull
#define LINUX_SYS_SENDMSG    46ull
#define LINUX_SYS_RECVMSG    47ull
#define LINUX_SYS_SENDMMSG   307ull
#define LINUX_SYS_BIND       49ull
#define LINUX_SYS_LISTEN     50ull
#define LINUX_SYS_SETSOCKOPT 54ull
#define LINUX_SYS_GETSOCKOPT 55ull
#define LINUX_SYS_CLONE      56ull
#define LINUX_SYS_FORK       57ull
#define LINUX_SYS_VFORK     58ull
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
#define LINUX_SYS_EVENTFD   284ull
#define LINUX_SYS_EVENTFD2  290ull
#define LINUX_SYS_TIMERFD_CREATE 283ull
#define LINUX_SYS_TIMERFD_SETTIME 286ull
#define LINUX_SYS_TIMERFD_GETTIME 287ull
#define LINUX_SYS_MKDIRAT 258ull
#define LINUX_SYS_EXIT_GROUP 231ull
#define LINUX_SYS_POLL       7ull
#define LINUX_SYS_READV      19ull
#define LINUX_SYS_WRITEV     20ull
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
#define LINUX_SYS_GETTIMEOFDAY 96ull
#define LINUX_SYS_GETRLIMIT  97ull
#define LINUX_SYS_SYSINFO    99ull
#define LINUX_SYS_STATFS     137ull
#define LINUX_SYS_FSTATFS    138ull
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
#define LINUX_SYS_SCHED_SETSCHEDULER 144ull
#define LINUX_SYS_SCHED_GETSCHEDULER 145ull
#define LINUX_SYS_SCHED_GET_PRIORITY_MAX 146ull
#define LINUX_SYS_SCHED_GET_PRIORITY_MIN 147ull
#define LINUX_SYS_MLOCK      149ull
#define LINUX_SYS_MUNLOCK    150ull
#define LINUX_SYS_TGKILL     234ull
#define LINUX_SYS_FACCESSAT  269ull
#define LINUX_SYS_PPOLL      271ull
#define LINUX_SYS_DUP3       292ull
#define LINUX_SYS_MEMBARRIER 324ull
#define LINUX_SYS_STATX      332ull
#define LINUX_SYS_FACCESSAT2 439ull
#define LINUX_SYS_CLONE3      435ull

#define LINUX_AT_EMPTY_PATH 0x1000u
#define LINUX_ENOENT 2u

#define LINUX_EINVAL 22ull
#define LINUX_EFAULT 14ull
#define LINUX_EBADF 9ull
#define LINUX_ENOMEM 12ull
#define LINUX_EACCES 13ull
#define LINUX_EAGAIN 11ull
#define LINUX_EMSGSIZE 90ull
#define LINUX_EOPNOTSUPP 95ull
#define LINUX_ENOSYS 38ull
#define LINUX_EPERM 1ull
#define LINUX_ESRCH 3ull
#define LINUX_ENOTSUP 95ull
#define LINUX_EMFILE 24ull

#define LINUX_SCHED_OTHER 0u
#define LINUX_SCHED_FIFO 1u
#define LINUX_SCHED_RR 2u
#define LINUX_SCHED_RESET_ON_FORK 0x40000000u
#define LINUX_RLIMIT_STACK 3u

/* syscall_entry.S records the user return context in a compact frame rather
 * than interrupt_frame_t.  RCX (the user RIP saved by SYSCALL) is at +128. */
static uint64_t linux_current_user_rip(void) {
    process_t *process = scheduler_current_process();
    return process == NULL || process->syscall_frame_rsp == 0u
        ? 0u : *(const volatile uint64_t *)(uintptr_t)(process->syscall_frame_rsp + 128u);
}

/* The SYSCALL frame is kept on the kernel stack.  These accessors deliberately
 * read only the saved user context; callers must validate any user address
 * before dereferencing it. */
static uint64_t linux_current_user_rsp(void) {
    process_t *process = scheduler_current_process();
    return process == NULL || process->syscall_frame_rsp == 0u
        ? 0u : *(const volatile uint64_t *)(uintptr_t)(process->syscall_frame_rsp + 136u);
}

static uint64_t linux_current_user_rbp(void) {
    process_t *process = scheduler_current_process();
    return process == NULL || process->syscall_frame_rsp == 0u
        ? 0u : *(const volatile uint64_t *)(uintptr_t)(process->syscall_frame_rsp + 96u);
}

#define LINUX_SIGUSR1 10
#define LINUX_SIGBUS 7
#define LINUX_SIGABRT 6
#define LINUX_SIGSEGV 11
#define LINUX_SIGPIPE 13
#define LINUX_SIGCANCEL 32
#define LINUX_SIGSETXID 33
#define LINUX_EVENT_SIGCANCEL 2u
#define LINUX_EVENT_SIGSETXID 3u
#define LINUX_SA_SIGINFO 4ull
#define LINUX_SA_RESTORER 0x04000000ull
#define LINUX_SA_RESTART 0x10000000ull
#define LINUX_SA_NODEFER 0x40000000ull
#define LINUX_SA_RESETHAND 0x80000000ull
#define LINUX_SA_ONSTACK 0x08000000ull
#define LINUX_SIG_BLOCK 0u
#define LINUX_SIG_UNBLOCK 1u
#define LINUX_SIG_SETMASK 2u
#define LINUX_SEGV_MAPERR 1
#define LINUX_SEGV_ACCERR 2
#define LINUX_SI_KERNEL 128
#define LINUX_BUS_ADRALN 1
#define LINUX_BUS_ADRERR 2
#define LINUX_UCONTEXT_SIZE 968u
#define LINUX_UC_GREGS_OFFSET 40u
#define LINUX_UC_FPREGS_OFFSET 224u
#define LINUX_UC_SIGMASK_OFFSET 296u
#define LINUX_UC_FPSTATE_OFFSET 424u
#define LINUX_FPSTATE_SIZE 512u

/* x86-64 kernel sigaction ABI.  The POSIX-facing layout is deliberately kept
 * private to this adapter; the native event layer stores only VexOS event
 * registrations. */
typedef struct linux_kernel_sigaction {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    /* x86-64 rt_sigaction has a single 64-bit kernel sigset here.  The
       userspace syscall also passes sigsetsize=8; reading another word
       would consume unrelated stack data past the ABI object. */
    uint64_t mask[1];
} linux_kernel_sigaction_t;
_Static_assert(sizeof(linux_kernel_sigaction_t) == 32u,
               "x86-64 rt_sigaction kernel ABI must remain 32 bytes");

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
#define LINUX_CLONE_VFORK 0x00004000ull
#define LINUX_CLONE_THREAD 0x00010000ull
#define LINUX_CLONE_CLEAR_SIGHAND 0x100000000ull
#define LINUX_CLONE_PARENT_SETTID 0x00100000ull
#define LINUX_CLONE_CHILD_CLEARTID 0x00200000ull
#define LINUX_CLONE_CHILD_SETTID 0x01000000ull
#define LINUX_CLONE_SETTLS_FLAG 0x00080000ull
#define LINUX_CLONE_SIGCHLD 17ull
#define LINUX_SYSCALL_FRAME_SIZE 144ull

/* Linux clone3() argument ABI.  Keep this private to the compatibility
 * adapter; native VexOS process creation remains process_fork_current(). */
typedef struct linux_clone_args {
    uint64_t flags;
    uint64_t pidfd;
    uint64_t child_tid;
    uint64_t parent_tid;
    uint64_t exit_signal;
    uint64_t stack;
    uint64_t stack_size;
    uint64_t tls;
    uint64_t set_tid;
    uint64_t set_tid_size;
    uint64_t cgroup;
} linux_clone_args_t;

#define LINUX_O_NONBLOCK 0x00000800ull
#define LINUX_O_CLOEXEC  0x00080000ull

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
    /* Linux x86-64 exposes a 0x70-byte sysinfo record.  The compatibility
     * layout previously reserved 256 bytes here, so sysinfo() zeroed 0x148
     * bytes into callers' stacks and overwrote stack-protector canaries. */
    char __reserved[4];
} linux_sysinfo_t;

_Static_assert(sizeof(linux_sysinfo_t) == 0x70u,
               "Linux x86-64 sysinfo ABI size must remain 112 bytes");

typedef struct {
    int64_t f_type;
    int64_t f_bsize;
    int64_t f_blocks;
    int64_t f_bfree;
    int64_t f_bavail;
    int64_t f_files;
    int64_t f_ffree;
    int32_t f_fsid[2];
    int64_t f_namelen;
    int64_t f_frsize;
    int64_t f_flags;
    int64_t f_spare[4];
} linux_statfs_t;

#define LINUX_VEXFS_MAGIC 0x56455846ll /* "VEXF" */

static uint64_t linux_error(uint64_t err) {
    return (uint64_t)(0ull - err);
}

static int linux_wait_status_from_vex(int status) {
    if (status >= 128) {
        return status - 128;
    }
    return (status & 0xff) << 8;
}

static void debug_log_u64_dec(uint64_t value) {
    char digits[21];
    size_t count = 0u;

    if (value == 0u) {
        debug_log_puts("0");
        return;
    }
    while (value != 0u && count < sizeof(digits) - 1u) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    digits[count] = '\0';
    for (size_t i = 0u; i < count / 2u; ++i) {
        const char tmp = digits[i];
        digits[i] = digits[count - 1u - i];
        digits[count - 1u - i] = tmp;
    }
    debug_log_puts(digits);
}

static void debug_log_u64_hex(uint64_t value) {
    static const char hex[] = "0123456789abcdef";
    char digits[17];
    for (unsigned int i = 0u; i < 16u; ++i) {
        digits[15u - i] = hex[value & 0xfu];
        value >>= 4u;
    }
    digits[16] = '\0';
    debug_log_puts(digits);
}

/*
 * Bounded tracing for the Linux spawn handshake.  This is deliberately kept
 * in the compatibility adapter: it records what Linux callers observe
 * without changing the native process model or the values returned by a
 * syscall.  The cap prevents a WebKit run from evicting the useful early
 * clone/exec/error-pipe events from the serial ring.
 */
static uint32_t linux_spawn_trace_count;
static uint32_t linux_spawn_trace_owner;
static uint8_t linux_spawn_trace_active;

/* Short diagnostic window for the WebKit platform-threading probe.  Keep the
 * trace in the Linux adapter so it records the ABI values crossing the
 * boundary without changing native event or scheduler state. */
static uint32_t linux_signal_trace_count;
static uint32_t linux_signal_trace_owner;
#define LINUX_SIGNAL_TRACE_LIMIT 512u
static uint32_t linux_sync_fault_trace_count;
static uint32_t linux_sync_return_trace_count;
#define LINUX_SYNC_FAULT_TRACE_LIMIT 256u

/* A denied Linux RWX request opens a short, per-process memory trace window.
 * This captures the allocator's immediate fallback and later permission or
 * unmap operations without tracing unrelated processes or the whole VM. */
#define LINUX_MEMORY_WATCH_COUNT 8u
#define LINUX_MEMORY_WATCH_EVENTS 48u
typedef struct linux_memory_watch {
    uint32_t pid;
    uint32_t events;
    uint64_t denied_length;
    uint64_t candidate_addr;
    uint64_t candidate_length;
    uint64_t candidate_fd;
    uint64_t candidate_offset;
    uint8_t active;
} linux_memory_watch_t;
static linux_memory_watch_t linux_memory_watches[LINUX_MEMORY_WATCH_COUNT];
static uint32_t linux_memory_watch_next;
static uint32_t linux_wk2_mmap_trace_count;

static uint64_t linux_memory_range_end(uint64_t start, uint64_t length) {
    return length > UINT64_MAX - start ? UINT64_MAX : start + length;
}

static int linux_memory_ranges_overlap(uint64_t a, uint64_t a_length,
                                       uint64_t b, uint64_t b_length) {
    if (a_length == 0u || b_length == 0u) return 0;
    return a < linux_memory_range_end(b, b_length) &&
        b < linux_memory_range_end(a, a_length);
}

static void linux_memory_trace(const char *op, uint64_t addr, uint64_t length,
                               uint64_t prot, uint64_t flags, uint64_t fd,
                               uint64_t offset, uint64_t result) {
    const uint32_t pid = scheduler_current_pid();
    const uint64_t wx = (prot & (PROT_WRITE | PROT_EXEC)) ==
        (PROT_WRITE | PROT_EXEC);
    const int is_mmap = op[0] == 'm' && op[1] == 'm' && op[2] == 'a' &&
                        op[3] == 'p' && op[4] == '\0';
    const int is_mprotect = op[0] == 'm' && op[1] == 'p' && op[2] == 'r' &&
                            op[3] == 'o' && op[4] == 't';
    linux_memory_watch_t *watch = NULL;
    int candidate_mapping = 0;

    for (uint32_t i = 0u; i < LINUX_MEMORY_WATCH_COUNT; ++i) {
        if (linux_memory_watches[i].active != 0u &&
            linux_memory_watches[i].pid == pid) {
            watch = &linux_memory_watches[i];
            break;
        }
    }
    if (watch == NULL && wx != 0u && length >= (16ull * 1024ull * 1024ull) &&
        result == linux_error(LINUX_EACCES)) {
        watch = &linux_memory_watches[linux_memory_watch_next++ %
                                      LINUX_MEMORY_WATCH_COUNT];
        memset(watch, 0, sizeof(*watch));
        watch->pid = pid;
        watch->denied_length = length;
        watch->active = 1u;
    }
    if (watch == NULL || watch->events >= LINUX_MEMORY_WATCH_EVENTS) return;

    if (is_mmap && watch->candidate_addr == 0u &&
        prot == (PROT_READ | PROT_WRITE) && (flags & MAP_ANONYMOUS) != 0u &&
        length >= watch->denied_length && result < (uint64_t)-4095ll) {
        watch->candidate_addr = result;
        watch->candidate_length = length;
        watch->candidate_fd = fd;
        watch->candidate_offset = offset;
        candidate_mapping = 1;
    }

    {
        const int overlaps = linux_memory_ranges_overlap(addr, length,
            watch->candidate_addr, watch->candidate_length);
        const int exec_mapping = is_mmap && (prot & PROT_EXEC) != 0u;
        /* Keep the window about JIT allocation, not unrelated allocator and
           file-backed traffic.  A non-overlapping executable mmap is kept so
           a dual alias of the candidate backing remains visible. */
        if (wx == 0u && overlaps == 0 && exec_mapping == 0 &&
            candidate_mapping == 0 &&
            !(is_mprotect && (prot & PROT_EXEC) != 0u)) {
            return;
        }
    }

    ++watch->events;
    {
        const uint64_t rip = linux_current_user_rip();
        const uint64_t rsp = linux_current_user_rsp();
        uint64_t frame = linux_current_user_rbp();
        const int overlaps = linux_memory_ranges_overlap(addr, length,
            watch->candidate_addr, watch->candidate_length);
        debug_log_puts("linux-memory-watch: op=");
        debug_log_puts(op);
        debug_log_puts(" pid=");
        debug_log_u64_dec(pid);
        debug_log_puts(" tid=");
        debug_log_u64_dec(scheduler_current_tid());
        debug_log_puts(" event=");
        debug_log_u64_dec(watch->events);
        debug_log_puts(" candidate=");
        debug_log_u64_dec((uint64_t)(unsigned int)overlaps);
        debug_log_puts(" rip=0x");
        debug_log_u64_hex(rip);
        debug_log_puts(" rsp=0x");
        debug_log_u64_hex(rsp);
        debug_log_puts(" rbp=0x");
        debug_log_u64_hex(frame);
        debug_log_puts(" addr=0x");
        debug_log_u64_hex(addr);
        debug_log_puts(" len=0x");
        debug_log_u64_hex(length);
        debug_log_puts(" prot=0x");
        debug_log_u64_hex(prot);
        debug_log_puts(" flags=0x");
        debug_log_u64_hex(flags);
        debug_log_puts(" fd=0x");
        debug_log_u64_hex(fd);
        debug_log_puts(" off=0x");
        debug_log_u64_hex(offset);
        debug_log_puts(" result=0x");
        debug_log_u64_hex(result);
        if (rsp != 0u && vex_syscall_user_range_valid(rsp, 32u, 0)) {
            debug_log_puts(" stack0=0x");
            debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)rsp);
            debug_log_puts(" stack8=0x");
            debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)(rsp + 8u));
            debug_log_puts(" stack16=0x");
            debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)(rsp + 16u));
            debug_log_puts(" stack24=0x");
            debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)(rsp + 24u));
        }
        /* Keep a short raw stack window for only the first denied RWX request
           and its first fallback.  This avoids guessing an ABI-dependent
           frame offset while still allowing return addresses to be symbolized. */
        if (watch->events <= 2u && rsp <= UINT64_MAX - 264u &&
            vex_syscall_user_range_valid(rsp, 264u, 0)) {
            for (uint64_t stack_offset = 32u; stack_offset <= 256u;
                 stack_offset += sizeof(uint64_t)) {
                debug_log_puts(" stack+");
                debug_log_u64_dec(stack_offset);
                debug_log_puts("=0x");
                debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)
                                  (rsp + stack_offset));
            }
        }
        /* Record a bounded frame-pointer chain alongside the raw syscall
           stack.  Validate each pair before reading; optimized frames may
           stop the chain early, which is diagnostic data, not an error. */
        for (uint32_t depth = 0u; depth < 6u && frame != 0u; ++depth) {
            uint64_t previous;
            uint64_t return_address;
            if (frame >= VMM_USER_ADDRESS_LIMIT ||
                !vex_syscall_user_range_valid(frame, 16u, 0)) {
                break;
            }
            previous = *(const volatile uint64_t *)(uintptr_t)frame;
            return_address = *(const volatile uint64_t *)(uintptr_t)(frame + 8u);
            debug_log_puts(" frame");
            debug_log_u64_dec(depth);
            debug_log_puts("=0x");
            debug_log_u64_hex(return_address);
            if (previous <= frame || previous - frame > (1024ull * 1024ull)) {
                break;
            }
            frame = previous;
        }
        debug_log_puts("\n");
    }
}

static void linux_signal_trace(const char *event, uint64_t a, uint64_t b,
                               uint64_t c, uint64_t d, uint64_t result) {
    if (!VEX_COMPAT_TRACE) return;
    const uint32_t owner = scheduler_current_pid();
    /* Futex transitions already have their own bounded trace. Repeating
     * them here resets this window whenever threads alternate. */
    if (strcmp(event, "futex") == 0) return;
    /* Keep a bounded window per Linux process.  A busy parent must not
       consume the diagnostic budget before a child reaches its fatal path. */
    if (linux_signal_trace_owner != owner) {
        linux_signal_trace_owner = owner;
        linux_signal_trace_count = 0u;
    }
    if (linux_signal_trace_count >= LINUX_SIGNAL_TRACE_LIMIT) return;
    ++linux_signal_trace_count;
    debug_log_puts("linux-signal-call: ");
    debug_log_puts(event);
    debug_log_puts(" pid=");
    debug_log_u64_dec(scheduler_current_pid());
    debug_log_puts(" tid=");
    debug_log_u64_dec(scheduler_current_tid());
    debug_log_puts(" rip=0x");
    debug_log_u64_hex(linux_current_user_rip());
    debug_log_puts(" fs=0x");
    debug_log_u64_hex(scheduler_current_fs_base());
    debug_log_puts(" a=");
    debug_log_u64_dec(a);
    debug_log_puts(" b=");
    debug_log_u64_dec(b);
    debug_log_puts(" c=");
    debug_log_u64_dec(c);
    debug_log_puts(" d=");
    debug_log_u64_dec(d);
    debug_log_puts(" result=");
    debug_log_u64_dec(result);
    debug_log_puts(" errno=");
    debug_log_u64_dec((uint64_t)(unsigned int)*ladybird_errno_location());
    debug_log_puts("\n");
}

/* Context trace for the userspace loop observed before web_view creation.
 * It is intentionally narrower than the general syscall trace: only a
 * FUTEX_WAKE with a large count (the pattern used by GLib/WTF broadcast
 * notifications) is sampled, and at most eight samples are emitted per
 * process.  The stack is read only after the normal user-range validator has
 * accepted it, so this diagnostic cannot turn a bad userspace stack into a
 * kernel dereference. */
static uint32_t linux_futex_context_trace_owner;
static uint32_t linux_futex_context_trace_count;
static uint32_t linux_futex_context_trace_total;
#define LINUX_FUTEX_CONTEXT_TRACE_LIMIT 24u

static void linux_futex_context_trace(uint64_t uaddr, uint64_t op,
                                      uint64_t count, uint64_t result) {
    if (!VEX_COMPAT_TRACE) return;
    const uint32_t owner = scheduler_current_pid();
    if ((op & LINUX_FUTEX_CMD_MASK) != LINUX_FUTEX_WAKE || count < 0x100000u) {
        return;
    }
    if (linux_futex_context_trace_owner != owner) {
        linux_futex_context_trace_owner = owner;
        linux_futex_context_trace_count = 0u;
        linux_futex_context_trace_total = 0u;
    }
    ++linux_futex_context_trace_total;
    /* Keep the first two calls for the startup path, then sample the same
     * userspace loop periodically so the trace also covers the state after
     * WebKit has created its view backend. */
    if (linux_futex_context_trace_total > 2u &&
        (linux_futex_context_trace_total & 31u) != 0u) {
        return;
    }
    if (linux_futex_context_trace_count >= LINUX_FUTEX_CONTEXT_TRACE_LIMIT) {
        return;
    }
    ++linux_futex_context_trace_count;

    const uint64_t rip = linux_current_user_rip();
    const uint64_t rsp = linux_current_user_rsp();
    uint64_t rbp = linux_current_user_rbp();
    debug_log_puts("linux-futex-context: sample=");
    debug_log_u64_dec(linux_futex_context_trace_total);
    debug_log_puts(" pid=");
    debug_log_u64_dec(scheduler_current_pid());
    debug_log_puts(" tid=");
    debug_log_u64_dec(scheduler_current_tid());
    debug_log_puts(" op=");
    debug_log_u64_dec(op);
    debug_log_puts(" addr=0x");
    debug_log_u64_hex(uaddr);
    debug_log_puts(" count=");
    debug_log_u64_dec(count);
    debug_log_puts(" result=");
    debug_log_u64_dec(result);
    debug_log_puts(" rip=0x");
    debug_log_u64_hex(rip);
    debug_log_puts(" rsp=0x");
    debug_log_u64_hex(rsp);
    debug_log_puts(" rbp=0x");
    debug_log_u64_hex(rbp);
    debug_log_puts(" fs=0x");
    debug_log_u64_hex(scheduler_current_fs_base());
    debug_log_puts("\n");

    /* The return address immediately above the syscall's user RSP and the
     * first words of the active frame give a bounded, ABI-neutral view even
     * when frame pointers were omitted. */
    if (vex_syscall_user_range_valid(rsp, 128u, 0)) {
        for (unsigned int word = 0u; word < 16u; ++word) {
            debug_log_puts("linux-futex-stack: +");
            debug_log_u64_hex((uint64_t)word * 8u);
            debug_log_puts("=0x");
            debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)
                              (rsp + (uint64_t)word * 8u));
            debug_log_puts("\n");
        }
    } else {
        debug_log_puts("linux-futex-stack: invalid\n");
    }

    /* g_cond_broadcast is called by GLib's g_once_init_leave.  With the
     * SysV frame layout, g_cond_broadcast's saved RBX is at caller-RBP+16;
     * g_once_init_leave keeps the once-object pointer in RBX.  Sampling that
     * object lets us distinguish a legitimate sequence of different once
     * initializers from one guard being observed as zero repeatedly. */
    if (rbp <= UINT64_MAX - 24u &&
        vex_syscall_user_range_valid(rbp + 16u, 8u, 0)) {
        const uint64_t once_object = *(const volatile uint64_t *)(uintptr_t)(rbp + 16u);
        uint64_t once_value = 0u;
        if (once_object != 0u && once_object <= UINT64_MAX - 8u &&
            vex_syscall_user_range_valid(once_object, 8u, 0)) {
            once_value = *(const volatile uint64_t *)(uintptr_t)once_object;
        }
        debug_log_puts("linux-futex-once: object=0x");
        debug_log_u64_hex(once_object);
        debug_log_puts(" value=0x");
        debug_log_u64_hex(once_value);
        debug_log_puts("\n");
    }

    /* Walk a conventional SysV RBP chain when present.  Every link and its
     * return address is validated before being read; malformed chains simply
     * terminate the diagnostic. */
    for (unsigned int depth = 0u; depth < 8u; ++depth) {
        uint64_t next_rbp;
        uint64_t ret;
        if ((rbp & 7u) != 0u || rbp < 16u ||
            !vex_syscall_user_range_valid(rbp, 16u, 0)) {
            break;
        }
        next_rbp = *(const volatile uint64_t *)(uintptr_t)rbp;
        ret = *(const volatile uint64_t *)(uintptr_t)(rbp + 8u);
        debug_log_puts("linux-futex-bt: depth=");
        debug_log_u64_dec(depth);
        debug_log_puts(" rbp=0x");
        debug_log_u64_hex(rbp);
        debug_log_puts(" ret=0x");
        debug_log_u64_hex(ret);
        debug_log_puts("\n");
        if (next_rbp <= rbp || next_rbp - rbp > (1ull << 20)) {
            break;
        }
        /* Keep the next iteration's validation separate from the arithmetic
         * above so an adversarial user pointer cannot wrap the walk. */
        rbp = next_rbp;
    }
}

static void linux_signal_action_trace(uint64_t sig, uint64_t act_addr,
                                      uint64_t old_addr, uint64_t sigset_size,
                                      uint64_t result) {
    if (!VEX_COMPAT_TRACE) return;
    linux_kernel_sigaction_t action;
    linux_kernel_sigaction_t old_action;
    memset(&action, 0, sizeof(action));
    memset(&old_action, 0, sizeof(old_action));
    if (act_addr != 0u && vex_syscall_user_range_valid(
            act_addr, sizeof(action), 0)) {
        memcpy(&action, (const void *)(uintptr_t)act_addr, sizeof(action));
    }
    if (old_addr != 0u && vex_syscall_user_range_valid(
            old_addr, sizeof(old_action), 1)) {
        memcpy(&old_action, (const void *)(uintptr_t)old_addr, sizeof(old_action));
    }
    linux_signal_trace("rt_sigaction", sig, sigset_size,
                       action.handler, action.flags, result);
    if (linux_signal_trace_count >= LINUX_SIGNAL_TRACE_LIMIT) return;
    debug_log_puts("linux-signal-action: restorer=");
    debug_log_u64_hex(action.restorer);
    debug_log_puts(" mask0=");
    debug_log_u64_hex(action.mask[0]);
    debug_log_puts(" old_handler=");
    debug_log_u64_hex(old_action.handler);
    debug_log_puts(" old_flags=");
    debug_log_u64_hex(old_action.flags);
    debug_log_puts("\n");
}

static void linux_signal_mask_trace(uint64_t how, uint64_t set_addr,
                                    uint64_t old_addr, uint64_t sigset_size,
                                    uint64_t result) {
    if (!VEX_COMPAT_TRACE) return;
    uint64_t set = 0u;
    uint64_t old = 0u;
    if (set_addr != 0u && vex_syscall_user_range_valid(set_addr, sizeof(set), 0)) {
        memcpy(&set, (const void *)(uintptr_t)set_addr, sizeof(set));
    }
    if (old_addr != 0u && vex_syscall_user_range_valid(old_addr, sizeof(old), 1)) {
        memcpy(&old, (const void *)(uintptr_t)old_addr, sizeof(old));
    }
    linux_signal_trace("rt_sigprocmask", how, sigset_size, set, old, result);
}

static void linux_spawn_trace(const char *event, uint64_t a, uint64_t b,
                              uint64_t c, uint64_t result) {
    if (!VEX_COMPAT_TRACE && !VEX_BROWSER_DIAGNOSTIC) return;
    const uint8_t is_clone3 = event != NULL && event[0] == 'c' && event[1] == 'l' &&
        event[2] == 'o' && event[3] == 'n' && event[4] == 'e' &&
        event[5] == '3' && event[6] == '.';
    const uint8_t is_spawn_dup = event != NULL && event[0] == 'f' &&
        event[1] == 'c' && event[2] == 'n' && event[3] == 't' &&
        event[4] == 'l' && b == F_DUPFD_CLOEXEC;

    /* Start a fresh bounded window for every posix_spawn-style clone3.  The
       earlier startup traffic is uninteresting and would otherwise consume
       the trace before the child error-pipe handshake occurs. */
    if (is_clone3 || is_spawn_dup) {
        linux_spawn_trace_count = 0u;
        linux_spawn_trace_owner = scheduler_current_pid();
        linux_spawn_trace_active = 1u;
    }
    if (linux_spawn_trace_active == 0u ||
        linux_spawn_trace_owner != scheduler_current_pid()) {
        return;
    }
    if (linux_spawn_trace_count >= 64u) {
        return;
    }
    ++linux_spawn_trace_count;
    debug_log_puts("linux-spawn: ");
    debug_log_puts(event);
    debug_log_puts(" pid=");
    debug_log_u64_dec(scheduler_current_pid());
    debug_log_puts(" tid=");
    debug_log_u64_dec(scheduler_current_tid());
    debug_log_puts(" a=");
    debug_log_u64_dec(a);
    debug_log_puts(" b=");
    debug_log_u64_dec(b);
    debug_log_puts(" c=");
    debug_log_u64_dec(c);
    debug_log_puts(" result=");
    debug_log_u64_dec(result);
    debug_log_puts(" errno=");
    debug_log_u64_dec((uint64_t)(unsigned int)*ladybird_errno_location());
    debug_log_puts("\n");
}

static uint64_t linux_arch_prctl(uint64_t code, uint64_t addr) {
    switch (code) {
    case LINUX_ARCH_SET_FS:
        scheduler_set_current_fs_base(addr);
        return 0u;
    case LINUX_ARCH_GET_FS:
        if (addr == 0u || !vex_syscall_user_range_valid(addr, sizeof(uint64_t), 1)) {
            return linux_error(LINUX_EFAULT);
        }
        *(uint64_t *)(uintptr_t)addr = scheduler_current_fs_base();
        return 0u;
    default:
        return linux_error(LINUX_ENOSYS);
    }
}

typedef struct linux_sched_param {
    int32_t sched_priority;
} linux_sched_param_t;

static process_t *linux_sched_target(scheduler_task_ref_t *ref,uint64_t pid) {
    return scheduler_task_acquire(ref,pid?(uint32_t)pid:scheduler_current_tid(),pid!=0u);
}

static uint64_t linux_sched_setscheduler(uint64_t pid, uint64_t policy_arg,
                                         uint64_t param_addr) {
    SCHEDULER_TASK_REF(target_ref);
    process_t *target = linux_sched_target(&target_ref,pid);
    linux_sched_param_t param;
    const uint32_t policy = (uint32_t)policy_arg;
    const uint32_t base_policy = policy & ~LINUX_SCHED_RESET_ON_FORK;
    uint8_t native_priority;

    if (target == NULL || !scheduler_can_control_process(target->pid) ||
        param_addr == 0u || !vex_syscall_user_range_valid(
            param_addr, sizeof(param), 0)) {
        return linux_error(target == NULL ? LINUX_EINVAL : LINUX_EPERM);
    }
    memcpy(&param, (const void *)(uintptr_t)param_addr, sizeof(param));

    if (base_policy == LINUX_SCHED_OTHER) {
        if (param.sched_priority != 0) {
            return linux_error(LINUX_EINVAL);
        }
        /* SCHED_OTHER uses FlowSched's ordinary fair group entitlement. */
        native_priority = 1u;
    } else if (base_policy == LINUX_SCHED_FIFO || base_policy == LINUX_SCHED_RR) {
        if (!scheduler_current_has_capability(VEX_CAP_PROC_ADMIN)) {
            return linux_error(LINUX_EPERM);
        }
        if (param.sched_priority < 1 || param.sched_priority > 99) {
            return linux_error(LINUX_EINVAL);
        }
        /* FlowSched has no Linux strict-priority FIFO/RR class. Do not claim
           such a contract by storing metadata that cannot affect selection. */
        return linux_error(95u); /* ENOTSUP */
    } else {
        return linux_error(LINUX_EINVAL);
    }

    target->linux_sched_policy = base_policy;
    target->linux_sched_priority = param.sched_priority;
    target->linux_sched_reset_on_fork = (policy & LINUX_SCHED_RESET_ON_FORK) != 0u;
    scheduler_set_process_priority(target->pid, native_priority);
    return 0u;
}

static uint64_t linux_sched_getscheduler(uint64_t pid) {
    SCHEDULER_TASK_REF(target_ref);
    process_t *target = linux_sched_target(&target_ref,pid);
    if (target == NULL || !scheduler_can_control_process(target->pid)) {
        return linux_error(target == NULL ? LINUX_EINVAL : LINUX_EPERM);
    }
    return (uint64_t)(target->linux_sched_policy |
        (target->linux_sched_reset_on_fork != 0u ? LINUX_SCHED_RESET_ON_FORK : 0u));
}

static uint64_t linux_sched_priority_bound(uint64_t policy_arg, int maximum) {
    const uint32_t base_policy = (uint32_t)policy_arg & ~LINUX_SCHED_RESET_ON_FORK;
    if (base_policy == LINUX_SCHED_OTHER) {
        return 0u;
    }
    if (base_policy == LINUX_SCHED_FIFO || base_policy == LINUX_SCHED_RR) {
        return maximum != 0 ? 99u : 1u;
    }
    return linux_error(LINUX_EINVAL);
}

static uint64_t linux_mlock_range(uint64_t address, uint64_t length) {
    if (address == 0u || length == 0u ||
        length > UINT64_MAX - address ||
        address >= VMM_USER_ADDRESS_LIMIT ||
        length > VMM_USER_ADDRESS_LIMIT - address ||
        !vex_syscall_user_range_valid(address, (size_t)length, 0)) {
        return linux_error(LINUX_ENOMEM);
    }
    /* VexOS has no swap: once lazy pages are materialised by the access
       validation above they remain resident until the owning mapping exits.
       That is the native implementation of the mlock guarantee. */
    return 0u;
}

static uint64_t linux_result(uint64_t value) {
    int err;
    if (value != (uint64_t)-1) return value;
    err = *ladybird_errno_location();
    return err != 0 ? linux_error((uint64_t)(unsigned int)err) : linux_error(LINUX_EINVAL);
}

static uint64_t linux_align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
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

static uint64_t linux_path_inode(const char *path) {
    uint64_t hash = 1469598103934665603ull; /* FNV-1a 64 offset basis */

    if (path == NULL || path[0] == '\0') {
        return 1u;
    }
    while (*path != '\0') {
        hash ^= (uint64_t)(unsigned char)*path++;
        hash *= 1099511628211ull;
    }
    return hash != 0u ? hash : 1u;
}

static uint64_t linux_read(uint64_t fd, uint64_t buffer, uint64_t count) {
    if(!vex_syscall_user_range_valid(buffer,(size_t)count,1))return linux_error(LINUX_EFAULT);
    vex_fd_io_req_t req;
    req.fd = (int)fd;
    req.buffer = (void *)(uintptr_t)buffer;
    req.count = (size_t)count;
    return vex_syscall_dispatch(VEX_SYS_READFD,
        (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t linux_write(uint64_t fd, uint64_t buffer, uint64_t count) {
    if(!vex_syscall_user_range_valid(buffer,(size_t)count,0))return linux_error(LINUX_EFAULT);
    vex_fd_io_req_t req;
    req.fd = (int)fd;
    req.buffer = (void *)(uintptr_t)buffer;
    req.count = (size_t)count;
    return vex_syscall_dispatch(VEX_SYS_WRITEFD,
        (uint64_t)(uintptr_t)&req, 0, 0);
}

typedef struct {
    void *iov_base;
    uint64_t iov_len;
} linux_iovec_t;

static uint64_t linux_writev(uint64_t fd, uint64_t iov_addr, uint64_t iovcnt) {
    VEX_SCRATCH(vector);
    linux_iovec_t *iov=NULL;
    uint64_t total = 0u;

    if (iovcnt == 0u) {
        return 0u;
    }
    if (iov == NULL || iovcnt > 1024u || iovcnt > UINT64_MAX / sizeof(*iov) ||
        !vex_syscall_user_range_valid(iov_addr, (size_t)(iovcnt * sizeof(*iov)), 0)) {
        return linux_error(LINUX_EFAULT);
    }

    iov=vex_scratch_allocate(&vector,(size_t)iovcnt*sizeof(*iov));
    if(!iov)return linux_error(LINUX_ENOMEM);
    if(vex_copy_from_user(iov,iov_addr,(size_t)iovcnt*sizeof(*iov)))return linux_error(LINUX_EFAULT);
    for (uint64_t i = 0u; i < iovcnt; ++i) {
        if (iov[i].iov_len == 0u) {
            continue;
        }
        const uint64_t written = linux_write(fd, (uint64_t)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
        if (written == (uint64_t)-1) {
            return total != 0u ? total : linux_result(written);
        }
        total += written;
        if (written < iov[i].iov_len) {
            break;
        }
    }
    return total;
}

static uint64_t linux_readv(uint64_t fd, uint64_t iov_addr, uint64_t iovcnt) {
    VEX_SCRATCH(vector);
    linux_iovec_t *iov=NULL;
    uint64_t total = 0u;

    if (iovcnt == 0u) {
        return 0u;
    }
    if (iov == NULL || iovcnt > 1024u || iovcnt > UINT64_MAX / sizeof(*iov) ||
        !vex_syscall_user_range_valid(iov_addr, (size_t)(iovcnt * sizeof(*iov)), 0)) {
        return linux_error(LINUX_EFAULT);
    }

    iov=vex_scratch_allocate(&vector,(size_t)iovcnt*sizeof(*iov));
    if(!iov)return linux_error(LINUX_ENOMEM);
    if(vex_copy_from_user(iov,iov_addr,(size_t)iovcnt*sizeof(*iov)))return linux_error(LINUX_EFAULT);
    for (uint64_t i = 0u; i < iovcnt; ++i) {
        if (iov[i].iov_len == 0u) {
            continue;
        }
        const uint64_t got = linux_read(fd, (uint64_t)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
        if (got == (uint64_t)-1) {
            return total != 0u ? total : linux_result(got);
        }
        total += got;
        if (got < iov[i].iov_len) {
            break;
        }
    }
    return total;
}

static uint64_t linux_mmap(uint64_t addr, uint64_t length, uint64_t prot, uint64_t flags, uint64_t fd, uint64_t offset) {
    vex_mmap_req_t req;
    uint64_t result;
    const int requests_wx = (prot & (PROT_WRITE | PROT_EXEC)) ==
        (PROT_WRITE | PROT_EXEC);

    /* The Vex JIT arena exposes distinct RW/NX and RX/RO addresses. Linux
       mmap returns only one address, so returning either alias for an RWX
       request would lie about the other permission. Refuse it before the
       native mmap/JIT path can reserve anything. */
    if (requests_wx) {
        result = linux_error(LINUX_EACCES);
        linux_memory_trace("mmap", addr, length, prot, flags, fd, offset,
                           result);
        return result;
    }
    req.addr = (void *)(uintptr_t)addr;
    req.length = (size_t)length;
    req.prot = (int)prot;
    req.flags = (int)flags;
    req.fd = (int)fd;
    req.offset = (off_t)offset;
    result = vex_syscall_dispatch(VEX_SYS_MMAP, (uint64_t)(uintptr_t)&req, 0, 0);
    result = result == (uint64_t)MAP_FAILED ? linux_error(LINUX_ENOMEM) : result;
    if (VEX_BROWSER_MEMORY_DIAGNOSTIC && fd <= 0x7fffffffu &&
        vex_web_fd_is_wk2_shared_memory((int)fd) != 0 &&
        linux_wk2_mmap_trace_count < 2048u) {
        ++linux_wk2_mmap_trace_count;
        debug_log_puts("wk2-shm-mmap: pid=");
        debug_log_u64_dec(scheduler_current_pid());
        debug_log_puts(" tid=");
        debug_log_u64_dec(scheduler_current_tid());
        debug_log_puts(" rip=0x");
        debug_log_u64_hex(linux_current_user_rip());
        debug_log_puts(" addr=0x");
        debug_log_u64_hex(addr);
        debug_log_puts(" len=0x");
        debug_log_u64_hex(length);
        debug_log_puts(" prot=0x");
        debug_log_u64_hex(prot);
        debug_log_puts(" flags=0x");
        debug_log_u64_hex(flags);
        debug_log_puts(" fd=");
        debug_log_u64_dec(fd);
        debug_log_puts(" off=0x");
        debug_log_u64_hex(offset);
        debug_log_puts(" result=0x");
        debug_log_u64_hex(result);
        debug_log_puts(" free=");
        debug_log_u64_dec(pmm_free_page_count());
        debug_log_puts("\n");
        if (VEX_BROWSER_MEMORY_DIAGNOSTIC && (linux_wk2_mmap_trace_count % 32u)==0u) {
            storage_memory_diagnostic();process_memory_diagnostic();surface_memory_diagnostic();
            for(uint64_t index=0;index<scheduler_process_count();index++) {
                scheduler_process_info_t info;
                if(scheduler_process_info_at(index,&info)!=0)continue;
                SCHEDULER_TASK_REF(task_ref);
                process_t *p=scheduler_task_acquire(&task_ref,info.pid,1);
                if(!p || !p->user_mode || (p->group && p->group->process_id!=p->pid))continue;
                debug_log_puts("memory-process: pid=");debug_log_u64_dec(p->pid);debug_log_puts("\n");
                vmm_memory_diagnostic(vmm_cr3_to_address_space(p->cr3));
                process_memory_resident_diagnostic(p);
            }
        }
    }
    linux_memory_trace("mmap", addr, length, prot, flags, fd, offset, result);
    return result;
}

static uint64_t linux_brk(uint64_t requested) {
    process_t *process = scheduler_current_process();
    uint64_t current;
    uint64_t old_aligned;
    uint64_t new_aligned;

    if (process == NULL) {
        return 0u;
    }

    current = process->group != NULL ? process->group->linux_brk : 0u;
    if (current == 0u) {
        current = scheduler_current_user_alloc_next();
    }
    if (current == 0u) {
        current = 0x0000000100000000ull;
        if (process->group != NULL) {
            process->group->linux_brk = current;
        }
    }
    if (requested == 0u) {
        return current;
    }

    if (requested <= current) {
        if (process->group != NULL) {
            process->group->linux_brk = requested;
        }
        return requested;
    }

    old_aligned = linux_align_up(current, 4096u);
    new_aligned = linux_align_up(requested, 4096u);
    if (new_aligned > old_aligned) {
        const uint64_t mapped = linux_mmap(old_aligned, new_aligned - old_aligned,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, (uint64_t)-1ll, 0u);
        /* linux_mmap translates VexOS failure to a negative errno.  Testing
         * only MAP_FAILED here turns -ENOMEM into a successful break update,
         * after which glibc writes into the old (possibly library-owned)
         * break. */
        if (mapped >= (uint64_t)-4095ll || mapped != old_aligned) {
            /* brk() has stricter semantics than mmap(): the break must grow
             * at exactly the requested contiguous address.  linux_mmap()
             * correctly treats an occupied hint as a non-FIXED fallback, but
             * accepting that fallback here would make glibc believe the old
             * range was backed and it would write into the existing mapping.
             * Remove the temporary fallback before reporting brk failure. */
            if (mapped < (uint64_t)-4095ll && mapped != old_aligned) {
                vex_memory_req_t rollback;
                rollback.addr = (void *)(uintptr_t)mapped;
                rollback.length = new_aligned - old_aligned;
                rollback.prot_or_advice = 0;
                (void)vex_syscall_dispatch(VEX_SYS_MUNMAP,
                    (uint64_t)(uintptr_t)&rollback, 0u, 0u);
            }
            return current;
        }
    }

    if (process->group != NULL) {
        process->group->linux_brk = requested;
    }
    return requested;
}

static void compat_zero(void *dst,size_t length);

static void fill_linux_stat_from_vex(linux_stat_t *linux_st, const struct stat *vex_st) {
    if (linux_st == NULL || vex_st == NULL) {
        return;
    }

    compat_zero(linux_st,sizeof(*linux_st));
    linux_st->st_dev = 1u;
    linux_st->st_ino = 1u;
    linux_st->st_nlink = 1u;
    linux_st->st_mode = vex_st->st_mode != 0u ? vex_st->st_mode : (LINUX_STAT_MODE_REG | LINUX_STAT_MODE_PERM);
    linux_st->st_uid = vex_st->st_uid;
    linux_st->st_gid = vex_st->st_gid;
    linux_st->st_rdev = 0u;
    linux_st->st_size = (int64_t)vex_st->st_size;
    linux_st->st_blksize = vex_st->st_blksize != 0u ? (int64_t)vex_st->st_blksize : 4096;
    linux_st->st_blocks = vex_st->st_blocks != 0u ? (int64_t)vex_st->st_blocks : (int64_t)((vex_st->st_size + 511u) / 512u);
}

static void linux_stat_times(linux_stat_t *out,const vex_file_times_t *times) {
    if(times->valid & VEX_FILE_ACCESS_TIME){out->st_atime=times->seconds[0];out->st_atime_nsec=times->nanoseconds[0];}
    if(times->valid & VEX_FILE_MODIFY_TIME){out->st_mtime=times->seconds[1];out->st_mtime_nsec=times->nanoseconds[1];}
    if(times->valid & VEX_FILE_CHANGE_TIME){out->st_ctime=times->seconds[2];out->st_ctime_nsec=times->nanoseconds[2];}
}

static uint64_t linux_stat_path(uint64_t path, uint64_t st, uint64_t which) {
    struct stat vex_st;
    linux_stat_t *linux_st = (linux_stat_t *)(uintptr_t)st;
    vex_path_stat_req_t req;
    req.path = (const char *)(uintptr_t)path;
    req.st = &vex_st;

    if (!vex_syscall_user_range_valid(st,sizeof(*linux_st),1) ||
        !vex_syscall_user_string_valid(path,4096u))return linux_error(LINUX_EFAULT);

    if (vex_syscall_dispatch(which, (uint64_t)(uintptr_t)&req, 0, 0) == (uint64_t)-1) {
        return linux_result((uint64_t)-1);
    }

    fill_linux_stat_from_vex(linux_st, &vex_st);
    linux_st->st_ino = linux_path_inode(req.path);
    vex_file_times_t times={0};if(!fs_get_times(req.path,&times))linux_stat_times(linux_st,&times);
    return 0;
}

static uint64_t linux_stat_resolved_path(const char *path, uint64_t st, uint64_t flags) {
    struct stat vex_st;
    linux_stat_t *linux_st = (linux_stat_t *)(uintptr_t)st;

    if(!vex_syscall_user_range_valid(st,sizeof(*linux_st),1))return linux_error(LINUX_EFAULT);
    if (((flags & 0x100u) ? lstat(path,&vex_st) : stat(path,&vex_st)) != 0) {
        return linux_result((uint64_t)-1);
    }

    fill_linux_stat_from_vex(linux_st, &vex_st);
    linux_st->st_ino = linux_path_inode(path);
    vex_file_times_t times={0};if(!fs_get_times(path,&times))linux_stat_times(linux_st,&times);
    return 0u;
}

static uint64_t linux_fstat(uint64_t fd, uint64_t st) {
    struct stat vex_st;
    linux_stat_t *linux_st = (linux_stat_t *)(uintptr_t)st;
    uint64_t inode;

    if(!vex_syscall_user_range_valid(st,sizeof(*linux_st),1))return linux_error(LINUX_EFAULT);
    if (fstat((int)fd,&vex_st)!=0)return linux_result((uint64_t)-1);

    fill_linux_stat_from_vex(linux_st, &vex_st);
    vex_file_times_t times={0};if(!vex_web_fd_times((int)fd,&times))linux_stat_times(linux_st,&times);
    inode = ladybird_fd_inode_hint((int)fd);
    if (inode != 0u) {
        linux_st->st_ino = inode;
    }
    return 0;
}

static uint64_t linux_pipe(uint64_t pipefd, uint64_t flags) {
    vex_pipe_req_t req;
    uint64_t result;

    if ((flags & ~(LINUX_O_NONBLOCK | LINUX_O_CLOEXEC)) != 0u) {
        return (uint64_t)-1;
    }
    req.pipefd = (int *)(uintptr_t)pipefd;
    result = vex_syscall_dispatch(VEX_SYS_PIPE, (uint64_t)(uintptr_t)&req, 0, 0);
    if (result == (uint64_t)-1) {
        linux_spawn_trace("pipe.fail", flags, 0u, 0u, result);
        return result;
    }

    linux_spawn_trace("pipe.fds", (uint64_t)(uint32_t)req.pipefd[0],
                      (uint64_t)(uint32_t)req.pipefd[1], flags, result);

    /* Linux creates both descriptors atomically with pipe2().  VexOS keeps
       the native pipe primitive independent and applies the descriptor policy
       immediately after creation; failure rolls the pair back so callers
       never observe a half-configured pipe. */
    if ((flags & LINUX_O_NONBLOCK) != 0u &&
        (fcntl(req.pipefd[0], F_SETFL, O_NONBLOCK) != 0 ||
         fcntl(req.pipefd[1], F_SETFL, O_NONBLOCK) != 0)) {
        close(req.pipefd[0]);
        close(req.pipefd[1]);
        return (uint64_t)-1;
    }
    if ((flags & LINUX_O_CLOEXEC) != 0u &&
        (fcntl(req.pipefd[0], F_SETFD, FD_CLOEXEC) != 0 ||
         fcntl(req.pipefd[1], F_SETFD, FD_CLOEXEC) != 0)) {
        close(req.pipefd[0]);
        close(req.pipefd[1]);
        return (uint64_t)-1;
    }
    return 0u;
}

static uint64_t linux_pread(uint64_t fd, uint64_t buffer, uint64_t count, uint64_t offset) {
    if(!vex_syscall_user_range_valid(buffer,(size_t)count,1))return linux_error(LINUX_EFAULT);
    vex_rw_offset_req_t req;
    req.fd = (int)fd;
    req.buffer = (void *)(uintptr_t)buffer;
    req.count = (size_t)count;
    req.offset = (off_t)offset;
    return vex_syscall_dispatch(VEX_SYS_PREAD, (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t linux_pwrite(uint64_t fd, uint64_t buffer, uint64_t count, uint64_t offset) {
    if(!vex_syscall_user_range_valid(buffer,(size_t)count,0))return linux_error(LINUX_EFAULT);
    vex_rw_offset_req_t req;
    req.fd = (int)fd;
    req.buffer = (void *)(uintptr_t)buffer;
    req.count = (size_t)count;
    req.offset = (off_t)offset;
    return vex_syscall_dispatch(VEX_SYS_PWRITE, (uint64_t)(uintptr_t)&req, 0, 0);
}

static uint64_t linux_mprotect(uint64_t addr, uint64_t length, uint64_t prot) {
    vex_memory_req_t req;
    uint64_t result;
    const int requests_wx = (prot & (PROT_WRITE | PROT_EXEC)) ==
        (PROT_WRITE | PROT_EXEC);

    if (requests_wx) {
        result = linux_error(LINUX_EACCES);
        linux_memory_trace("mprotect", addr, length, prot, 0u, 0u, 0u,
                           result);
        return result;
    }
    req.addr = (void *)(uintptr_t)addr;
    req.length = (size_t)length;
    req.prot_or_advice = (int)prot;
    result = vex_syscall_dispatch(VEX_SYS_MPROTECT, (uint64_t)(uintptr_t)&req, 0, 0);
    linux_memory_trace("mprotect", addr, length, prot, 0u, 0u, 0u, result);
    return result;
}

static uint64_t linux_sockaddr_call(uint64_t fd, uint64_t addr, uint64_t addrlen, uint64_t which) {
    if(addrlen>4096u)return linux_error(LINUX_EINVAL);
    if(!vex_syscall_user_range_valid(addr,(size_t)addrlen,0))return linux_error(LINUX_EFAULT);
    vex_sockaddr_req_t req;
    req.sockfd = (int)fd;
    req.addr = (const struct sockaddr *)(uintptr_t)addr;
    req.addrlen = (uint32_t)addrlen;
    return linux_result(vex_syscall_dispatch(which, (uint64_t)(uintptr_t)&req, 0, 0));
}

static uint64_t linux_getsockname(uint64_t fd, uint64_t address,
                                  uint64_t address_length) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    struct sockaddr unnamed;
    uint32_t capacity;
    uint32_t actual_length = 0u;
    uint32_t copied;
    int result;

    if (address_length == 0u ||
        !vex_syscall_user_range_valid(address_length, sizeof(capacity), 1)) {
        return linux_error(LINUX_EFAULT);
    }
    memcpy(&capacity, (const void *)(uintptr_t)address_length, sizeof(capacity));
    memset(&unnamed, 0, sizeof(unnamed));
    result = vex_socketpair_getsockname((int)fd, &unnamed, sizeof(unnamed),
                                        &actual_length);
    if (result < 0) {
        return linux_result((uint64_t)(int64_t)result);
    }
    copied = capacity < actual_length ? capacity : actual_length;
    if (copied != 0u) {
        if (address == 0u || !vex_syscall_user_range_valid(address, copied, 1)) {
            return linux_error(LINUX_EFAULT);
        }
        memcpy((void *)(uintptr_t)address, &unnamed, copied);
    }
    memcpy((void *)(uintptr_t)address_length, &actual_length,
           sizeof(actual_length));
    return 0u;
}

static uint64_t linux_getpeername(uint64_t fd, uint64_t address,
                                  uint64_t address_length) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    struct sockaddr unnamed;
    uint32_t capacity;
    uint32_t actual_length = 0u;
    uint32_t copied;
    int result;

    if (address_length == 0u ||
        !vex_syscall_user_range_valid(address_length, sizeof(capacity), 1)) {
        return linux_error(LINUX_EFAULT);
    }
    memcpy(&capacity, (const void *)(uintptr_t)address_length, sizeof(capacity));
    memset(&unnamed, 0, sizeof(unnamed));
    result = vex_socketpair_getpeername((int)fd, &unnamed, sizeof(unnamed),
                                        &actual_length);
    if (result < 0) {
        return linux_result((uint64_t)(int64_t)result);
    }
    copied = capacity < actual_length ? capacity : actual_length;
    if (copied != 0u) {
        if (address == 0u || !vex_syscall_user_range_valid(address, copied, 1)) {
            return linux_error(LINUX_EFAULT);
        }
        memcpy((void *)(uintptr_t)address, &unnamed, copied);
    }
    memcpy((void *)(uintptr_t)address_length, &actual_length,
           sizeof(actual_length));
    return 0u;
}

static uint64_t linux_datagram_io(uint64_t fd,uint64_t buffer,uint64_t length,uint64_t flags,uint64_t address,uint64_t address_arg,int sending) {
    VEX_SCRATCH(temporary);uint8_t local[1024];struct sockaddr_in name;uint32_t capacity=0,actual=0;
    if(length>INT64_MAX || !vex_syscall_user_range_valid(buffer,(size_t)length,!sending))return linux_error(LINUX_EFAULT);
    void *bytes=length<=sizeof(local)?local:vex_scratch_allocate(&temporary,(size_t)length);
    if(!bytes)return linux_error(LINUX_ENOMEM);
    if(sending) {
        if(vex_copy_from_user(bytes,buffer,(size_t)length))return linux_error(LINUX_EFAULT);
        if(address && (address_arg<sizeof(name) || vex_copy_from_user(&name,address,sizeof(name))))return linux_error(LINUX_EFAULT);
        return linux_result((uint64_t)vex_socket_sendto(fd,bytes,length,flags,address?(void *)&name:NULL,address?sizeof(name):0));
    }
    if(address && (!address_arg || vex_copy_from_user(&capacity,address_arg,4)))return linux_error(LINUX_EFAULT);
    ssize_t n=vex_socket_recvfrom(fd,bytes,length,flags,address?(void *)&name:NULL,sizeof(name),address?&actual:NULL);
    if(n>=0) {
        kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
        size_t copied=(uint64_t)n>length?(size_t)length:(size_t)n;
        if(vex_copy_to_user(buffer,bytes,copied) ||
           (address && (vex_copy_to_user(address,&name,capacity<actual?capacity:actual) || vex_copy_to_user(address_arg,&actual,4))))return linux_error(LINUX_EFAULT);
    }
    return linux_result((uint64_t)n);
}
static uint64_t linux_send_recv_kernel(uint64_t fd, uint64_t buffer, uint64_t length, uint64_t flags, uint64_t which) {
    vex_send_recv_req_t req;
    req.sockfd = (int)fd;
    req.buffer = (void *)(uintptr_t)buffer;
    req.length = (size_t)length;
    req.flags = (int)flags;
    return linux_result(vex_syscall_dispatch(which, (uint64_t)(uintptr_t)&req, 0, 0));
}


/* Linux msghdr for sendmsg/recvmsg (x86-64 ABI sizes) */
typedef struct {
    void    *msg_name;
    uint32_t msg_namelen;
    uint32_t _pad0;
    linux_iovec_t *msg_iov;
    uint64_t msg_iovlen;
    void    *msg_control;
    uint64_t msg_controllen;
    int      msg_flags;
} linux_msghdr_t;

typedef struct {
    uint64_t cmsg_len;
    int32_t cmsg_level;
    int32_t cmsg_type;
} linux_cmsghdr_t;

#define LINUX_SOL_SOCKET       1
#define LINUX_SCM_RIGHTS       1
#define LINUX_MSG_CTRUNC       0x0008
#define LINUX_MSG_CMSG_CLOEXEC 0x40000000ull
#define LINUX_CMSG_HDR_SIZE    16u
#define LINUX_CMSG_ALIGN(value) (((value) + 7u) & ~7u)
#define LINUX_MSG_MAX_CONTROL  (64u * 1024u)

/* sendmsg/recvmsg keep one Linux msghdr as one native message.  Splitting an
   iovec across several send() calls would silently turn SEQPACKET into a
   stream, so gather/scatter through a bounded kernel buffer instead. */
#define LINUX_MSG_MAX_GATHER (16u * 1024u * 1024u)

static uint64_t linux_msg_validate(uint64_t msg_addr, int writable,
                                   linux_msghdr_t *copy, uint64_t *total_out, vex_scratch_t *vector, linux_iovec_t **original_iov) {
    uint64_t total = 0u;
    if (copy == NULL || total_out == NULL || msg_addr == 0u ||
        !vex_syscall_user_range_valid(msg_addr, sizeof(*copy), writable)) {
        return linux_error(LINUX_EFAULT);
    }
    if(vex_copy_from_user(copy,msg_addr,sizeof(*copy)))return linux_error(LINUX_EFAULT);
    if (copy->msg_name != NULL && copy->msg_namelen != 0u) {
        size_t n=copy->msg_namelen;
        if(writable && n>sizeof(struct sockaddr_in))n=sizeof(struct sockaddr_in);
        if(!vex_syscall_user_range_valid((uint64_t)(uintptr_t)copy->msg_name,n,writable))return linux_error(LINUX_EFAULT);
    }
    if ((copy->msg_control == NULL && copy->msg_controllen != 0u) ||
        copy->msg_controllen > LINUX_MSG_MAX_CONTROL) {
        return linux_error(copy->msg_controllen > LINUX_MSG_MAX_CONTROL
            ? LINUX_EMSGSIZE : LINUX_EFAULT);
    }
    if (copy->msg_control != NULL && copy->msg_controllen != 0u) {
        if (!vex_syscall_user_range_valid(
                (uint64_t)(uintptr_t)copy->msg_control,
                (size_t)copy->msg_controllen, writable)) {
            return linux_error(LINUX_EFAULT);
        }
    }
    if (copy->msg_iovlen > 1024u ||
        (copy->msg_iovlen != 0u && copy->msg_iov == NULL) ||
        (copy->msg_iovlen > UINT64_MAX / sizeof(linux_iovec_t)) ||
        (copy->msg_iovlen != 0u && !vex_syscall_user_range_valid(
            (uint64_t)(uintptr_t)copy->msg_iov,
            (size_t)(copy->msg_iovlen * sizeof(linux_iovec_t)), 0))) {
        return linux_error(LINUX_EFAULT);
    }
    *original_iov=copy->msg_iov;
    size_t vector_size=(size_t)copy->msg_iovlen*sizeof(linux_iovec_t);
    copy->msg_iov=vex_scratch_allocate(vector,vector_size);
    if(!copy->msg_iov)return linux_error(LINUX_ENOMEM);
    if(vex_copy_from_user(copy->msg_iov,(uint64_t)(uintptr_t)*original_iov,vector_size))return linux_error(LINUX_EFAULT);
    for (uint64_t i = 0u; i < copy->msg_iovlen; ++i) {
        linux_iovec_t iov;
        memcpy(&iov, &copy->msg_iov[i], sizeof(iov));
        if (iov.iov_len == 0u) {
            continue;
        }
        if (iov.iov_base == NULL || iov.iov_len > UINT64_MAX - total ||
            total + iov.iov_len > LINUX_MSG_MAX_GATHER ||
            !vex_syscall_user_range_valid((uint64_t)(uintptr_t)iov.iov_base,
                (size_t)iov.iov_len, writable)) {
            const uint64_t too_large = iov.iov_len > LINUX_MSG_MAX_GATHER ||
                total > LINUX_MSG_MAX_GATHER -
                    (iov.iov_len < LINUX_MSG_MAX_GATHER ? iov.iov_len : 0u);
            return linux_error(too_large != 0u ? LINUX_EMSGSIZE : LINUX_EFAULT);
        }
        total += iov.iov_len;
    }
    *total_out = total;
    return 0u;
}

static void linux_release_message_attachments(
    vex_message_attachment_t *attachments, size_t count) {
    if (attachments == NULL) {
        return;
    }
    for (size_t index = 0u; index < count; ++index) {
        vex_message_attachment_release(&attachments[index]);
    }
}

/* Parse Linux ancillary data into acquired native object references.  The
 * numeric descriptors are used only to acquire the sender's local object;
 * no descriptor value enters the Vex message queue. */
static int linux_parse_send_attachments(const linux_msghdr_t *msg,
                                        vex_message_attachment_t *attachments,
                                        size_t *count_out) {
    uint64_t offset = 0u;
    size_t count = 0u;

    if (msg == NULL || count_out == NULL || attachments == NULL) {
        return EINVAL;
    }
    *count_out = 0u;
    memset(attachments, 0,
           VEX_MESSAGE_MAX_ATTACHMENTS * sizeof(*attachments));
    while (offset < msg->msg_controllen) {
        linux_cmsghdr_t header;
        uint64_t remaining = msg->msg_controllen - offset;
        uint64_t step;
        uint64_t data_length;
        uint64_t fd_count;

        if (remaining < LINUX_CMSG_HDR_SIZE) {
            goto malformed;
        }
        memcpy(&header, (const uint8_t *)msg->msg_control + offset,
               sizeof(header));
        if (header.cmsg_len < LINUX_CMSG_HDR_SIZE ||
            header.cmsg_len > remaining) {
            goto malformed;
        }
        data_length = header.cmsg_len - LINUX_CMSG_HDR_SIZE;
        step = LINUX_CMSG_ALIGN(header.cmsg_len);
        /* A final cmsghdr is allowed to omit trailing alignment padding. */
        if (step > remaining) {
            if (header.cmsg_len != remaining) {
                goto malformed;
            }
            step = header.cmsg_len;
        }
        if (header.cmsg_level != LINUX_SOL_SOCKET ||
            header.cmsg_type != LINUX_SCM_RIGHTS ||
            data_length == 0u || data_length % sizeof(int) != 0u) {
            if (header.cmsg_level == LINUX_SOL_SOCKET &&
                header.cmsg_type == LINUX_SCM_RIGHTS) {
                goto malformed;
            }
            goto unsupported;
        }
        fd_count = data_length / sizeof(int);
        if (fd_count > VEX_MESSAGE_MAX_ATTACHMENTS - count) {
            goto too_many;
        }
        for (uint64_t index = 0u; index < fd_count; ++index) {
            int fd;
            ladybird_fd_object_t *object;
            memcpy(&fd, (const uint8_t *)msg->msg_control + offset +
                   LINUX_CMSG_HDR_SIZE + index * sizeof(int), sizeof(fd));
            object = ladybird_fd_acquire(fd);
            if (object == NULL) {
#if VEX_BROWSER_DIAGNOSTIC
                static unsigned bad_attachment_traces;
                if(bad_attachment_traces++<32){
                    debug_log_puts("browser IPC invalid attachment pid=");debug_log_u64_dec(scheduler_current_process_id());
                    debug_log_puts(" tid=");debug_log_u64_dec(scheduler_current_tid());
                    debug_log_puts(" fd=");debug_log_u64_dec((uint64_t)(int64_t)fd);debug_log_puts("\n");
                }
#endif
                goto bad_fd;
            }
            attachments[count].object = object;
            attachments[count].rights = VEX_MESSAGE_ATTACHMENT_DUP;
            attachments[count].type = VEX_MESSAGE_ATTACHMENT_FD;
            ++count;
        }
        offset += step;
    }
    *count_out = count;
    return 0;

malformed:
    linux_release_message_attachments(attachments, count);
    *count_out = 0u;
    return EINVAL;
unsupported:
    linux_release_message_attachments(attachments, count);
    *count_out = 0u;
    return ENOTSUP;
too_many:
    linux_release_message_attachments(attachments, count);
    *count_out = 0u;
    return EMSGSIZE;
bad_fd:
    linux_release_message_attachments(attachments, count);
    *count_out = 0u;
    return EBADF;
}

static uint64_t linux_install_received_attachments(
    uint64_t msg_addr, linux_msghdr_t *msg,
    vex_message_attachment_t *attachments, size_t attachment_count,
    uint64_t recv_flags) {
    int installed[VEX_MESSAGE_MAX_ATTACHMENTS];
    size_t deliver_count = attachment_count;
    size_t capacity = msg != NULL ? (size_t)msg->msg_controllen : 0u;
    size_t written = 0u;
    int truncated = 0;
    uint8_t close_on_exec = (recv_flags & LINUX_MSG_CMSG_CLOEXEC) != 0u;

    memset(installed, 0xff, sizeof(installed));
    if (attachment_count == 0u) {
        msg->msg_controllen = 0u;
        msg->msg_flags = 0;
        memcpy((void *)(uintptr_t)msg_addr, msg, sizeof(*msg));
        return 0u;
    }
    for (size_t index = 0u; index < attachment_count; ++index) {
        if (attachments[index].type != VEX_MESSAGE_ATTACHMENT_FD) {
            linux_release_message_attachments(attachments, attachment_count);
            return linux_error(LINUX_ENOTSUP);
        }
    }
    if (msg->msg_control == NULL || capacity < LINUX_CMSG_HDR_SIZE) {
        deliver_count = 0u;
        truncated = 1;
    } else {
        const size_t max_fds = (capacity - LINUX_CMSG_HDR_SIZE) / sizeof(int);
        if (deliver_count > max_fds) {
            deliver_count = max_fds;
            truncated = 1;
        }
        if (deliver_count == 0u) {
            truncated = 1;
        }
    }
    for (size_t index = 0u; index < deliver_count; ++index) {
        installed[index] = ladybird_fd_install_flags(
            (ladybird_fd_object_t *)attachments[index].object, -1,
            close_on_exec);
        /* install_flags consumes the acquired reference on both success and
         * failure.  Never release the same object a second time. */
        attachments[index].object = NULL;
        attachments[index].rights = 0u;
        attachments[index].type = 0u;
        if (installed[index] < 0) {
            for (size_t rollback = 0u; rollback < index; ++rollback) {
                if (installed[rollback] >= 0) {
                    (void)close(installed[rollback]);
                }
            }
            linux_release_message_attachments(attachments + index + 1u,
                                              attachment_count - index - 1u);
            return linux_error(LINUX_EMFILE);
        }
    }
    for (size_t index = deliver_count; index < attachment_count; ++index) {
        vex_message_attachment_release(&attachments[index]);
    }
    if (deliver_count != 0u) {
        linux_cmsghdr_t header;
        const size_t cmsg_length = LINUX_CMSG_HDR_SIZE +
            deliver_count * sizeof(int);
        const size_t cmsg_space = LINUX_CMSG_ALIGN(cmsg_length);
        memset(&header, 0, sizeof(header));
        header.cmsg_len = cmsg_length;
        header.cmsg_level = LINUX_SOL_SOCKET;
        header.cmsg_type = LINUX_SCM_RIGHTS;
        memcpy(msg->msg_control, &header, sizeof(header));
        for (size_t index = 0u; index < deliver_count; ++index) {
            memcpy((uint8_t *)msg->msg_control + LINUX_CMSG_HDR_SIZE +
                   index * sizeof(int), &installed[index], sizeof(int));
        }
        written = cmsg_space <= capacity ? cmsg_space : cmsg_length;
        if (written > cmsg_length) {
            memset((uint8_t *)msg->msg_control + cmsg_length, 0,
                   written - cmsg_length);
        }
    }
    msg->msg_controllen = written;
    msg->msg_flags = truncated ? LINUX_MSG_CTRUNC : 0;
    memcpy((void *)(uintptr_t)msg_addr, msg, sizeof(*msg));
    return 0u;
}

/* Opt-in transport observation: bounded per Process, kernel-owned gathered
 * bytes only. No decoding or alteration of the application's protocol. */
static void linux_browser_ipc_trace(const char *operation,uint64_t fd,
        const uint8_t *payload,uint64_t bytes,size_t attachments,uint64_t result) {
#if VEX_BROWSER_DIAGNOSTIC
    static struct {uint32_t pid,count;} traced[16];
    if(!scheduler_current_has_capability(VEX_CAP_NET_CONNECT) || vex_socket_network_fd(fd))return;
    if((int64_t)result==-LINUX_EAGAIN)return;
    /* Diagnostic-only suppression of the known periodic WebKit display
     * refresh message, established by the matching ELF's MessageNames table.
     * Never used for dispatch, validation, or application behavior. */
    if(bytes>=32 && !attachments){uint64_t header;memcpy(&header,payload+24,8);
        if(header==UINT64_C(0x000000000b7c0000))return;}
    uint32_t pid=scheduler_current_process_id(),slot=16;
    for(unsigned i=0;i<16;i++)if(traced[i].pid==pid){slot=i;break;}
    if(slot==16)for(unsigned i=0;i<16;i++)if(!traced[i].pid){slot=i;traced[i].pid=pid;break;}
    if(slot==16 || traced[slot].count++>=512)return;
    debug_log_puts("browser IPC ");debug_log_puts(operation);
    debug_log_puts(" pid=");debug_log_u64_dec(pid);debug_log_puts(" tid=");debug_log_u64_dec(scheduler_current_tid());
    debug_log_puts(" fd=");debug_log_u64_dec(fd);debug_log_puts(" bytes=");debug_log_u64_dec(bytes);
    debug_log_puts(" attachments=");debug_log_u64_dec(attachments);debug_log_puts(" result=");debug_log_u64_hex(result);
    for(uint64_t i=0;i+8<=bytes && i<56;i+=8){uint64_t word;memcpy(&word,payload+i,8);debug_log_puts(" word=");debug_log_u64_hex(word);}
    debug_log_puts("\n");
#else
    (void)operation;(void)fd;(void)payload;(void)bytes;(void)attachments;(void)result;
#endif
}

static uint64_t linux_sendmsg(uint64_t fd, uint64_t msg_addr, uint64_t flags) {
    VEX_SCRATCH(vector);VEX_SCRATCH(payload_ref);VEX_SCRATCH(control_ref);
    linux_iovec_t *original_iov=NULL;
    linux_msghdr_t msg;
    uint64_t total;
    uint8_t *payload;
    vex_message_attachment_t attachments[VEX_MESSAGE_MAX_ATTACHMENTS];
    size_t attachment_count = 0u;
    int attachment_error;
    uint64_t offset = 0u;
    uint64_t result;
    {
        const uint64_t validation = linux_msg_validate(msg_addr, 0, &msg, &total,&vector,&original_iov);
        if (validation != 0u) {
            return validation;
        }
    }
    if(msg.msg_controllen) {
        void *control=vex_scratch_allocate(&control_ref,msg.msg_controllen);
        if(!control)return linux_error(LINUX_ENOMEM);
        if(vex_copy_from_user(control,(uint64_t)(uintptr_t)msg.msg_control,msg.msg_controllen))return linux_error(LINUX_EFAULT);
        msg.msg_control=control;
    }
    struct sockaddr_in address_copy;
    if(msg.msg_name && msg.msg_namelen) {
        size_t n=msg.msg_namelen<sizeof(address_copy)?msg.msg_namelen:sizeof(address_copy);
        if(vex_copy_from_user(&address_copy,(uint64_t)(uintptr_t)msg.msg_name,n))return linux_error(LINUX_EFAULT);
        msg.msg_name=&address_copy;
    }
    if (msg.msg_controllen != 0u) {
        if(vex_socket_network_fd(fd))return linux_error(LINUX_EOPNOTSUPP);
        attachment_error = linux_parse_send_attachments(&msg, attachments,
                                                         &attachment_count);
        if (attachment_error != 0) {
            linux_release_message_attachments(attachments, attachment_count);
            return linux_error((uint64_t)(unsigned int)attachment_error);
        }
    }
    payload = vex_scratch_allocate(&payload_ref,total == 0u ? 1u : (size_t)total);
    if (payload == NULL) {
        linux_release_message_attachments(attachments, attachment_count);
        return linux_error(LINUX_ENOMEM);
    }
    for (uint64_t i = 0u; i < msg.msg_iovlen; ++i) {
        linux_iovec_t iov;
        memcpy(&iov, &msg.msg_iov[i], sizeof(iov));
        if (iov.iov_len != 0u) {
            if(vex_copy_from_user(payload+offset,(uint64_t)(uintptr_t)iov.iov_base,(size_t)iov.iov_len)) {
                linux_release_message_attachments(attachments,attachment_count);return linux_error(LINUX_EFAULT);
            }
            offset += iov.iov_len;
        }
    }
    if (attachment_count != 0u) {
        const ssize_t sent = vex_socketpair_send_attached((int)fd, payload,
            (size_t)total, (int)flags, attachments, attachment_count);
        result = sent < 0 ? linux_result((uint64_t)-1) : (uint64_t)sent;
    } else if(vex_socket_network_fd(fd)) {
        result=linux_result((uint64_t)vex_socket_sendto(fd,payload,total,flags,msg.msg_name,msg.msg_namelen));
    } else if(msg.msg_name && msg.msg_namelen) {
        result=linux_error(LINUX_EOPNOTSUPP);
    } else {
        result = linux_result(linux_send_recv_kernel(fd,
            (uint64_t)(uintptr_t)payload, total, flags, VEX_SYS_SEND));
    }
    if (attachment_count != 0u &&
        (result == (uint64_t)-1 ||
         (result != (uint64_t)(ssize_t)-1 &&
          attachments[0].object != NULL))) {
        linux_release_message_attachments(attachments, attachment_count);
    }
    linux_browser_ipc_trace("send",fd,payload,total,attachment_count,result);
    vex_scratch_release(&payload_ref);
    return result;
}

static uint64_t linux_recvmsg(uint64_t fd, uint64_t msg_addr, uint64_t flags) {
    VEX_SCRATCH(vector);VEX_SCRATCH(payload_ref);VEX_SCRATCH(control_ref);
    linux_iovec_t *original_iov=NULL;
    linux_msghdr_t msg;
    uint64_t capacity;
    uint8_t *payload;
    uint64_t result;
    uint64_t offset = 0u;struct sockaddr_in address_copy;int address_received=0;
    vex_message_attachment_t attachments[VEX_MESSAGE_MAX_ATTACHMENTS];
    size_t attachment_count = VEX_MESSAGE_MAX_ATTACHMENTS;
    int attached_receive = 1;
    {
        const uint64_t validation = linux_msg_validate(msg_addr, 1, &msg, &capacity,&vector,&original_iov);
        if (validation != 0u) {
            return validation;
        }
    }
    payload = vex_scratch_allocate(&payload_ref,capacity == 0u ? 1u : (size_t)capacity);
    if (payload == NULL) {
        return linux_error(LINUX_ENOMEM);
    }
    memset(attachments, 0, sizeof(attachments));
    if(vex_socket_network_fd(fd)){
        attached_receive=0;
        if(flags&0x2000u){
            udp_transport_fault_t fault;
            if(flags&~(0x2000u|MSG_DONTWAIT))result=linux_error(LINUX_EOPNOTSUPP);
            else if(vex_socket_receive_fault(fd,&fault)<0)result=linux_result((uint64_t)-1);
            else {
                struct {uint64_t length;int32_t level,type;uint32_t error;uint8_t origin,icmp_type,code,pad;uint32_t info,data;struct sockaddr_in offender;} c;
                memset(&c,0,sizeof(c));c.length=sizeof(c);c.level=0;c.type=11;c.origin=2;c.icmp_type=fault.type;c.code=fault.code;c.info=fault.detail;
                c.error=vex_socket_fault_errno(&fault);
                c.offender.sin_family=AF_INET;c.offender.sin_addr.s_addr=__builtin_bswap32(fault.reporter_ip);
                struct sockaddr_in destination={.sin_family=AF_INET,.sin_port=__builtin_bswap16(fault.destination_port)};
                destination.sin_addr.s_addr=__builtin_bswap32(fault.destination_ip);
                if(msg.msg_name){size_t n=msg.msg_namelen<sizeof(destination)?msg.msg_namelen:sizeof(destination);memcpy(msg.msg_name,&destination,n);msg.msg_namelen=sizeof(destination);}
                size_t n=msg.msg_controllen<sizeof(c)?msg.msg_controllen:sizeof(c);
                msg.msg_flags=0x2000|(n<sizeof(c)?LINUX_MSG_CTRUNC:0);
                if(n){memcpy(msg.msg_control,&c,n);if(n>=16)((uint64_t *)msg.msg_control)[0]=n;}
                msg.msg_controllen=n;
                result=fault.payload_size<capacity?fault.payload_size:capacity;
                if(fault.payload_size>capacity)msg.msg_flags|=0x20;
                memcpy(payload,fault.payload,result);
            }
        } else {
            uint32_t actual=0;
            /* Request the original datagram size to report MSG_TRUNC without
             * copying beyond the supplied iovecs. Streams reject this flag. */
            result=linux_result((uint64_t)vex_socket_recvfrom(fd,payload,capacity,flags,msg.msg_name?(void *)&address_copy:NULL,sizeof(address_copy),msg.msg_name?&actual:NULL));
            if((int64_t)result>=0){if(msg.msg_name){size_t n=msg.msg_namelen<actual?msg.msg_namelen:actual;address_received=(int)n;msg.msg_namelen=actual;}msg.msg_controllen=0;msg.msg_flags=result>capacity?0x20:0;}
        }
    } else if(msg.msg_name && msg.msg_namelen){
        attached_receive=0;result=linux_error(LINUX_EOPNOTSUPP);
    } else {
        const ssize_t received = vex_socketpair_receive_attached((int)fd,
            payload, (size_t)capacity, (int)flags, attachments,
            &attachment_count);
        if (received < 0 && *ladybird_errno_location() == ENOTSUP) {
            attached_receive = 0;
            msg.msg_controllen=0u;msg.msg_flags=0;
            result = linux_result(linux_send_recv_kernel(fd,
                (uint64_t)(uintptr_t)payload, capacity, flags, VEX_SYS_RECV));
        } else {
            result = received < 0 ? linux_result((uint64_t)-1)
                                  : (uint64_t)received;
        }
    }
    if ((int64_t)result >= 0) {
        kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
        if(!vex_syscall_user_range_valid(msg_addr,sizeof(msg),1) ||
           (msg.msg_controllen && !vex_syscall_user_range_valid((uint64_t)(uintptr_t)msg.msg_control,msg.msg_controllen,1))) {
            linux_release_message_attachments(attachments,attachment_count);return linux_error(LINUX_EFAULT);
        }
        if(address_received && vex_copy_to_user((uint64_t)(uintptr_t)msg.msg_name,&address_copy,(size_t)address_received)) {
            linux_release_message_attachments(attachments,attachment_count);return linux_error(LINUX_EFAULT);
        }
        uint64_t copied=result<capacity?result:capacity;
        for (uint64_t i = 0u; i < msg.msg_iovlen && offset < copied; ++i) {
            linux_iovec_t iov;
            uint64_t copy_length;
            memcpy(&iov, &msg.msg_iov[i], sizeof(iov));
            copy_length = iov.iov_len < copied - offset
                ? iov.iov_len : copied - offset;
            if (copy_length != 0u) {
                if(vex_copy_to_user((uint64_t)(uintptr_t)iov.iov_base,payload+offset,(size_t)copy_length)) {
                    linux_release_message_attachments(attachments,attachment_count);return linux_error(LINUX_EFAULT);
                }
                offset += copy_length;
            }
        }
        msg.msg_iov=original_iov;
        if (attached_receive != 0) {
            const uint64_t control_result = linux_install_received_attachments(
                msg_addr, &msg, attachments, attachment_count, flags);
            if (control_result != 0u) {
                vex_scratch_release(&payload_ref);
                return control_result;
            }
        } else {
            memcpy((void *)(uintptr_t)msg_addr, &msg, sizeof(msg));
        }
    } else if (attached_receive != 0) {
        linux_release_message_attachments(attachments, attachment_count);
    }
    linux_browser_ipc_trace("recv",fd,payload,(int64_t)result>0?(result<capacity?result:capacity):0,attached_receive && (int64_t)result>=0?attachment_count:0,result);
    vex_scratch_release(&payload_ref);
    return result;
}

static void compat_zero(void *dst, size_t length) {
    uint8_t *bytes = (uint8_t *)dst;
    for (size_t i = 0u; i < length; ++i) {
        bytes[i] = 0u;
    }
}

static uint64_t linux_poll(uint64_t fds, uint64_t nfds, int timeout) {
    if(nfds>SIZE_MAX/sizeof(struct pollfd) || !vex_syscall_user_range_valid(fds,(size_t)nfds*sizeof(struct pollfd),1))return linux_error(LINUX_EFAULT);
    vex_poll_req_t req;
    static unsigned poll_reports;unsigned ticket=++poll_reports;
    int trace=VEX_COMPAT_TRACE&&ticket<=96u&&nfds<=64u&&vex_syscall_user_range_valid(fds,nfds*sizeof(struct pollfd),1);
    if(trace){debug_log_puts("linux-poll: enter id=");debug_log_u64_dec(ticket);debug_log_puts(" pid=");debug_log_u64_dec(scheduler_current_process_id());debug_log_puts(" tid=");debug_log_u64_dec(scheduler_current_pid());debug_log_puts(" count=");debug_log_u64_dec(nfds);debug_log_puts(" timeout=");debug_log_u64_hex((uint64_t)(int64_t)timeout);debug_log_puts(" rip=");debug_log_u64_hex(linux_current_user_rip());
        for(uint64_t i=0;i<nfds&&i<12;++i){const struct pollfd *p=(const struct pollfd*)(uintptr_t)fds+i;debug_log_puts(" fd/events=");debug_log_u64_dec((uint32_t)p->fd);debug_log_puts("/");debug_log_u64_hex((uint16_t)p->events);}debug_log_puts("\n");}
    req.fds = (struct pollfd *)(uintptr_t)fds;
    req.count = (nfds_t)nfds;
    req.timeout = timeout;
    uint64_t result=vex_syscall_dispatch(VEX_SYS_POLL,(uint64_t)(uintptr_t)&req,0,0);
    if(trace){debug_log_puts("linux-poll: return id=");debug_log_u64_dec(ticket);debug_log_puts(" result=");debug_log_u64_hex(result);
        for(uint64_t i=0;i<nfds&&i<12;++i){const struct pollfd *p=(const struct pollfd*)(uintptr_t)fds+i;debug_log_puts(" fd/revents=");debug_log_u64_dec((uint32_t)p->fd);debug_log_puts("/");debug_log_u64_hex((uint16_t)p->revents);}debug_log_puts("\n");}
    return result;
}

static uint64_t linux_nanosleep(uint64_t requested, uint64_t remaining) {
    vex_nanosleep_req_t req;
    req.requested = (const struct timespec *)(uintptr_t)requested;
    req.remaining = (struct timespec *)(uintptr_t)remaining;
    return linux_result(vex_syscall_dispatch(VEX_SYS_NANOSLEEP,
        (uint64_t)(uintptr_t)&req, 0, 0));
}

static int linux_signal_event(uint64_t sig, uint16_t *event_id,
                              uint64_t *signal_bit) {
    if (event_id == NULL || signal_bit == NULL || sig == 0u || sig > 64u) {
        return -1;
    }
    *signal_bit = 1ull << (sig - 1u);
    switch (sig) {
    case LINUX_SIGUSR1:
        *event_id = VEX_EVENT_USER_INTERRUPT;
        return 0;
    case LINUX_SIGCANCEL:
        *event_id = LINUX_EVENT_SIGCANCEL;
        return 0;
    case LINUX_SIGSETXID:
        *event_id = LINUX_EVENT_SIGSETXID;
        return 0;
    default:
        return -1;
    }
}

static int linux_sync_signal_index(uint64_t sig) {
    return sig == LINUX_SIGSEGV ? 0 : (sig == LINUX_SIGBUS ? 1 : -1);
}

static uint32_t linux_signal_mask_to_native(uint64_t mask) {
    uint32_t native = 0u;
    if ((mask & (1ull << (LINUX_SIGUSR1 - 1u))) != 0u) {
        native |= 1u << VEX_EVENT_USER_INTERRUPT;
    }
    if ((mask & (1ull << (LINUX_SIGCANCEL - 1u))) != 0u) {
        native |= 1u << LINUX_EVENT_SIGCANCEL;
    }
    if ((mask & (1ull << (LINUX_SIGSETXID - 1u))) != 0u) {
        native |= 1u << LINUX_EVENT_SIGSETXID;
    }
    return native;
}

static uint32_t linux_native_signal_bits(void) {
    return (1u << VEX_EVENT_USER_INTERRUPT) |
           (1u << LINUX_EVENT_SIGCANCEL) |
           (1u << LINUX_EVENT_SIGSETXID);
}

static uint64_t linux_rt_sigaction(uint64_t sig, uint64_t act_addr,
                                   uint64_t old_addr, uint64_t sigset_size) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    kernel_guard_t events __attribute__((cleanup(vex_event_state_end)))=vex_event_state_begin();
    process_t *process = scheduler_current_process();
    process_group_t *group = process == NULL ? NULL : process->group;
    linux_kernel_sigaction_t action;
    linux_kernel_sigaction_t old_action;
    vex_event_registration_t registration;
    uint16_t event_id = 0u;
    uint64_t signal_bit = 0u;
    uint64_t *stored_mask = NULL;
    uint32_t action_mask_native;
    const int sync_signal_index = linux_sync_signal_index(sig);

    if (group == NULL || sigset_size != sizeof(uint64_t)) {
        return linux_error(LINUX_EINVAL);
    }
    if (old_addr != 0u && !vex_syscall_user_range_valid(old_addr,
            sizeof(old_action), 1)) {
        return linux_error(LINUX_EFAULT);
    }
    if (act_addr != 0u && !vex_syscall_user_range_valid(act_addr,
            sizeof(action), 0)) {
        return linux_error(LINUX_EFAULT);
    }

    if (sig == LINUX_SIGPIPE) {
        if (old_addr != 0u) {
            compat_zero(&old_action, sizeof(old_action));
            old_action.handler = group->linux_sigpipe_handler;
            old_action.flags = group->linux_sigpipe_flags;
            old_action.restorer = group->linux_sigpipe_restorer;
            old_action.mask[0] = group->linux_sigpipe_mask[0];
            memcpy((void *)(uintptr_t)old_addr, &old_action, sizeof(old_action));
        }
        if (act_addr == 0u) {
            return 0u;
        }
        memcpy(&action, (const void *)(uintptr_t)act_addr, sizeof(action));
        /* VexOS has no native SIGPIPE event.  Preserve dispositions and
           masks faithfully for SIG_IGN/default; reject an undeliverable
           user handler instead of returning a false success. */
        if (action.handler != 0u && action.handler != 1u) {
            return linux_error(LINUX_ENOSYS);
        }
        group->linux_sigpipe_handler = action.handler;
        group->linux_sigpipe_flags = action.flags;
        group->linux_sigpipe_restorer = action.restorer;
        group->linux_sigpipe_mask[0] = action.mask[0];
        group->linux_sigpipe_mask[1] = 0u;
        return 0u;
    }

    if (sync_signal_index >= 0) {
        const uint32_t index = (uint32_t)sync_signal_index;
        if (old_addr != 0u) {
            compat_zero(&old_action, sizeof(old_action));
            old_action.handler = group->linux_sync_fault_handler[index];
            old_action.flags = group->linux_sync_fault_flags[index];
            old_action.restorer = group->linux_sync_fault_restorer[index];
            old_action.mask[0] = group->linux_sync_fault_mask[index][0];
            memcpy((void *)(uintptr_t)old_addr, &old_action, sizeof(old_action));
        }
        if (act_addr == 0u) {
            return 0u;
        }
        memcpy(&action, (const void *)(uintptr_t)act_addr, sizeof(action));
        /* Vex currently has one userspace signal stack.  Do not claim
           SA_ONSTACK support until an alternate stack is actually tracked. */
        if ((action.flags & LINUX_SA_ONSTACK) != 0u) {
            return linux_error(LINUX_ENOSYS);
        }
        if (action.handler > 1u) {
            if ((action.flags & LINUX_SA_RESTORER) == 0u ||
                action.restorer == 0u) {
                return linux_error(LINUX_EINVAL);
            }
            if (!vex_event_validate_user_code((struct process *)process,
                                               action.handler) ||
                !vex_event_validate_user_code((struct process *)process,
                                               action.restorer)) {
                return linux_error(LINUX_EFAULT);
            }
        }
        group->linux_sync_fault_handler[index] = action.handler;
        group->linux_sync_fault_flags[index] = action.flags;
        group->linux_sync_fault_restorer[index] = action.restorer;
        group->linux_sync_fault_mask[index][0] = action.mask[0] &
            ~((1ull << (9u - 1u)) | (1ull << (19u - 1u)));
        group->linux_sync_fault_mask[index][1] = 0u;
        return 0u;
    }

    if (linux_signal_event(sig, &event_id, &signal_bit) != 0) {
        return linux_error(LINUX_ENOSYS);
    }
    if (vex_event_query(event_id, &registration) != 0) {
        return linux_error(LINUX_EINVAL);
    }
    old_action.handler = registration.handler;
    old_action.flags = registration.flags;
    old_action.restorer = registration.restorer;
    if (sig == LINUX_SIGUSR1) {
        old_action.mask[0] = group->linux_sigusr1_action_mask[0];
        stored_mask = group->linux_sigusr1_action_mask;
    } else {
        const uint32_t index = sig == LINUX_SIGCANCEL ? 0u : 1u;
        old_action.mask[0] = group->linux_internal_action_mask[index][0];
        stored_mask = group->linux_internal_action_mask[index];
    }
    if (old_addr != 0u) {
        memcpy((void *)(uintptr_t)old_addr, &old_action, sizeof(old_action));
    }
    if (act_addr == 0u) {
        return 0u;
    }

    memcpy(&action, (const void *)(uintptr_t)act_addr, sizeof(action));
    registration.handler = action.handler;
    registration.restorer = action.restorer;
    registration.flags = action.flags;
    registration.delivery_value = sig;
    if (vex_event_register(event_id, &registration) != 0) {
        return linux_error(LINUX_EFAULT);
    }
    *stored_mask = action.mask[0];
    action_mask_native = linux_signal_mask_to_native(action.mask[0]);
    group->event_action_mask[event_id] = action_mask_native;
    (void)signal_bit;
    return 0u;
}

static uint64_t linux_rt_sigprocmask(uint64_t how, uint64_t set_addr,
                                     uint64_t old_addr, uint64_t sigset_size) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    kernel_guard_t events __attribute__((cleanup(vex_event_state_end)))=vex_event_state_begin();
    process_t *process = scheduler_current_process();
    uint64_t set = 0u;
    uint64_t old;
    uint64_t next;
    const uint64_t unblockable = (1ull << (9u - 1u)) |
                                 (1ull << (19u - 1u));
    const uint32_t native_bits = linux_native_signal_bits();

    if (process == NULL || sigset_size != sizeof(uint64_t)) {
        return linux_error(LINUX_EINVAL);
    }
    if (old_addr != 0u && !vex_syscall_user_range_valid(old_addr,
            sizeof(uint64_t), 1)) {
        return linux_error(LINUX_EFAULT);
    }
    if (set_addr != 0u) {
        if (!vex_syscall_user_range_valid(set_addr, sizeof(uint64_t), 0)) {
            return linux_error(LINUX_EFAULT);
        }
        memcpy(&set, (const void *)(uintptr_t)set_addr, sizeof(set));
    }
    if (how > LINUX_SIG_SETMASK || (how != LINUX_SIG_SETMASK && set_addr == 0u)) {
        return linux_error(LINUX_EINVAL);
    }

    old = process->linux_signal_mask;
    if (set_addr != 0u) {
        switch (how) {
        case LINUX_SIG_BLOCK: next = old | set; break;
        case LINUX_SIG_UNBLOCK: next = old & ~set; break;
        default: next = set; break;
        }
        next &= ~unblockable;
        process->linux_signal_mask = next;
        switch (how) {
        case LINUX_SIG_BLOCK:
            process->event_mask |= linux_signal_mask_to_native(set);
            break;
        case LINUX_SIG_UNBLOCK:
            process->event_mask &= ~linux_signal_mask_to_native(set);
            break;
        default:
            process->event_mask = (process->event_mask & ~native_bits) |
                                  linux_signal_mask_to_native(next);
            break;
        }
    }
    if (old_addr != 0u) {
        memcpy((void *)(uintptr_t)old_addr, &old, sizeof(old));
    }
    return 0u;
}

static void linux_context_from_interrupt(uint8_t *ucontext,
                                         const interrupt_frame_t *frame,
                                         const vex_sync_fault_t *fault,
                                         uint64_t signal_mask,
                                         const uint8_t *fpstate) {
    uint64_t *gregs = (uint64_t *)(void *)(ucontext + LINUX_UC_GREGS_OFFSET);

    memset(ucontext, 0, LINUX_UCONTEXT_SIZE);
    /* uc_stack: no alternate signal stack is currently registered. */
    *(uint64_t *)(void *)(ucontext + 16u) = 0u;
    *(uint32_t *)(void *)(ucontext + 24u) = 2u; /* SS_DISABLE */
    *(uint64_t *)(void *)(ucontext + 32u) = 0u;
    gregs[0] = frame->r8;  gregs[1] = frame->r9;
    gregs[2] = frame->r10; gregs[3] = frame->r11;
    gregs[4] = frame->r12; gregs[5] = frame->r13;
    gregs[6] = frame->r14; gregs[7] = frame->r15;
    gregs[8] = frame->rdi; gregs[9] = frame->rsi;
    gregs[10] = frame->rbp; gregs[11] = frame->rbx;
    gregs[12] = frame->rdx; gregs[13] = frame->rax;
    gregs[14] = frame->rcx; gregs[15] = frame->rsp;
    gregs[16] = frame->rip; gregs[17] = frame->rflags;
    gregs[18] = (frame->cs & 0xffffu) | ((frame->ss & 0xffffu) << 48u);
    gregs[19] = fault->error_code;
    gregs[20] = fault->vector;
    gregs[21] = signal_mask;
    gregs[22] = fault->address_valid != 0u ? fault->address : 0u;
    memcpy(ucontext + LINUX_UC_SIGMASK_OFFSET, &signal_mask, sizeof(signal_mask));
    if (fpstate != NULL) {
        memcpy(ucontext + LINUX_UC_FPSTATE_OFFSET, fpstate, LINUX_FPSTATE_SIZE);
    }
}

size_t compat_linux_async_context_size(void){return LINUX_UCONTEXT_SIZE;}
void compat_linux_build_async_context(void *destination,const uint64_t *s,uint64_t result,uint64_t signal) {
    process_t *p=scheduler_current_process();interrupt_frame_t f={0};vex_sync_fault_t no_fault={0};
    f.r15=s[3];f.r14=s[4];f.r13=s[5];f.r12=s[6];f.r10=s[7];f.r9=s[8];f.r8=s[9];
    f.rdi=s[10];f.rsi=s[11];f.rbp=s[12];f.rdx=s[13];f.rbx=s[14];f.r11=s[15];f.rcx=s[16];
    f.rax=result;f.rip=s[16];f.rsp=s[17];f.rflags=s[15];f.cs=0x2b;f.ss=0x23;
    linux_context_from_interrupt(destination,&f,&no_fault,p->linux_signal_mask,p->event_saved_fpu_state);
    uint64_t address=(uint64_t)(uintptr_t)destination;
    *(uint64_t *)((uint8_t *)destination+LINUX_UC_FPREGS_OFFSET)=address+LINUX_UC_FPSTATE_OFFSET;
    memcpy(p->event_saved_frame,&f,sizeof(f));
    kernel_guard_t action __attribute__((cleanup(vex_event_state_end)))=vex_event_state_begin();
    uint64_t mask=0;
    if(signal==10u)mask=p->group->linux_sigusr1_action_mask[0];
    else if(signal==32u || signal==33u)mask=p->group->linux_internal_action_mask[signal-32u][0];
    if(signal && signal<=64u)p->linux_signal_mask|=mask|(UINT64_C(1)<<(signal-1u));
}
static uint64_t linux_signal_mask_sanitize(uint64_t mask) {
    return mask & ~((1ull << (9u - 1u)) | (1ull << (19u - 1u)));
}

int compat_linux_deliver_sync_fault(struct interrupt_frame *opaque_frame,
                                    const struct vex_sync_fault *opaque_fault) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    kernel_guard_t events __attribute__((cleanup(vex_event_state_end)))=vex_event_state_begin();
    process_t *process = scheduler_current_process();
    process_group_t *group = process == NULL ? NULL : process->group;
    const interrupt_frame_t *frame = (const interrupt_frame_t *)opaque_frame;
    const vex_sync_fault_t *fault = (const vex_sync_fault_t *)opaque_fault;
    linux_kernel_sigaction_t action;
    vex_event_registration_t registration;
    uint8_t siginfo[128];
    uint8_t ucontext[LINUX_UCONTEXT_SIZE];
    uint64_t old_mask;
    uint64_t next_mask;
    uint64_t signal_address = 0u;
    int signal_number;
    int signal_code;
    int index;

    if (process == NULL || group == NULL || frame == NULL || fault == NULL ||
        scheduler_current_abi() != VEX_ABI_LINUX || process->event_active != 0u) {
        return 0;
    }
    if (fault->vector == 14u) {
        if (fault->address_valid != 0u &&
            process_memory_file_fault_is_bus(process, fault->address)) {
            signal_number = LINUX_SIGBUS;
            signal_code = LINUX_BUS_ADRERR;
        } else {
            signal_number = LINUX_SIGSEGV;
            signal_code = ((fault->error_code & 1u) != 0u ||
                process_memory_region_contains(process, fault->address, 1u))
                ? LINUX_SEGV_ACCERR : LINUX_SEGV_MAPERR;
        }
        signal_address = fault->address_valid != 0u ? fault->address : 0u;
    } else if (fault->vector == 17u) {
        signal_number = LINUX_SIGBUS;
        signal_code = LINUX_BUS_ADRALN;
    } else if (fault->vector == 13u) {
        signal_number = LINUX_SIGSEGV;
        signal_code = LINUX_SI_KERNEL;
    } else {
        /* No Linux signal is claimed for an exception whose semantics are not
           implemented here (for example #UD/#DE).  The common fault path will
           terminate the process instead of fabricating SIGSEGV. */
        return 0;
    }
    index = linux_sync_signal_index((uint64_t)signal_number);
    if (index < 0) {
        return 0;
    }
    action.handler = group->linux_sync_fault_handler[index];
    action.flags = group->linux_sync_fault_flags[index];
    action.restorer = group->linux_sync_fault_restorer[index];
    action.mask[0] = group->linux_sync_fault_mask[index][0];
    if (action.handler <= 1u || action.restorer == 0u ||
        (action.flags & LINUX_SA_RESTORER) == 0u ||
        !vex_event_validate_user_code((struct process *)process, action.handler) ||
        !vex_event_validate_user_code((struct process *)process, action.restorer)) {
        return 0;
    }

    old_mask = process->linux_signal_mask;
    memset(siginfo, 0, sizeof(siginfo));
    *(int32_t *)(void *)(siginfo + 0u) = signal_number;
    *(int32_t *)(void *)(siginfo + 8u) = signal_code;
    *(uint64_t *)(void *)(siginfo + 16u) = signal_address;
    linux_context_from_interrupt(ucontext, frame, fault, old_mask,
                                 process->event_saved_fpu_valid != 0u
                                     ? process->event_saved_fpu_state : NULL);

    memset(&registration, 0, sizeof(registration));
    registration.handler = action.handler;
    registration.restorer = action.restorer;
    registration.flags = action.flags;
    registration.delivery_value = (uint64_t)signal_number;

    next_mask = old_mask | action.mask[0];
    if ((action.flags & LINUX_SA_NODEFER) == 0u) {
        next_mask |= 1ull << ((uint32_t)signal_number - 1u);
    }
    next_mask = linux_signal_mask_sanitize(next_mask);
    process->linux_signal_mask = next_mask;
    process->event_mask = (process->event_mask & ~linux_native_signal_bits()) |
        linux_signal_mask_to_native(next_mask);
    if ((action.flags & LINUX_SA_RESETHAND) != 0u) {
        group->linux_sync_fault_handler[index] = 0u;
        group->linux_sync_fault_flags[index] = 0u;
        group->linux_sync_fault_restorer[index] = 0u;
        group->linux_sync_fault_mask[index][0] = 0u;
    }

    if (vex_event_enter_sync(opaque_frame, fault,
            VEX_EVENT_SYNCHRONOUS_FAULT, &registration,
            siginfo, sizeof(siginfo), ucontext, sizeof(ucontext),
            VEX_EVENT_CONTEXT_SYNC_COMPAT) != 0) {
        process->linux_signal_mask = old_mask;
        process->event_mask = (process->event_mask & ~linux_native_signal_bits()) |
            linux_signal_mask_to_native(old_mask);
        return 0;
    }
    /* Linux ucontext_t carries an fpregs pointer.  Point it at the copied
       legacy x86 FXSAVE area embedded after the public signal mask. */
    {
        const uint64_t address = vex_event_user_context_address();
        if (address == 0u || address > UINT64_MAX -
                (LINUX_UC_FPSTATE_OFFSET + LINUX_FPSTATE_SIZE) ||
            !vex_syscall_user_range_valid(
                address + LINUX_UC_FPSTATE_OFFSET, LINUX_FPSTATE_SIZE, 1)) {
            vex_event_state_end(&events);process_memory_end_update(&memory);
        process_exit_group(128 + signal_number);__builtin_unreachable();
            return 0;
        }
        *(volatile uint64_t *)(uintptr_t)(address + LINUX_UC_FPREGS_OFFSET) =
            address + LINUX_UC_FPSTATE_OFFSET;
    }
    if (linux_sync_fault_trace_count < LINUX_SYNC_FAULT_TRACE_LIMIT) {
        ++linux_sync_fault_trace_count;
        debug_log_puts("linux-fault-signal: signal=");
        debug_log_u64_dec((uint64_t)signal_number);
        debug_log_puts(" code=");
        debug_log_u64_dec((uint64_t)(unsigned int)signal_code);
        debug_log_puts(" addr=0x");
        debug_log_u64_hex(signal_address);
        debug_log_puts(" instruction=0x");
        debug_log_u64_hex(fault->instruction);
        debug_log_puts(" error=0x");
        debug_log_u64_hex(fault->error_code);
        debug_log_puts(" access=");
        debug_log_u64_dec(fault->access);
        debug_log_puts(" handler=0x");
        debug_log_u64_hex(action.handler);
        debug_log_puts(" pte=0x");
        debug_log_u64_hex(vmm_get_page_flags(
            vmm_cr3_to_address_space(process->cr3), fault->instruction));
        debug_log_puts("\n");
    }
    return 1;
}

static uint64_t linux_rt_sigreturn(void) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    kernel_guard_t events __attribute__((cleanup(vex_event_state_end)))=vex_event_state_begin();
    process_t *process = scheduler_current_process();
    uint8_t user_context[LINUX_UCONTEXT_SIZE];
    vex_event_user_context_t context;
    uint64_t mask;
    uint64_t fpregs;
    const uint64_t address = vex_event_user_context_address();
    uint64_t *gregs;
    int trace_return = 0;

    if (linux_sync_return_trace_count < LINUX_SYNC_FAULT_TRACE_LIMIT) {
        ++linux_sync_return_trace_count;
        trace_return = 1;
    }

    if (process == NULL || (vex_event_context_kind()!=VEX_EVENT_CONTEXT_SYNC_COMPAT &&
                            vex_event_context_kind()!=VEX_EVENT_CONTEXT_ASYNC_COMPAT) || address == 0u ||
        !vex_syscall_user_range_valid(address, sizeof(user_context), 0)) {
        return vex_event_sigreturn() == 0 ? 0u : linux_error(LINUX_EFAULT);
    }
    memcpy(user_context, (const void *)(uintptr_t)address, sizeof(user_context));
    gregs = (uint64_t *)(void *)(user_context + LINUX_UC_GREGS_OFFSET);
    memset(&context, 0, sizeof(context));
    context.r8 = gregs[0]; context.r9 = gregs[1];
    context.r10 = gregs[2]; context.r11 = gregs[3];
    context.r12 = gregs[4]; context.r13 = gregs[5];
    context.r14 = gregs[6]; context.r15 = gregs[7];
    context.rdi = gregs[8]; context.rsi = gregs[9];
    context.rbp = gregs[10]; context.rbx = gregs[11];
    context.rdx = gregs[12]; context.rax = gregs[13];
    context.rcx = gregs[14]; context.rsp = gregs[15];
    context.rip = gregs[16]; context.rflags = gregs[17];
    context.cs = gregs[18] & 0xffffu;
    context.ss = gregs[18] >> 48u;
    context.vector = gregs[20]; context.error_code = gregs[19];
    context.fault_address = gregs[22];
    memcpy(&mask, user_context + LINUX_UC_SIGMASK_OFFSET, sizeof(mask));
    fpregs = *(const uint64_t *)(const void *)(user_context + LINUX_UC_FPREGS_OFFSET);
    if (trace_return != 0) {
        debug_log_puts("linux-fault-return: rip=0x");
        debug_log_u64_hex(context.rip);
        debug_log_puts(" rsp=0x");
        debug_log_u64_hex(context.rsp);
        debug_log_puts(" fp=0x");
        debug_log_u64_hex(fpregs);
        debug_log_puts(" fpu_valid=");
        debug_log_u64_dec(process->event_saved_fpu_valid);
        debug_log_puts("\n");
        debug_log_puts("linux-fault-return-check: rip_exec=");
        debug_log_u64_dec((uint64_t)vex_event_validate_user_code(
            (struct process *)process, context.rip));
        debug_log_puts(" pte=0x");
        debug_log_u64_hex(vmm_get_page_flags(
            vmm_cr3_to_address_space(process->cr3), context.rip));
        debug_log_puts("\n");
    }
    if (fpregs != 0u && vex_syscall_user_range_valid(fpregs,
            LINUX_FPSTATE_SIZE, 0) && process->event_saved_fpu_valid != 0u) {
        uint8_t legacy[LINUX_FPSTATE_SIZE];memcpy(legacy,(const void *)(uintptr_t)fpregs,sizeof(legacy));
        if(!scheduler_fpstate_valid(legacy)) {
            vex_event_state_end(&events);process_memory_end_update(&memory);
            process_exit_group(128+LINUX_SIGSEGV);__builtin_unreachable();
        }
        memcpy(process->event_saved_fpu_state,legacy,sizeof(legacy));
    } else if (fpregs != 0u) {
        vex_event_state_end(&events);process_memory_end_update(&memory);
        process_exit_group(128 + LINUX_SIGSEGV);__builtin_unreachable();
        return linux_error(LINUX_EFAULT);
    }
    if (vex_event_sigreturn_context(&context) != 0) {
        if (trace_return != 0) {
            debug_log_puts("linux-fault-return: rejected context cs=0x");
            debug_log_u64_hex(context.cs);
            debug_log_puts(" ss=0x");
            debug_log_u64_hex(context.ss);
            debug_log_puts(" flags=0x");
            debug_log_u64_hex(context.rflags);
            debug_log_puts("\n");
        }
        vex_event_state_end(&events);process_memory_end_update(&memory);
        process_exit_group(128 + LINUX_SIGSEGV);__builtin_unreachable();
        return linux_error(LINUX_EFAULT);
    }
    process->linux_signal_mask = linux_signal_mask_sanitize(mask);
    process->event_mask = (process->event_mask & ~linux_native_signal_bits()) |
        linux_signal_mask_to_native(process->linux_signal_mask);
    if (trace_return != 0) {
        debug_log_puts("linux-fault-return: context restored\n");
    }
    return 0u;
}

/* Linux signal operations are translated to the native VexOS event object.
   The Linux numbers and sigaction layout stop at this file. */
static uint64_t linux_signal_target(uint32_t pid, int sig) {
    process_t *caller = scheduler_current_process();
    if (sig == 3 || sig == 6 || sig == 11 ||
        (VEX_COMPAT_TRACE && (sig == 9 || sig == 15))) {
        const uint64_t frame = caller == NULL ? 0u : caller->syscall_frame_rsp;
        const uint64_t call_rip = linux_current_user_rip();
        debug_log_puts("linux-signal: sender=");
        debug_log_u64_dec(scheduler_current_pid());
        debug_log_puts(" tid=");
        debug_log_u64_dec(scheduler_current_tid());
        debug_log_puts(" target=");
        debug_log_u64_dec(pid);
        debug_log_puts(" sig=");
        debug_log_u64_dec((uint64_t)(unsigned int)sig);
        debug_log_puts(" rip=0x");
        debug_log_u64_hex(call_rip);
        debug_log_puts(" frame=0x");
        debug_log_u64_hex(frame);
        if (frame != 0u) {
            debug_log_puts(" f104=0x");
            debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)(frame + 104u));
            debug_log_puts(" f96=0x");
            debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)(frame + 96u));
            debug_log_puts(" f112=0x");
            debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)(frame + 112u));
            debug_log_puts(" f120=0x");
            debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)(frame + 120u));
            debug_log_puts(" f128=0x");
            debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)(frame + 128u));
            debug_log_puts(" f136=0x");
            debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)(frame + 136u));
            {
                const uint64_t user_rsp = *(const volatile uint64_t *)(uintptr_t)(frame + 136u);
                if (vex_syscall_user_range_valid(user_rsp, 16u, 0)) {
                    debug_log_puts(" u0=0x");
                    debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)user_rsp);
                    debug_log_puts(" u8=0x");
                    debug_log_u64_hex(*(const volatile uint64_t *)(uintptr_t)(user_rsp + 8u));

                    /* abort() reaches tgkill through a libc wrapper, so the
                       frame-pointer chain is often unavailable.  Preserve a
                       small, validated RSP window to recover return addresses
                       without dereferencing arbitrary user memory. */
                    if (vex_syscall_user_range_valid(user_rsp, 256u, 0)) {
                        for (unsigned int word = 0u; word < 32u; ++word) {
                            const uint64_t value = *(const volatile uint64_t *)
                                (uintptr_t)(user_rsp + (uint64_t)word * 8u);
                            debug_log_puts(" stk+");
                            if (word < 16u) debug_log_puts("0");
                            if (word < 8u) debug_log_puts("0");
                            debug_log_u64_hex((uint64_t)word * 8u);
                            debug_log_puts("=0x");
                            debug_log_u64_hex(value);
                        }
                    }
                }
            }
            {
                uint64_t rbp = *(const volatile uint64_t *)(uintptr_t)(frame + 96u);
                for (unsigned int depth = 0u; depth < 8u; ++depth) {
                    uint64_t ret;
                    if ((rbp & 7u) != 0u || rbp < 16u ||
                        !vex_syscall_user_range_valid(rbp, 16u, 0)) {
                        break;
                    }
                    ret = *(const volatile uint64_t *)(uintptr_t)(rbp + 8u);
                    debug_log_puts(" bt");
                    debug_log_u64_hex(depth);
                    debug_log_puts("=0x");
                    debug_log_u64_hex(ret);
                    rbp = *(const volatile uint64_t *)(uintptr_t)rbp;
                }
            }
        }
        debug_log_puts("\n");
    }
    switch (sig) {
    case 0:
        return 0u;
    case 3:
        return process_kill(pid) == 0 ? 0u : linux_error(LINUX_EINVAL);
    case LINUX_SIGABRT:
        if (caller != NULL && caller->group != NULL &&
            caller->group->process_id == pid) {
            process_exit_group(128 + LINUX_SIGABRT);
            return 0u;
        }
        return process_kill(pid) == 0 ? 0u : linux_error(LINUX_EINVAL);
    case 9: {
        /* kill(SIGKILL) names the Process, not just its leader Thread.
         * Otherwise WebKit's termination API leaves worker threads alive. */
        const int result = process_kill_group(pid, 128 + 9);
        return result == 0 ? 0u : linux_error(result == -2 ? LINUX_EPERM : LINUX_ESRCH);
    }
    case 11:
    case 15:
        return process_kill(pid) == 0 ? 0u : linux_error(LINUX_EINVAL);
    case LINUX_SIGUSR1:
        return vex_event_post(pid, 0u, VEX_EVENT_USER_INTERRUPT) == 0
            ? 0u : linux_error(LINUX_EPERM);
    case LINUX_SIGCANCEL:
        return vex_event_post(pid, 0u, LINUX_EVENT_SIGCANCEL) == 0
            ? 0u : linux_error(LINUX_EPERM);
    case LINUX_SIGSETXID:
        return vex_event_post(pid, 0u, LINUX_EVENT_SIGSETXID) == 0
            ? 0u : linux_error(LINUX_EPERM);
    default:
        return linux_error(LINUX_ENOSYS);
    }
}

static uint64_t linux_signal_thread_target(uint32_t tid, int sig) {
    SCHEDULER_TASK_REF(target_ref);
    process_t *target=scheduler_task_acquire(&target_ref,tid,0);
    if (sig == 9) {
        return target != NULL && target->group != NULL
            ? linux_signal_target(target->group->process_id, sig)
            : linux_error(LINUX_ESRCH);
    }
    if (sig == 0) {
        return target != NULL && target->state != PROC_ZOMBIE ? 0u : linux_error(LINUX_EINVAL);
    }
    if (sig == LINUX_SIGUSR1) {
        const uint32_t process_id = target == NULL || target->group == NULL
            ? 0u : target->group->process_id;
        return process_id != 0u &&
               vex_event_post(process_id, tid, VEX_EVENT_USER_INTERRUPT) == 0
            ? 0u : linux_error(LINUX_EPERM);
    }
    if (sig == LINUX_SIGCANCEL || sig == LINUX_SIGSETXID) {
        const uint32_t process_id = target == NULL || target->group == NULL
            ? 0u : target->group->process_id;
        const uint16_t event_id = sig == LINUX_SIGCANCEL
            ? LINUX_EVENT_SIGCANCEL : LINUX_EVENT_SIGSETXID;
        return process_id != 0u &&
               vex_event_post(process_id, tid, event_id) == 0
            ? 0u : linux_error(LINUX_EPERM);
    }
    if (sig == LINUX_SIGABRT) {
        process_t *caller = scheduler_current_process();
        /* abort() resets SIGABRT to its default disposition and sends it to
           the calling thread with tgkill.  The default Linux action is
           process-wide; treating this as process_kill(tid) leaves sibling
           threads running in a half-aborted process.  Preserve the native
           process_group ownership model and terminate the caller's group. */
        if (target != NULL && caller != NULL && caller->group != NULL &&
            target->group == caller->group) {
            return linux_signal_target(target->group->process_id, sig);
        }
    }
    return linux_signal_target(tid, sig);
}

static uint64_t linux_statx(uint64_t dirfd, uint64_t path_addr, uint64_t flags, uint64_t buf) {
    struct stat vex_st;
    linux_statx_t *sx = (linux_statx_t *)(uintptr_t)buf;
    const char *path = (const char *)(uintptr_t)path_addr;
    char resolved[1024];
    int result;
    int by_fd;

    if ((flags & ~(uint64_t)(0x100u | 0x800u | 0x1000u | 0x6000u)) != 0u ||
        (flags & 0x6000u) == 0x6000u) {
        return linux_error(LINUX_EINVAL);
    }
    if (!vex_syscall_user_range_valid(buf, sizeof(*sx), 1) ||
        !vex_syscall_user_string_valid(path_addr, sizeof(resolved))) {
        return linux_error(LINUX_EFAULT);
    }
    by_fd = path[0] == '\0' && (flags & LINUX_AT_EMPTY_PATH) != 0u;
    /* dirfd is an ABI int: x86-64 callers may zero-extend its low 32 bits.
     * AT_EMPTY_PATH permits an empty path; it does not ignore a nonempty one. */
    if (by_fd) {
        result = fstat((int32_t)dirfd, &vex_st);
    } else {
        if (path[0] == '\0') return linux_error(LINUX_ENOENT);
        if (ladybird_resolve_dirfd_path((int32_t)dirfd, path, resolved,
                                       sizeof(resolved)) != 0)
            return linux_result((uint64_t)-1);
        result = (flags & 0x100u) != 0u ? lstat(resolved, &vex_st)
                                       : stat(resolved, &vex_st);
    }
    if (result != 0) return linux_result((uint64_t)-1);

    compat_zero(sx, sizeof(*sx));
    /* Expose only metadata actually supplied by the backend. */
    sx->stx_mask = 0x71fu;
    vex_file_times_t times = {0};
    int times_result = by_fd ? vex_web_fd_times((int32_t)dirfd, &times)
                            : fs_get_times(resolved, &times);
    if (times_result == 0) {
        linux_statx_timestamp_t *fields[] = {&sx->stx_atime,&sx->stx_mtime,
                                           &sx->stx_ctime,&sx->stx_btime};
        const uint32_t bits[] = {0x20u,0x40u,0x80u,0x800u};
        for (unsigned i=0;i<4;i++) if(times.valid & (1u<<i)) {
            fields[i]->tv_sec=times.seconds[i];
            fields[i]->tv_nsec=times.nanoseconds[i];
            sx->stx_mask |= bits[i];
        }
    }
    sx->stx_blksize = vex_st.st_blksize != 0u ? (uint32_t)vex_st.st_blksize : 4096u;
    sx->stx_nlink = 1u;
    sx->stx_uid = vex_st.st_uid;
    sx->stx_gid = vex_st.st_gid;
    sx->stx_mode = (uint16_t)vex_st.st_mode;
    if (by_fd) {
        const uint64_t inode = ladybird_fd_inode_hint((int)dirfd);
        sx->stx_ino = inode != 0u ? inode : 1u;
    } else {
        sx->stx_ino = linux_path_inode(resolved);
    }
    sx->stx_size = (uint64_t)vex_st.st_size;
    sx->stx_blocks = vex_st.st_blocks != 0u ? (uint64_t)vex_st.st_blocks : (uint64_t)((vex_st.st_size + 511) / 512);
    return 0u;
}

static uint64_t linux_statfs_fill(uint64_t buf) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    fs_storage_info_t storage;
    linux_statfs_t *out = (linux_statfs_t *)(uintptr_t)buf;

    if (out == NULL || !vex_syscall_user_range_valid(buf, sizeof(*out), 1) ||
        fs_get_storage_info(&storage) != 0 || storage.block_size == 0u) {
        return linux_error(LINUX_EFAULT);
    }

    compat_zero(out, sizeof(*out));
    out->f_type = LINUX_VEXFS_MAGIC;
    out->f_bsize = (int64_t)storage.block_size;
    out->f_frsize = (int64_t)storage.block_size;
    out->f_blocks = (int64_t)storage.total_blocks;
    out->f_bfree = (int64_t)storage.free_blocks;
    out->f_bavail = (int64_t)storage.free_blocks;
    out->f_files = (int64_t)storage.file_slots;
    out->f_ffree = storage.used_files > storage.file_slots
        ? 0 : (int64_t)(storage.file_slots - storage.used_files);
    out->f_namelen = 127;
    /* The RAM-backed fallback is writable too; it simply has no persistent
     * blocks to report, so do not advertise ST_RDONLY here. */
    out->f_flags = 0;
    out->f_fsid[0] = 0x5645584f; /* VEXO */
    out->f_fsid[1] = (int32_t)(storage.total_blocks ^ storage.used_blocks);
    return 0u;
}

static uint64_t linux_statfs_path(uint64_t path_addr, uint64_t buf) {
    char resolved[256];
    if (path_addr == 0u || !vex_syscall_user_string_valid(path_addr, sizeof(resolved)) ||
        ladybird_resolve_dirfd_path((int)LINUX_AT_FDCWD,
            (const char *)(uintptr_t)path_addr, resolved, sizeof(resolved)) != 0) {
        return linux_error(LINUX_EFAULT);
    }
    /* Resolving the path also preserves the native access/ENOENT contract. */
    {
        struct stat st;
        if (stat(resolved, &st) != 0) {
            return linux_result((uint64_t)-1);
        }
    }
    return linux_statfs_fill(buf);
}

static uint64_t linux_gettimeofday(uint64_t tv_addr, uint64_t tz_addr) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_gettimeofday_req_t req;

    if (tv_addr == 0u ||
        !vex_syscall_user_range_valid(tv_addr, sizeof(struct timeval), 1) ||
        (tz_addr != 0u &&
         !vex_syscall_user_range_valid(tz_addr, sizeof(int32_t) * 2u, 1))) {
        return linux_error(LINUX_EFAULT);
    }
    req.tv = (struct timeval *)(uintptr_t)tv_addr;
    req.tz = (void *)(uintptr_t)tz_addr;
    return linux_result(vex_syscall_dispatch(VEX_SYS_GETTIMEOFDAY,
        (uint64_t)(uintptr_t)&req, 0, 0));
}

static uint64_t linux_read_stack_limit(uint64_t user_addr) {
    const size_t stack_size = process_memory_stack_size(scheduler_current_process_id());
    uint64_t limits[2];

    if (user_addr == 0u || !vex_syscall_user_range_valid(user_addr, sizeof(limits), 1)) {
        return linux_error(LINUX_EFAULT);
    }
    if (stack_size == 0u) {
        return linux_error(LINUX_ENOSYS);
    }
    /* VexOS currently reserves a fixed user stack for an ELF process.  The
     * soft and hard Linux views must both describe that actual reservation;
     * promising an expandable or unlimited stack would mislead glibc/JSC. */
    limits[0] = (uint64_t)stack_size;
    limits[1] = (uint64_t)stack_size;
    if(vex_copy_to_user(user_addr,limits,sizeof(limits)))return linux_error(LINUX_EFAULT);
    return 0u;
}

static uint64_t compat_linux_syscall_impl(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    /* Native VexOS SHM is exposed to Linux-ABI processes only through an
     * explicit extension namespace.  This preserves Linux syscall 161
     * (chroot), 162 (sync), and 163 (acct) while allowing WebKit to acquire
     * process-owned SHM capabilities. */
    switch (number) {
    case VEX_SYS_LINUX_SHM_OPEN:
        return vex_syscall_dispatch_user(VEX_SYS_SHM_OPEN, arg1, arg2, arg3);
    case VEX_SYS_LINUX_SHM_CLOSE:
        return vex_syscall_dispatch_user(VEX_SYS_SHM_CLOSE, arg1, arg2, arg3);
    case VEX_SYS_LINUX_SHM_MAP:
        return vex_syscall_dispatch_user(VEX_SYS_SHM_MAP, arg1, arg2, arg3);
    default:
        break;
    }
    switch (number) {
    case LINUX_SYS_READ:
        {
            const uint64_t raw = (uint64_t)linux_read(arg1, arg2, arg3);
            if (arg1 >= 7u && arg1 <= 32u && arg3 <= 4096u) {
                linux_spawn_trace("read", arg1, arg3, 0u, raw);
            }
            return linux_result(raw);
        }
    case LINUX_SYS_WRITE:
        {
            const uint64_t raw = (uint64_t)linux_write(arg1, arg2, arg3);
            if (arg1 >= 7u && arg1 <= 32u && arg3 <= 4096u) {
                linux_spawn_trace("write", arg1, arg3, 0u, raw);
            }
            return linux_result(raw);
        }
    case LINUX_SYS_READV:
        return linux_readv(arg1, arg2, arg3);
    case LINUX_SYS_WRITEV:
        return linux_writev(arg1, arg2, arg3);
    case LINUX_SYS_OPEN: {
        VEX_SCRATCH(path_ref);
        char *path=vex_scratch_allocate(&path_ref,4096u);
        if(!path)return linux_error(LINUX_ENOMEM);
        if(vex_user_string_copy(path,4096u,arg1))return linux_error(LINUX_EFAULT);
        return linux_result(vex_syscall_dispatch(VEX_SYS_OPEN,(uint64_t)(uintptr_t)path,arg2,0));
    }
    case LINUX_SYS_CLOSE:
        {
            const uint64_t raw = vex_syscall_dispatch(VEX_SYS_CLOSE, arg1, 0, 0);
            linux_spawn_trace("close", arg1, 0u, 0u, raw);
            return linux_result(raw);
        }
    case LINUX_SYS_EVENTFD:
        return linux_result((uint64_t)vex_web_eventfd_create(arg1, 0u));
    case LINUX_SYS_EVENTFD2:
        return linux_result((uint64_t)vex_web_eventfd_create(arg1, (uint32_t)arg2));
    case LINUX_SYS_TIMERFD_CREATE:
        return linux_result((uint64_t)vex_web_timerfd_create((int)arg1,
                                                               (uint32_t)arg2));
    case LINUX_SYS_TIMERFD_SETTIME:
        {
            vex_linux_itimerspec_t new_value, old_value;
            const uint64_t user_size = sizeof(vex_linux_itimerspec_t);
            if (!vex_syscall_user_range_valid(arg3, user_size, 0) ||
                (arg4 != 0u && !vex_syscall_user_range_valid(arg4, user_size, 1))) {
                return linux_error(LINUX_EFAULT);
            }
            memcpy(&new_value, (const void *)(uintptr_t)arg3, user_size);
            if (vex_web_timerfd_settime((int)arg1, (uint32_t)arg2,
                                        &new_value,
                                        arg4 != 0u ? &old_value : NULL) != 0) {
                return linux_result((uint64_t)-1);
            }
            if (arg4 != 0u) {
                memcpy((void *)(uintptr_t)arg4, &old_value, user_size);
            }
            return 0u;
        }
    case LINUX_SYS_TIMERFD_GETTIME:
        {
            vex_linux_itimerspec_t current_value;
            const uint64_t user_size = sizeof(vex_linux_itimerspec_t);
            if (!vex_syscall_user_range_valid(arg2, user_size, 1)) {
                return linux_error(LINUX_EFAULT);
            }
            if (vex_web_timerfd_gettime((int)arg1, &current_value) != 0) {
                return linux_result((uint64_t)-1);
            }
            memcpy((void *)(uintptr_t)arg2, &current_value, user_size);
            return 0u;
        }
    case LINUX_SYS_STAT:
        return linux_stat_path(arg1, arg2, VEX_SYS_STAT);
    case LINUX_SYS_FSTAT:
        return linux_fstat(arg1, arg2);
    case LINUX_SYS_LSTAT:
        return linux_stat_path(arg1, arg2, VEX_SYS_LSTAT);
    case LINUX_SYS_STATFS:
        return linux_statfs_path(arg1, arg2);
    case LINUX_SYS_FSTATFS:
        {
            struct stat st;
            if (vex_syscall_dispatch(VEX_SYS_FSTAT, arg1,
                    (uint64_t)(uintptr_t)&st, 0) == (uint64_t)-1) {
                return linux_error(LINUX_EBADF);
            }
            return linux_statfs_fill(arg2);
        }
    case LINUX_SYS_LSEEK:
        return linux_result(vex_syscall_dispatch(VEX_SYS_LSEEK, arg1, arg2, arg3));
    case LINUX_SYS_MMAP:
        return linux_mmap(arg1, arg2, arg3, arg4, arg5, arg6);
    case LINUX_SYS_MUNMAP:
        {
            vex_memory_req_t req;
            uint64_t result;
            req.addr = (void *)(uintptr_t)arg1;
            req.length = (size_t)arg2;
            req.prot_or_advice = 0;
            result = vex_syscall_dispatch(VEX_SYS_MUNMAP,
                (uint64_t)(uintptr_t)&req, 0, 0);
            linux_memory_trace("munmap", arg1, arg2, 0u, 0u, 0u, 0u,
                               result);
            return result;
        }
    case LINUX_SYS_MPROTECT:
        return linux_mprotect(arg1, arg2, arg3);
    case LINUX_SYS_BRK:
        return linux_brk(arg1);
    case LINUX_SYS_RT_SIGACTION:
        {
            const uint64_t result = linux_rt_sigaction(arg1, arg2, arg3, arg4);
            linux_signal_action_trace(arg1, arg2, arg3, arg4, result);
            return result;
        }
    case LINUX_SYS_RT_SIGPROCMASK:
        {
            const uint64_t result = linux_rt_sigprocmask(arg1, arg2, arg3, arg4);
            linux_signal_mask_trace(arg1, arg2, arg3, arg4, result);
            return result;
        }
    case LINUX_SYS_RT_SIGRETURN:
        return linux_rt_sigreturn();
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
        {
            const uint64_t raw = linux_pipe(arg1, 0u);
            linux_spawn_trace("pipe", arg1, 0u, 0u, raw);
            return linux_result(raw);
        }
    case LINUX_SYS_PIPE2:
        {
            const uint64_t raw = linux_pipe(arg1, arg2);
            linux_spawn_trace("pipe2", arg1, arg2, 0u, raw);
            return linux_result(raw);
        }
    case LINUX_SYS_SOCKET:
        {
            uint64_t result=linux_result(vex_syscall_dispatch(VEX_SYS_SOCKET,arg1,arg2,arg3));
            static unsigned traces;
            if(arg1!=AF_UNIX && traces++<32) {
                debug_log_puts("network socket: pid=");debug_log_u64_hex(scheduler_current_pid());
                debug_log_puts(" domain=");debug_log_u64_hex(arg1);
                debug_log_puts(" type=");debug_log_u64_hex(arg2);
                debug_log_puts(" protocol=");debug_log_u64_hex(arg3);
                debug_log_puts(" result=");debug_log_u64_hex(result);debug_log_puts("\n");
            }
            return result;
        }
    case LINUX_SYS_GETSOCKNAME:
        return linux_getsockname(arg1, arg2, arg3);
    case LINUX_SYS_GETPEERNAME:
        return linux_getpeername(arg1, arg2, arg3);
    case LINUX_SYS_CONNECT:
        return linux_sockaddr_call(arg1, arg2, arg3, VEX_SYS_CONNECT);
    case LINUX_SYS_BIND:
        return linux_sockaddr_call(arg1, arg2, arg3, VEX_SYS_BIND);
    case LINUX_SYS_ACCEPT:
        {
            uint32_t capacity=0;
            if(arg3 && vex_copy_from_user(&capacity,arg3,sizeof(capacity)))return linux_error(LINUX_EFAULT);
            if(arg3 && (!vex_syscall_user_range_valid(arg3,sizeof(capacity),1) ||
                         !vex_syscall_user_range_valid(arg2,capacity,1)))return linux_error(LINUX_EFAULT);
            vex_accept_req_t req;
            req.sockfd = (int)arg1;
            req.addr = (struct sockaddr *)(uintptr_t)arg2;
            req.addrlen = (uint32_t *)(uintptr_t)arg3;
            return vex_syscall_dispatch(VEX_SYS_ACCEPT, (uint64_t)(uintptr_t)&req, 0, 0);
        }
    case LINUX_SYS_SENDTO:
        return linux_datagram_io(arg1,arg2,arg3,arg4,arg5,arg6,1);
    case LINUX_SYS_RECVFROM:
        return linux_datagram_io(arg1,arg2,arg3,arg4,arg5,arg6,0);
    case LINUX_SYS_SENDMSG:
        return linux_sendmsg(arg1,arg2,arg3);
    case LINUX_SYS_RECVMSG:
        return linux_recvmsg(arg1,arg2,arg3);
    case LINUX_SYS_SENDMMSG: {
        if(arg3>1024u)return linux_error(LINUX_EINVAL);
        const size_t stride=(sizeof(linux_msghdr_t)+sizeof(uint32_t)+7u)&~7u;
        if(arg3 && (arg2>UINT64_MAX-arg3*stride ||
           !vex_syscall_user_range_valid(arg2,(size_t)arg3*stride,1)))return linux_error(LINUX_EFAULT);
        for(uint64_t i=0;i<arg3;++i) {
            uint64_t address=arg2+i*stride,result=linux_sendmsg(arg1,address,arg4);
            if((int64_t)result<0)return i?i:result;
            uint32_t length=(uint32_t)result;
            if(vex_copy_to_user(address+sizeof(linux_msghdr_t),&length,sizeof(length)))
                return i?i:linux_error(LINUX_EFAULT);
        }
        return arg3;
    }
    case LINUX_SYS_SETSOCKOPT:
    case LINUX_SYS_GETSOCKOPT:
        {
            kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
            uint32_t length;
            if(number==LINUX_SYS_SETSOCKOPT){
                if(arg5>4096u || !vex_syscall_user_range_valid(arg4,arg5,0))return linux_error(LINUX_EFAULT);
                length=(uint32_t)arg5;
                return linux_result((uint64_t)setsockopt((int)arg1,(int)arg2,(int)arg3,(const void *)(uintptr_t)arg4,length));
            }
            if(!vex_syscall_user_range_valid(arg5,sizeof(length),1))return linux_error(LINUX_EFAULT);
            memcpy(&length,(const void *)(uintptr_t)arg5,sizeof(length));
            if(length>4096u || !vex_syscall_user_range_valid(arg4,length,1))return linux_error(LINUX_EFAULT);
            uint64_t result=linux_result((uint64_t)getsockopt((int)arg1,(int)arg2,(int)arg3,(void *)(uintptr_t)arg4,&length));
            if(!result)memcpy((void *)(uintptr_t)arg5,&length,sizeof(length));
            return result;
        }
    case LINUX_SYS_CLONE:
        linux_spawn_trace("clone.enter", arg1, arg2, arg3, arg4);
        if ((arg1 & LINUX_CLONE_VM) != 0u &&
            (arg1 & LINUX_CLONE_THREAD) != 0u) {
            if (arg2 <= LINUX_SYSCALL_FRAME_SIZE ||
                !vex_syscall_user_range_valid(arg2 - LINUX_SYSCALL_FRAME_SIZE,
                    LINUX_SYSCALL_FRAME_SIZE, 1) ||
                ((arg1 & LINUX_CLONE_PARENT_SETTID) != 0u &&
                 !vex_syscall_user_range_valid(arg3, sizeof(uint32_t), 1)) ||
                ((arg1 & (LINUX_CLONE_CHILD_CLEARTID | LINUX_CLONE_CHILD_SETTID)) != 0u &&
                 arg4 != 0u && !vex_syscall_user_range_valid(arg4, sizeof(uint32_t), 1))) {
                return linux_error(LINUX_EFAULT);
            }
            const uint32_t tid = process_clone_thread(arg2, arg1, arg3, arg4, arg5);
            linux_spawn_trace("clone.thread", arg1, arg2, tid, arg5);
            return tid == 0u ? linux_error(LINUX_EINVAL) : (uint64_t)tid;
        }
        /* glibc's posix_spawn path uses clone(CLONE_VM|CLONE_VFORK|SIGCHLD,
           ...).  That is a process creation request even though CLONE_VM is
           present.  VexOS has no native vfork address-space sharing contract;
           translate this narrowly to an independent Process fork.  The
           parent/child exec handshake remains observable through the caller's
           CLOEXEC error pipe. */
        if ((arg1 & LINUX_CLONE_VM) == 0u ||
            (arg1 & LINUX_CLONE_VFORK) != 0u ||
            (arg1 & 0xffu) == LINUX_CLONE_SIGCHLD) {
            if ((arg1 & LINUX_CLONE_PARENT_SETTID) != 0u &&
                (arg3 == 0u || !vex_syscall_user_range_valid(arg3, sizeof(uint32_t), 1))) {
                return linux_error(LINUX_EFAULT);
            }
            if ((arg1 & (LINUX_CLONE_CHILD_SETTID | LINUX_CLONE_CHILD_CLEARTID)) &&
                arg4 && !vex_syscall_user_range_valid(arg4,sizeof(uint32_t),1)) {
                return linux_error(LINUX_EFAULT);
            }
            {
                const uint32_t child_pid = process_fork_current_tid(arg4,
                    (arg1 & LINUX_CLONE_CHILD_SETTID)!=0,
                    (arg1 & LINUX_CLONE_CHILD_CLEARTID)!=0);
                if (child_pid == 0u) {
                    linux_spawn_trace("clone.fork.fail", arg1, arg2, arg3, 0u);
                    return linux_error(LINUX_EAGAIN);
                }
                if ((arg1 & LINUX_CLONE_PARENT_SETTID) != 0u) {
                    *(uint32_t *)(uintptr_t)arg3 = child_pid;
                }
                linux_spawn_trace("clone.fork.return", arg1, arg2, child_pid,
                                  child_pid);
                return (uint64_t)child_pid;
            }
        }
        return linux_error(LINUX_ENOSYS);
    case LINUX_SYS_CLONE3:
        {
            linux_clone_args_t args;
            const uint64_t supported_flags =
                LINUX_CLONE_VM | LINUX_CLONE_VFORK | LINUX_CLONE_THREAD |
                LINUX_CLONE_SETTLS_FLAG | LINUX_CLONE_PARENT_SETTID |
                LINUX_CLONE_CHILD_CLEARTID | LINUX_CLONE_CHILD_SETTID |
                LINUX_CLONE_CLEAR_SIGHAND;
            uint64_t stack_top;
            uint32_t child_pid;

            linux_spawn_trace("clone3.enter", arg1, arg2, 0u, 0u);

            memset(&args, 0, sizeof(args));
            if (arg1 == 0u || arg2 < 64u || arg2 > 4096u) {
                return linux_error(LINUX_EINVAL);
            }
            if (!vex_syscall_user_range_valid(arg1, arg2, 0)) {
                return linux_error(LINUX_EFAULT);
            }
            memcpy(&args, (const void *)(uintptr_t)arg1,
                arg2 < sizeof(args) ? (size_t)arg2 : sizeof(args));

            if ((args.flags & ~supported_flags) != 0u ||
                args.pidfd != 0u || args.set_tid != 0u ||
                args.set_tid_size != 0u || args.cgroup != 0u) {
                return linux_error(LINUX_ENOSYS);
            }
            if (args.parent_tid != 0u &&
                !vex_syscall_user_range_valid(args.parent_tid, sizeof(uint32_t), 1)) {
                return linux_error(LINUX_EFAULT);
            }
            if ((args.flags & (LINUX_CLONE_CHILD_CLEARTID | LINUX_CLONE_CHILD_SETTID)) != 0u &&
                args.child_tid != 0u &&
                !vex_syscall_user_range_valid(args.child_tid, sizeof(uint32_t), 1)) {
                return linux_error(LINUX_EFAULT);
            }
            if (args.stack == 0u || args.stack_size == 0u ||
                args.stack > UINT64_MAX - args.stack_size) {
                return linux_error(LINUX_EFAULT);
            }
            if (!vex_syscall_user_range_valid(args.stack, args.stack_size, 1)) {
                return linux_error(LINUX_EFAULT);
            }
            stack_top = args.stack + args.stack_size;

            if ((args.flags & LINUX_CLONE_VM) != 0u &&
                (args.flags & LINUX_CLONE_THREAD) != 0u) {
                child_pid = process_clone_thread(stack_top, args.flags,
                    args.parent_tid, args.child_tid, args.tls);
            } else {
                /* glibc posix_spawn uses CLONE_VM|CLONE_VFORK with a
                   SIGCHLD exit signal.  VexOS gives it an independent
                   process/address space while preserving the supplied child
                   stack; exec/error completion is handled by the native
                   process lifecycle rather than exposing clone3 internals. */
                if ((args.flags & LINUX_CLONE_THREAD) != 0u ||
                    (args.exit_signal != 0u && args.exit_signal != LINUX_CLONE_SIGCHLD)) {
                    return linux_error(LINUX_EINVAL);
                }
                if ((args.flags & (LINUX_CLONE_CHILD_CLEARTID | LINUX_CLONE_CHILD_SETTID)) != 0u) {
                    return linux_error(LINUX_ENOSYS);
                }
                child_pid = process_fork_current_with_stack(stack_top);
                if (child_pid != 0u && args.parent_tid != 0u) {
                    *(uint32_t *)(uintptr_t)args.parent_tid = child_pid;
                }
                if (child_pid != 0u) {
                    /* CLONE_VFORK keeps the parent stopped until the child
                       has committed execve or exited, preserving glibc's
                       temporary child stack until that point. */
                    process_wait_spawn(child_pid);
                }
            }

            linux_spawn_trace("clone3.return", args.flags, stack_top,
                              child_pid, child_pid);

            return child_pid == 0u ? linux_error(LINUX_EAGAIN) : (uint64_t)child_pid;
        }
    case LINUX_SYS_VFORK:
        {
            const uint32_t child_pid = process_fork_current();
            return child_pid == 0u ? linux_error(LINUX_EAGAIN) : (uint64_t)child_pid;
        }
    case LINUX_SYS_FORK:
        {
            const uint32_t child_pid = process_fork_current();
            return child_pid == 0u ? linux_error(LINUX_EINVAL) : (uint64_t)child_pid;
        }
    case LINUX_SYS_EXECVE:
        {
            vex_process_exec_req_t req;
            uint64_t raw_result;
            uint64_t result;
            req.path = (const char *)(uintptr_t)arg1;
            req.argv = (char *const *)(uintptr_t)arg2;
            req.envp = (char *const *)(uintptr_t)arg3;
            linux_spawn_trace("exec.enter", arg1, arg2, arg3, 0u);
            raw_result = vex_syscall_dispatch(VEX_SYS_PROCESS_EXEC,
                (uint64_t)(uintptr_t)&req, 0, 0);
            result = raw_result == 0u ? 0u : linux_result((uint64_t)-1);
            linux_spawn_trace("exec.return", arg1, raw_result,
                              (uint64_t)(unsigned int)*ladybird_errno_location(),
                              result);
            return result;
        }
    case LINUX_SYS_ARCH_PRCTL:
        {
            const uint64_t result = linux_arch_prctl(arg1, arg2);
            linux_signal_trace("arch_prctl", arg1, arg2,
                               scheduler_current_fs_base(), 0u, result);
            return result;
        }
    case LINUX_SYS_GETPID:
        return linux_bulk_identity(VEX_BULK_PID, VEX_SYS_GETPID);
    case LINUX_SYS_GETTID:
        return scheduler_current_tid();
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
            int raw_status = 0;
            uint32_t reaped_pid = 0u;
            if (status != NULL && !vex_syscall_user_range_valid(arg2, sizeof(*status), 1)) {
                return linux_error(LINUX_EFAULT);
            }
            const int result = process_wait_pid((int32_t)arg1, status != NULL ? &raw_status : NULL, (uint32_t)arg3, &reaped_pid);
            if (result >= 0 && status != NULL) {
                *status = linux_wait_status_from_vex(raw_status);
            }
            linux_spawn_trace("wait4", arg1, (uint64_t)(uint32_t)raw_status,
                              reaped_pid, result < 0 ? linux_error(LINUX_EINVAL) : (uint64_t)result);
            return result < 0 ? linux_error(LINUX_EINVAL) : (uint64_t)result;
        }
    case LINUX_SYS_FCNTL:
        {
            const int command = (int)arg2;
            if (command == 5 || command == 6 || command == 7) {
                return linux_result((uint64_t)(int64_t)vex_linux_fcntl_lock(
                    (int)arg1, command,
                    (vex_linux_flock_t *)(uintptr_t)arg3));
            }
            const uint64_t raw = vex_syscall_dispatch(VEX_SYS_FCNTL, arg1, arg2, arg3);
            const uint64_t result = linux_result(raw);
            linux_spawn_trace("fcntl", arg1, arg2, arg3, result);
            return result;
        }
    case LINUX_SYS_FUTEX:
        {
            const uint32_t op = (uint32_t)arg2 & LINUX_FUTEX_CMD_MASK;
            uint64_t result;
            if (!vex_syscall_user_range_valid(arg1, sizeof(uint32_t), 0)) {
                result = linux_error(LINUX_EFAULT);
                linux_signal_trace("futex", arg1, arg2, arg3, arg4, result);
                return result;
            }
            switch (op) {
            case 6u: /* LOCK_PI: explicit owner protocol, not ordinary WAIT */
            case 7u: /* UNLOCK_PI */
            case 8u: /* TRYLOCK_PI */
                if(((uint32_t)arg2 & ~127u)!=LINUX_FUTEX_PRIVATE_FLAG || (op==6u && arg4))
                    return linux_error(LINUX_ENOSYS);
                return (uint64_t)(int64_t)(op==7u?linux_futex_pi_unlock(arg1):linux_futex_pi_lock(arg1,op==8u));
            case LINUX_FUTEX_WAIT:
            case LINUX_FUTEX_WAIT_BITSET:
                result = futex_wait(arg1, (uint32_t)arg3) == 0
                    ? 0u : linux_error(LINUX_EAGAIN);
                linux_signal_trace("futex", arg1, arg2, arg3, arg4, result);
                return result;
            case LINUX_FUTEX_WAKE:
            case LINUX_FUTEX_WAKE_BITSET:
                result = (uint64_t)futex_wake(arg1, (uint32_t)arg3);
                linux_futex_context_trace(arg1, arg2, arg3, result);
                linux_signal_trace("futex", arg1, arg2, arg3, arg4, result);
                return result;
            default:
                result = linux_error(LINUX_ENOSYS);
                linux_signal_trace("futex", arg1, arg2, arg3, arg4, result);
                return result;
            }
        }
    case LINUX_SYS_GETCWD:
        {
            char cwd[4096];
            vex_getcwd_req_t req = {cwd, sizeof(cwd)};
            if (vex_syscall_dispatch(VEX_SYS_GETCWD, (uint64_t)(uintptr_t)&req, 0, 0) == 0u)
                return linux_result((uint64_t)-1);
            const size_t length = strlen(cwd) + 1u;
            /* Native getcwd returns a pointer. Linux's raw syscall returns
             * the byte count INCLUDING NUL; glibc stores it in an int. */
            if (arg2 < length) return linux_error(34u); /* ERANGE */
            if (vex_copy_to_user(arg1,cwd,length))
                return linux_error(LINUX_EFAULT);
            return length;
        }
    case LINUX_SYS_CHDIR: {
        VEX_SCRATCH(path_ref);
        char *path=vex_scratch_allocate(&path_ref,4096u);
        if(!path)return linux_error(LINUX_ENOMEM);
        if(vex_user_string_copy(path,4096u,arg1))return linux_error(LINUX_EFAULT);
        return linux_result(vex_syscall_dispatch(VEX_SYS_CHDIR,(uint64_t)(uintptr_t)path,0,0));
    }
    case 201: { /* Linux time(): epoch seconds, not uptime. */
        struct timespec now;
        if(arg1 && !vex_syscall_user_range_valid(arg1,sizeof(int64_t),1))return linux_error(LINUX_EFAULT);
        if(clock_gettime(0,&now))return linux_error(LINUX_EINVAL);
        if(arg1 && vex_copy_to_user(arg1,&now.tv_sec,sizeof(int64_t)))return linux_error(LINUX_EFAULT);
        return now.tv_sec;
    }
    case LINUX_SYS_GETTIMEOFDAY:
        return linux_gettimeofday(arg1, arg2);
    case LINUX_SYS_CLOCK_GETTIME:
        {
            struct timespec ts;
            if(clock_gettime((int)arg1,&ts))return linux_error(LINUX_EINVAL);
            return vex_copy_to_user(arg2,&ts,sizeof(ts))?linux_error(LINUX_EFAULT):0u;
        }
    case LINUX_SYS_OPENAT:
        {
        VEX_SCRATCH(path_ref);
        char *path=vex_scratch_allocate(&path_ref,4096u);
        if(!path)return linux_error(LINUX_ENOMEM);
        if(vex_user_string_copy(path,4096u,arg2))return linux_error(LINUX_EFAULT);
        if (arg1 == LINUX_AT_FDCWD) {
            return linux_result(vex_syscall_dispatch(VEX_SYS_OPEN,(uint64_t)(uintptr_t)path,arg3,0));
        }
        return linux_result(vex_syscall_dispatch(VEX_SYS_OPENAT,arg1,(uint64_t)(uintptr_t)path,arg3));
        }
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
    case LINUX_SYS_MKDIRAT:
        {
            char resolved[256];
            if (!vex_syscall_user_string_valid(arg2, sizeof(resolved))) {
                return linux_error(LINUX_EFAULT);
            }
            if (ladybird_resolve_dirfd_path((int)arg1,
                    (const char *)(uintptr_t)arg2, resolved, sizeof(resolved)) != 0) {
                return linux_result((uint64_t)-1);
            }
            return linux_result((uint64_t)(int64_t)mkdir(resolved,
                                                         (uint32_t)arg3));
        }
    case LINUX_SYS_RMDIR:
        return linux_result(vex_syscall_dispatch(VEX_SYS_RMDIR, arg1, 0, 0));
    case LINUX_SYS_UNLINK:
        return linux_result(vex_syscall_dispatch(VEX_SYS_UNLINK, arg1, 0, 0));
    case LINUX_SYS_READLINKAT:
        {
            char resolved[256];
            if (ladybird_resolve_dirfd_path((int)arg1, (const char *)(uintptr_t)arg2, resolved, sizeof(resolved)) != 0) {
                return linux_result((uint64_t)-1);
            }
            return linux_result((uint64_t)readlink(resolved, (char *)(uintptr_t)arg3, (size_t)arg4));
        }
    case LINUX_SYS_NEWFSTATAT:
        {
            char resolved[4096];
            if(arg4 & ~(LINUX_AT_EMPTY_PATH | 0x100u | 0x800u))return linux_error(LINUX_EINVAL);
            if(!vex_syscall_user_range_valid(arg3,sizeof(linux_stat_t),1) ||
               !vex_syscall_user_string_valid(arg2,sizeof(resolved)))return linux_error(LINUX_EFAULT);
            if ((arg4 & LINUX_AT_EMPTY_PATH) != 0u && ((const char *)(uintptr_t)arg2)[0] == '\0') {
                return linux_fstat(arg1, arg3);
            }
            if (ladybird_resolve_dirfd_path((int)arg1, (const char *)(uintptr_t)arg2, resolved, sizeof(resolved)) != 0) {
                return linux_result((uint64_t)-1);
            }
            return linux_stat_resolved_path(resolved, arg3, arg4);
        }
    case LINUX_SYS_SET_TID_ADDRESS:
        if (arg1 != 0u && !vex_syscall_user_range_valid(arg1, sizeof(uint32_t), 1)) {
            return linux_error(LINUX_EFAULT);
        }
        process_set_clear_child_tid(arg1);
        return scheduler_current_tid();
    case LINUX_SYS_SET_ROBUST_LIST:
        return linux_error(LINUX_ENOSYS);
    case LINUX_SYS_PRLIMIT64:
        if (arg1 != 0u && arg1 != scheduler_current_process_id()) {
            return linux_error(LINUX_ENOSYS);
        }
        if (arg2 != LINUX_RLIMIT_STACK || arg3 != 0u) {
            return linux_error(LINUX_ENOSYS);
        }
        return arg4 == 0u ? 0u : linux_read_stack_limit(arg4);
    case LINUX_SYS_GETRANDOM:
        {
            vex_getrandom_req_t req;
            req.buffer = (void *)(uintptr_t)arg1;
            req.length = (size_t)arg2;
            req.flags = (unsigned int)arg3;
            return linux_result(vex_syscall_dispatch(VEX_SYS_GETRANDOM, (uint64_t)(uintptr_t)&req, 0, 0));
        }
    case LINUX_SYS_RSEQ:
        return linux_error(LINUX_ENOSYS);
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
            return vex_syscall_dispatch(VEX_SYS_MADVISE, (uint64_t)(uintptr_t)&req, 0, 0);
        }
    case LINUX_SYS_MSYNC:
        return linux_error(LINUX_ENOSYS);
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
    case LINUX_SYS_SCHED_SETSCHEDULER:
        return linux_sched_setscheduler(arg1, arg2, arg3);
    case LINUX_SYS_SCHED_GETSCHEDULER:
        return linux_sched_getscheduler(arg1);
    case LINUX_SYS_SCHED_GET_PRIORITY_MAX:
        return linux_sched_priority_bound(arg1, 1);
    case LINUX_SYS_SCHED_GET_PRIORITY_MIN:
        return linux_sched_priority_bound(arg1, 0);
    case LINUX_SYS_MLOCK:
    case LINUX_SYS_MUNLOCK:
        return linux_mlock_range(arg1, arg2);
    case LINUX_SYS_ACCESS:
        return linux_result(vex_syscall_dispatch(VEX_SYS_ACCESS, arg1, arg2, 0));
    case LINUX_SYS_FACCESSAT:
    case LINUX_SYS_FACCESSAT2:
        {
            char resolved[4096];
            if(arg3 & ~7ull)return linux_error(LINUX_EINVAL);
            /* Native credentials currently have one effective/real identity.
             * Do not silently accept empty-path or no-follow contracts. */
            if(number==LINUX_SYS_FACCESSAT2 && arg4){
                if(arg4 & ~(0x100u|0x200u|0x1000u))return linux_error(LINUX_EINVAL);
                if(arg4 & ~0x200ull)return linux_error(LINUX_ENOSYS);
            }
            if(!vex_syscall_user_string_valid(arg2,sizeof(resolved)))return linux_error(LINUX_EFAULT);
            if (ladybird_resolve_dirfd_path((int)arg1, (const char *)(uintptr_t)arg2, resolved, sizeof(resolved)) != 0) {
                return linux_result((uint64_t)-1);
            }
            return linux_result((uint64_t)access(resolved, (int)arg3));
        }
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
            /* Every online CPU: glibc derives sysconf(_SC_NPROCESSORS_ONLN) from this, and
             * WebKit sizes its worker pools (GC markers, JIT compiler threads) by it. */
            {
                const uint32_t cpus = smp_cpu_count();
                for (uint32_t cpu = 0u; cpu < cpus && cpu < size * 8u; ++cpu) {
                    mask[cpu / 8u] |= (uint8_t)(1u << (cpu % 8u));
                }
                if (mask[0] == 0u) {
                    mask[0] = 0x1u;
                }
            }
            return (uint64_t)size;
        }
    case LINUX_SYS_SCHED_SETAFFINITY:
        return linux_error(LINUX_ENOSYS);
    case LINUX_SYS_KILL:
        return linux_signal_target((uint32_t)arg1, (int)arg2);
    case LINUX_SYS_TKILL:
        return linux_signal_thread_target((uint32_t)arg1, (int)arg2);
    case LINUX_SYS_TGKILL:
        {
            SCHEDULER_TASK_REF(target_ref);
            process_t *target = scheduler_task_acquire(&target_ref,(uint32_t)arg2,0);
            if (target == NULL || target->group == NULL ||
                target->group->process_id != (uint32_t)arg1) {
                return linux_error(LINUX_EINVAL);
            }
            return linux_signal_thread_target((uint32_t)arg2, (int)arg3);
        }
    case LINUX_SYS_PRCTL:
    case LINUX_SYS_SIGALTSTACK:
    case LINUX_SYS_MEMBARRIER:
    case LINUX_SYS_UMASK:
    case LINUX_SYS_SETPGID:
    case LINUX_SYS_SETSID:
        return linux_error(LINUX_ENOSYS);
    case LINUX_SYS_GETPGRP:
    case LINUX_SYS_GETPGID:
        return scheduler_current_pid();
    case LINUX_SYS_GETRLIMIT:
        return arg1 == LINUX_RLIMIT_STACK
            ? linux_read_stack_limit(arg2) : linux_error(LINUX_ENOSYS);
    case LINUX_SYS_SETRLIMIT:
        return linux_error(LINUX_ENOSYS);
    case LINUX_SYS_SYSINFO:
        {
            linux_sysinfo_t record;
            linux_sysinfo_t *info=&record;
            compat_zero(info, sizeof(*info));
            info->uptime = (int64_t)((scheduler_ticks_now() * 10ull) / 1000ull);
            info->totalram = pmm_total_page_count() * 4096ull;
            info->freeram = pmm_free_page_count() * 4096ull;
            info->procs = (uint16_t)(scheduler_process_count() > 65535u
                ? 65535u : scheduler_process_count());
            info->mem_unit = 1u;
            return vex_copy_to_user(arg1,&record,sizeof(record))?linux_error(LINUX_EFAULT):0u;
        }
    case LINUX_SYS_CLOCK_GETRES:
        {
            struct timespec *ts = (struct timespec *)(uintptr_t)arg2;
            if (ts != NULL) {
                const struct timespec resolution={0,1};
                if(vex_copy_to_user(arg2,&resolution,sizeof(resolution)))return linux_error(LINUX_EFAULT);
            }
            return 0u;
        }
    case LINUX_SYS_GETDENTS64:
        if (!vex_syscall_user_range_valid(arg2, arg3, 1))
            return linux_error(LINUX_EFAULT);
        return linux_result((uint64_t)ladybird_getdents64((int)arg1, (void *)(uintptr_t)arg2, (size_t)arg3));
    case LINUX_SYS_STATX:
        return linux_statx(arg1, arg2, arg3, arg5);
    default: {
        /* A legitimate fallback may retry an unsupported operation on every
         * event-loop iteration. Preserve its error, but do not perform UART
         * I/O on every retry. Detailed investigations can opt into all calls. */
        static struct {uint32_t owner;uint64_t number,count;} unsupported[64];
        static uint32_t next_slot;
        uint32_t owner=scheduler_current_pid(),slot=64;
        for(uint32_t i=0;i<64;++i)if(unsupported[i].count && unsupported[i].owner==owner && unsupported[i].number==number){slot=i;break;}
        if(slot==64){slot=next_slot++%64;unsupported[slot].owner=owner;unsupported[slot].number=number;unsupported[slot].count=0;}
        uint64_t count=++unsupported[slot].count;
        if(!VEX_COMPAT_TRACE && count>1 && (count&(count-1))!=0)return linux_error(LINUX_ENOSYS);
        debug_log_puts("linux: ENOSYS syscall ");
        debug_log_u64_dec(number);
        debug_log_puts(" pid=");
        debug_log_u64_dec(scheduler_current_pid());
        debug_log_puts(" tid=");
        debug_log_u64_dec(scheduler_current_tid());
        debug_log_puts(" a1=");
        debug_log_u64_dec(arg1);
        debug_log_puts(" a2=");
        debug_log_u64_dec(arg2);
        debug_log_puts(" a3=");
        debug_log_u64_dec(arg3);
        {
            const uint64_t call_rip = linux_current_user_rip();
            debug_log_puts(" rip=0x");
            debug_log_u64_hex(call_rip);
        }
        debug_log_puts(" repeat=");debug_log_u64_dec(count);
        debug_log_puts("\n");
        return linux_error(LINUX_ENOSYS);
    }
    }
}

#if VEX_BROWSER_DIAGNOSTIC
static struct {uint32_t tid,pid;uint8_t active;uint64_t number,a1,a2,a3,rip,rsp,result;} browser_syscalls[256];
/* Private diagnostic snapshot, no public syscall or process API. */
void compat_linux_debug_tasks(void) {
    for(unsigned i=0;i<256;i++){
        if(!browser_syscalls[i].tid)continue;
        SCHEDULER_TASK_REF(task_ref);
        process_t *t=scheduler_task_acquire(&task_ref,browser_syscalls[i].tid,0);
        if(!t || t->state==PROC_ZOMBIE)continue;
        debug_log_puts("browser task pid=");debug_log_u64_dec(browser_syscalls[i].pid);
        debug_log_puts(" tid=");debug_log_u64_dec(t->pid);debug_log_puts(" state=");debug_log_u64_dec(t->state);
        debug_log_puts(" active=");debug_log_u64_dec(browser_syscalls[i].active);
        debug_log_puts(" syscall=");debug_log_u64_dec(browser_syscalls[i].number);
        debug_log_puts(" a1=");debug_log_u64_hex(browser_syscalls[i].a1);
        debug_log_puts(" a2=");debug_log_u64_hex(browser_syscalls[i].a2);
        debug_log_puts(" a3=");debug_log_u64_hex(browser_syscalls[i].a3);
        debug_log_puts(" rip=");debug_log_u64_hex(browser_syscalls[i].rip);
        debug_log_puts(" rsp=");debug_log_u64_hex(browser_syscalls[i].rsp);
        debug_log_puts(" result=");debug_log_u64_hex(browser_syscalls[i].result);debug_log_puts("\n");
        if(browser_syscalls[i].number==7 && browser_syscalls[i].a2<=16 && hhdm_request.response){
            void *pt=vmm_cr3_to_address_space(t->cr3);
            for(uint64_t n=0;n<browser_syscalls[i].a2;n++){
                uint64_t addr=browser_syscalls[i].a1+8*n,word=0;int valid=addr<VMM_USER_ADDRESS_LIMIT-8;
                for(unsigned b=0;valid && b<8;b++){
                    uint64_t flags=vmm_get_page_flags(pt,addr+b),phys=vmm_get_physical(pt,addr+b);
                    if(!phys || !(flags&VMM_PAGE_USER)){valid=0;break;}
                    word|=(uint64_t)*(const uint8_t *)(hhdm_request.response->offset+phys)<<(8*b);
                }
                if(valid){debug_log_puts("browser poll slot tid=");debug_log_u64_dec(t->pid);debug_log_puts(" index=");debug_log_u64_dec(n);debug_log_puts(" word=");debug_log_u64_hex(word);debug_log_puts("\n");}
            }
        }
    }
}
#endif

uint64_t compat_linux_syscall(uint64_t number,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5,uint64_t a6){
    /* Raw Linux pointers originate in ring 3. Internal native adapters may
       subsequently use owned kernel records/buffers, but that privilege must
       never be inherited by an untouched raw argument in the kernel half.
       This is ABI-defined argument validation, not ABI selection by pointer. */
    if(scheduler_current_user_mode()) {
        unsigned pointers=0;
        switch(number) {
        case 0:case 1:case 5:case 17:case 18:case 19:case 20:case 42:case 43:
        case 49:case 51:case 52:case 138:case 158:case 228:case 229:case 97:
            pointers=2u;break;
        case 2:case 21:case 63:case 79:case 80:case 83:case 84:case 85:case 87:
        case 90:case 92:case 99:case 202:case 273:case 334:
            pointers=1u;break;
        case 4:case 6:case 35:case 96:case 82:case 86:case 88:case 89:case 137:
            pointers=3u;break;
        case 7:pointers=1u;break;
        case 61:pointers=10u;break;
        case 13:case 14:pointers=6u;break;
        case 46:case 47:case 307:pointers=2u;break;
        case 53:pointers=8u;break;
        case 44:case 45:pointers=18u|(number==45?32u:0u);break;
        case 56:pointers=30u;break;
        case 59:pointers=7u;break;
        case 60:case 231:break;
        case 201:case 218:case 318:pointers=1u;break;
        case 257:case 269:pointers=2u;break;
        case 262:case 288:pointers=6u;break;
        case 271:pointers=13u;break;
        case 302:pointers=12u;break;
        case 332:pointers=18u;break;
        case 16: /* The implemented ioctls use a pointer or no argument. */
            if(a2==0x541bu || a2==0x5401u || a2==0x5402u || a2==0x5403u ||
               a2==0x5404u || a2==0x5413u || a2==0x5414u)pointers=4u;
            break;
        case 72:if(a2==5u || a2==6u || a2==7u || (a2>=36u && a2<=38u))pointers=4u;break;
        default:break;
        }
        const uint64_t args[]={a1,a2,a3,a4,a5,a6};
        for(unsigned i=0;i<6u;++i)
            if((pointers&(1u<<i)) && args[i]>=VMM_USER_ADDRESS_LIMIT)return linux_error(LINUX_EFAULT);
    }
#if VEX_BROWSER_DIAGNOSTIC
    unsigned task_slot=256;uint32_t tid=scheduler_current_tid();
    if(scheduler_current_has_capability(VEX_CAP_NET_CONNECT)){
        for(unsigned i=0;i<256;i++)if(browser_syscalls[i].tid==tid){task_slot=i;break;}
        if(task_slot==256)for(unsigned i=0;i<256;i++)if(!browser_syscalls[i].tid){task_slot=i;break;}
        if(task_slot<256){browser_syscalls[task_slot].tid=tid;browser_syscalls[task_slot].pid=scheduler_current_process_id();
            browser_syscalls[task_slot].number=number;browser_syscalls[task_slot].a1=a1;browser_syscalls[task_slot].a2=a2;browser_syscalls[task_slot].a3=a3;
            browser_syscalls[task_slot].rip=linux_current_user_rip();browser_syscalls[task_slot].rsp=linux_current_user_rsp();browser_syscalls[task_slot].active=1;}
    }
#endif
    uint64_t result=compat_linux_syscall_impl(number,a1,a2,a3,a4,a5,a6);
#if VEX_BROWSER_DIAGNOSTIC
    if(task_slot<256){browser_syscalls[task_slot].active=0;browser_syscalls[task_slot].result=result;}
#endif
#if VEX_BROWSER_DIAGNOSTIC
    static unsigned abi_failures;
    if((result==(uint64_t)-LINUX_EFAULT || (number==LINUX_SYS_FTRUNCATE && a2>=0x08000000)) && abi_failures++<80){
        debug_log_puts("browser ABI failure pid=");debug_log_u64_dec(scheduler_current_pid());
        debug_log_puts(" syscall=");debug_log_u64_dec(number);
        debug_log_puts(" a1=");debug_log_u64_hex(a1);debug_log_puts(" a2=");debug_log_u64_hex(a2);
        debug_log_puts(" a3=");debug_log_u64_hex(a3);debug_log_puts(" result=");debug_log_u64_hex(result);debug_log_puts("\n");
    }
    static unsigned network_traces;
    if(number>=42 && number<=55 && vex_socket_network_fd((int)a1) && network_traces++<96){
        debug_log_puts("net ABI pid=");debug_log_u64_hex(scheduler_current_pid());debug_log_puts(" syscall=");debug_log_u64_hex(number);
        debug_log_puts(" fd=");debug_log_u64_hex(a1);debug_log_puts(" arg2=");debug_log_u64_hex(a2);debug_log_puts(" arg3=");debug_log_u64_hex(a3);
        debug_log_puts(" result=");debug_log_u64_hex(result);debug_log_puts("\n");
    }
#endif
    return result;
}
