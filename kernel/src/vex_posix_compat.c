#include <unos/vex_legacy_compat.h>
#include <unos/compat.h>
#include <unos/smp.h>
#include <unos/kernel_guard.h>
#include <unos/fd_borrow.h>
#include <unos/usercopy.h>
#include <unos/rendezvous.h>
#include <unos/gui.h>

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unos/disk.h>
#include <unos/dhcp.h>
#include <unos/rtc.h>
#include <unos/debug_log.h>
#include <unos/event_counter.h>
#include <unos/elf.h>
#include <unos/fd_usage.h>
#include <unos/fs.h>
#include <unos/heap.h>
#include <unos/ipc.h>
#include <unos/irq.h>
#include <unos/iso9660.h>
#include <unos/ethernet.h>
#include <unos/process_memory.h>
#include <unos/keyboard.h>
#include <unos/random.h>
#include <unos/scheduler.h>
#include <unos/security.h>
#include <unos/tty.h>
#include <unos/users.h>
#include <unos/vex_syscall.h>
#include <unos/vex_file_abi.h>
#include <unos/file_times.h>
#include <unos/fs_root.h>
#include <unos/fs_root_provider.h>
#include <unos/process_resource.h>

#define LADYBIRD_MAX_FDS 128
#define LADYBIRD_MAX_PATH 512
#define LADYBIRD_MAX_MAPPINGS 64
#define LADYBIRD_MAX_ENV 32
#define LADYBIRD_PROC_BUFFER_SIZE 262144u
#define LADYBIRD_ENV_NAME_MAX 48
#define LADYBIRD_ENV_VALUE_MAX 160
#define LADYBIRD_TICK_MS 10ull
#define LADYBIRD_DIR_MARKER ".ladybird_dir"
#define LADYBIRD_AT_FDCWD (-100)
#define LADYBIRD_FCNTL_LOCK_MAX 128u
#define LADYBIRD_FCNTL_WAITER_MAX 64u

typedef struct ladybird_fd_slot {
    uint32_t owner_pid;
    int fd;
    uint8_t close_on_exec;
    uint8_t reserved[3];
    ladybird_fd_object_t *object;
} ladybird_fd_slot_t;

typedef struct ladybird_console_ctx {
    int fd;
    int readable;
    int writable;
    char pending_char;
} ladybird_console_ctx_t;

typedef struct ladybird_file_ctx {
    struct ladybird_file_ctx *live_next;
    kernel_gate_t data_gate;
    ladybird_fd_object_t *object; /* weak: FD object owns this context */
    char path[LADYBIRD_MAX_PATH];
    uint8_t *buffer;
    size_t size;
    size_t capacity;
    size_t offset;
    int flags;
    uint8_t dirty;
    uint8_t write_allowed;
    ora_view_t content; /* immutable file bytes retained through FD/map ownership */
    uint8_t buffer_owned;
    uint8_t volatile_shm;
    uint8_t unlinked;
    uint32_t granted_uid;
    uint32_t granted_caps;
    fs_root_t *root;
    uint32_t root_rights;
    uint64_t identity;
} ladybird_file_ctx_t;

typedef struct ladybird_pipe_ctx {
    int pipe_id;
    uint8_t readable;
    uint8_t writable;
    int flags;
} ladybird_pipe_ctx_t;

typedef struct ladybird_mmap_entry {
    void *addr;
    size_t length;
    int prot;
    int flags;
    int used;
} ladybird_mmap_entry_t;

/* Lazy streaming context for large iso9660 files — no data pre-loaded. */
typedef struct ladybird_iso_stream_ctx {
    uint32_t lba;
    uint32_t file_size;
    size_t   offset;
    uint32_t root_rights;
    vex_file_times_t times;
    fs_root_t *root;
} ladybird_iso_stream_ctx_t;

/* Directory fd — stores the directory path so openat() can resolve relative names. */
typedef struct ladybird_dir_child {
    struct ladybird_dir_child *next;
    uint64_t inode;
    uint8_t type;
    char name[LADYBIRD_MAX_PATH];
} ladybird_dir_child_t;

typedef struct ladybird_dir_ctx {
    kernel_gate_t data_gate;
    char path[LADYBIRD_MAX_PATH];
    ladybird_dir_child_t *children, *tail, *cursor;
    uint64_t position;
    uint8_t enumerated;
    fs_root_t *root;
    uint32_t root_rights,root_uid,root_caps;
} ladybird_dir_ctx_t;

#define LINUX_O_DIRECTORY 0x10000
#define LADYBIRD_O_EXCL 0x0080

typedef struct ladybird_env_entry {
    uint8_t used;
    char name[LADYBIRD_ENV_NAME_MAX];
    char value[LADYBIRD_ENV_VALUE_MAX];
} ladybird_env_entry_t;

static int bootstrap_kernel_errno; /* BSP only, before any AP is released. */
static uint64_t file_close_failures;
uint64_t vex_fd_close_failures(void){return __atomic_load_n(&file_close_failures,__ATOMIC_ACQUIRE);}
#define ladybird_errno_value (*ladybird_errno_location())
static uint8_t ladybird_posix_ready;
static void debug_log_size(const char *prefix, size_t n);
static uint8_t bootstrap_stdio_ensuring;
static kernel_gate_t posix_init_gate=KERNEL_GATE_INIT;
#include "vex_fd_table.inc"
uint32_t vex_fd_live_count(void){
    uint64_t lock=fd_inventory_lock();uint32_t count=0;
    for(ladybird_process_fd_table_t *t=ladybird_fd_tables;t;t=t->next)
        for(unsigned i=0;i<LADYBIRD_MAX_FDS;i++)if(t->slots[i].object && count!=UINT32_MAX)++count;
    fd_inventory_unlock(lock);return count;
}
/* Includes object references held by mappings and messages, not only FD slots.
 * Protected by the existing FD inventory lock; names are immutable. */
static ladybird_file_ctx_t *live_file_contexts;

typedef struct ladybird_fcntl_lock {
    uint8_t used;
    uint8_t type;
    uint8_t end_infinite;
    uint8_t mandatory;
    uint32_t pid;
    uint64_t start;
    uint64_t end;
    ladybird_file_ctx_t *description;
    uint64_t identity;
    char path[LADYBIRD_MAX_PATH];
} ladybird_fcntl_lock_t;

typedef struct ladybird_fcntl_waiter {
    uint8_t used;
    volatile uint8_t woken;
    uint8_t mandatory,reserved;
    uint32_t pid;
    uint32_t tid;
    char path[LADYBIRD_MAX_PATH];
} ladybird_fcntl_waiter_t;

static ladybird_fcntl_lock_t ladybird_fcntl_locks[LADYBIRD_FCNTL_LOCK_MAX];
static ladybird_fcntl_lock_t ladybird_fcntl_lock_backup[LADYBIRD_FCNTL_LOCK_MAX];
static ladybird_fcntl_waiter_t ladybird_fcntl_waiters[LADYBIRD_FCNTL_WAITER_MAX];
static volatile uint32_t ladybird_fcntl_lock_word;
static uint32_t mandatory_lock_count;

static char ladybird_cwd[LADYBIRD_MAX_PATH];
static ladybird_mmap_entry_t ladybird_mappings[LADYBIRD_MAX_MAPPINGS];
static ladybird_env_entry_t ladybird_env[LADYBIRD_MAX_ENV];
#ifdef VEX_COMPAT_FILE_TRACE
static uint32_t wk2_shared_trace_count;
static uint32_t hsts_file_trace_count;
#endif

static int wk2_shared_path(const char *path) {
    static const char prefix[] = "/dev/shm/WK2SharedMemory.";
    return path != NULL && strncmp(path, prefix, sizeof(prefix) - 1u) == 0;
}

#ifdef VEX_COMPAT_FILE_TRACE
static void wk2_shared_trace(const char *operation, const char *path,
                             uint64_t a, uint64_t b, uint64_t result) {
    if (!wk2_shared_path(path) || wk2_shared_trace_count >= 160u) return;
    ++wk2_shared_trace_count;
    debug_log_puts("wk2-shm: ");
    debug_log_puts(operation);
    debug_log_puts(" pid=");
    debug_log_size("", scheduler_current_pid());
    debug_log_puts(" tid=");
    debug_log_size("", scheduler_current_tid());
    debug_log_puts(" path=");
    debug_log_puts(path);
    debug_log_puts(" a=");
    debug_log_size("", (size_t)a);
    debug_log_puts(" b=");
    debug_log_size("", (size_t)b);
    debug_log_puts(" result=");
    debug_log_size("", (size_t)result);
    debug_log_puts("\n");
}

/* Keep the slow WebKit HSTS SQLite path observable without tracing ordinary
 * browser cache traffic. This temporary bounded trace records the file ABI
 * operations that SQLite maps to VSL and is removed after the failure is
 * localized. */
static int hsts_database_path(const char *path) {
    if (path == NULL) return 0;
    for (size_t i = 0u; path[i] != '\0' && i + 4u <= LADYBIRD_MAX_PATH; ++i) {
        if (path[i] == 'h' && path[i + 1u] == 's' &&
            path[i + 2u] == 't' && path[i + 3u] == 's') return 1;
    }
    return 0;
}

static void hsts_file_trace(const char *operation, const char *path,
                            uint64_t a, uint64_t b, int64_t result) {
    /* Lock calls are extremely frequent and were consuming the bounded trace
     * before SQLite's actual writes/syncs could be observed. Their successful
     * results are already established; keep this trace focused on I/O. */
    if (operation != NULL && strncmp(operation, "fcntl.", 6u) == 0) return;
    if (!hsts_database_path(path) || hsts_file_trace_count >= 384u) return;
    ++hsts_file_trace_count;
    debug_log_puts("hsts-file: op=");
    debug_log_puts(operation);
    debug_log_puts(" pid=");
    debug_log_size("", scheduler_current_pid());
    debug_log_puts(" tid=");
    debug_log_size("", scheduler_current_tid());
    debug_log_puts(" path=");
    debug_log_puts(path);
    debug_log_puts(" a=");
    debug_log_size("", (size_t)a);
    debug_log_puts(" b=");
    debug_log_size("", (size_t)b);
    debug_log_puts(" result=");
    if (result < 0) {
        debug_log_puts("-");
        debug_log_size("", (size_t)(-(result + 1)) + 1u);
    } else {
        debug_log_size("", (size_t)result);
    }
    debug_log_puts("\n");
}
#else
/* Keep short-lived WebKit bring-up tracing out of normal file hot paths. */
#define wk2_shared_trace(...) ((void)0)
#define hsts_file_trace(...) ((void)0)
#endif

static uint32_t ladybird_fd_owner_pid(void);
static int ladybird_fd_is_open(uint32_t owner_pid, int fd);
static void ladybird_ensure_standard_fds(void);
static void ladybird_fcntl_drop_path(uint32_t pid, const char *path);
static void ladybird_fcntl_drop_process(uint32_t pid);
static void description_range_closed(uint32_t,ladybird_fd_object_t *,int);
static int description_range_access(ladybird_file_ctx_t *,uint64_t,size_t,int);

static ssize_t console_read(void *ctx, void *buffer, size_t count);
static ssize_t console_write(void *ctx, const void *buffer, size_t count);
static int console_close(void *ctx);
static int console_fstat(void *ctx, struct stat *st);
static int console_seek(void *ctx, off_t offset, int whence, off_t *result);
static int console_fsync(void *ctx);
static int console_poll(void *ctx, short events, short *revents);

static ssize_t file_read_op(void *ctx, void *buffer, size_t count);
static ssize_t file_write_op(void *ctx, const void *buffer, size_t count);
static ssize_t file_pread_op(void *ctx, void *buffer, size_t count, off_t offset);
static ssize_t file_pwrite_op(void *ctx, const void *buffer, size_t count, off_t offset);
static int file_close_op(void *ctx);
static int file_fstat_op(void *ctx, struct stat *st);
static int file_seek_op(void *ctx, off_t offset, int whence, off_t *result);
static int file_fsync_op(void *ctx);
static int file_poll_op(void *ctx, short events, short *revents);

static ssize_t pipe_read_op(void *ctx, void *buffer, size_t count);
static ssize_t pipe_write_op(void *ctx, const void *buffer, size_t count);
static int pipe_close_op(void *ctx);
static int pipe_fstat_op(void *ctx, struct stat *st);
static int pipe_seek_op(void *ctx, off_t offset, int whence, off_t *result);
static int pipe_fsync_op(void *ctx);
static int pipe_poll_op(void *ctx, short events, short *revents);

/* iso9660 streaming ops — reads sectors on demand without pre-loading. */
static ssize_t iso_stream_read_op(void *ctx, void *buffer, size_t count);
static ssize_t iso_stream_pread_op(void *ctx, void *buffer, size_t count, off_t offset);
static ssize_t iso_stream_write_op(void *ctx, const void *buffer, size_t count);
static int iso_stream_close_op(void *ctx);
static int iso_stream_fstat_op(void *ctx, struct stat *st);
static int iso_stream_seek_op(void *ctx, off_t offset, int whence, off_t *result);
static int iso_stream_fsync_op(void *ctx);
static int iso_stream_poll_op(void *ctx, short events, short *revents);

/* directory fd ops */
static ssize_t dir_read_op(void *ctx, void *b, size_t n) { (void)ctx;(void)b;(void)n; ladybird_errno_value=EISDIR; return -1; }
static ssize_t dir_write_op(void *ctx, const void *b, size_t n) { (void)ctx;(void)b;(void)n; ladybird_errno_value=EISDIR; return -1; }
static void dir_free_children(ladybird_dir_ctx_t *ctx) {
    while (ctx->children) {
        ladybird_dir_child_t *next = ctx->children->next;
        kfree(ctx->children);
        ctx->children = next;
    }
    ctx->tail = ctx->cursor = NULL;
}
static int dir_close_op(void *ctx) {
    dir_free_children(ctx);
    fs_root_release(((ladybird_dir_ctx_t *)ctx)->root);
    kfree(ctx);
    return 0;
}
static int dir_fstat_op(void *ctx, struct stat *st) {
    (void)ctx;
    if (st == NULL) { ladybird_errno_value = EINVAL; return -1; }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0755u;
    return 0;
}
static int dir_seek_op(void *ctx, off_t o, int w, off_t *r) { (void)ctx;(void)o;(void)w;(void)r; ladybird_errno_value=EISDIR; return -1; }
static int dir_fsync_op(void *ctx) { (void)ctx; return 0; }
static int dir_poll_op(void *ctx, short ev, short *rev) { (void)ctx;(void)ev; if(rev)*rev=0; return 0; }

static const ladybird_fd_ops_t dir_ops = {
    dir_read_op, dir_write_op, dir_close_op,
    dir_fstat_op, dir_seek_op, dir_fsync_op, dir_poll_op, NULL, NULL
};

static const ladybird_fd_ops_t iso_stream_ops = {
    iso_stream_read_op,
    iso_stream_write_op,
    iso_stream_close_op,
    iso_stream_fstat_op,
    iso_stream_seek_op,
    iso_stream_fsync_op,
    iso_stream_poll_op,
    iso_stream_pread_op,
    NULL
};

static const ladybird_fd_ops_t console_ops = {
    console_read,
    console_write,
    console_close,
    console_fstat,
    console_seek,
    console_fsync,
    console_poll,
    NULL,
    NULL
};

static const ladybird_fd_ops_t file_ops = {
    file_read_op,
    file_write_op,
    file_close_op,
    file_fstat_op,
    file_seek_op,
    file_fsync_op,
    file_poll_op,
    file_pread_op,
    file_pwrite_op
};

void vex_web_fd_describe(const vex_web_fd_object_t *object, char *out, size_t capacity) {
    if (capacity == 0u) {
        return;
    }
    out[0] = '\0';
    if (object == NULL || object->ctx == NULL) {
        return;
    }
    if (object->ops == (const vex_web_fd_ops_t *)&file_ops) {
        const ladybird_file_ctx_t *file = object->ctx;
        size_t i = 0u;

        for (; i + 1u < capacity && file->path[i] != '\0'; ++i) {
            out[i] = file->path[i];
        }
        out[i] = '\0';
    } else if (object->ops == (const vex_web_fd_ops_t *)&iso_stream_ops) {
        const ladybird_iso_stream_ctx_t *stream = object->ctx;
        const unsigned lba = stream->lba;
        static const char digits[] = "0123456789";
        char number[12];
        size_t used = 0u, i = 0u;
        unsigned value = lba;

        do { number[used++] = digits[value % 10u]; value /= 10u; } while (value != 0u && used < sizeof(number));
        for (const char *prefix = "iso:"; *prefix != '\0' && i + 1u < capacity; ++prefix) {
            out[i++] = *prefix;
        }
        while (used != 0u && i + 1u < capacity) {
            out[i++] = number[--used];
        }
        out[i] = '\0';
    }
}

static const ladybird_fd_ops_t pipe_ops = {
    pipe_read_op,
    pipe_write_op,
    pipe_close_op,
    pipe_fstat_op,
    pipe_seek_op,
    pipe_fsync_op,
    pipe_poll_op,
    NULL,
    NULL
};

/* Forward declarations for helpers defined later in this file. */
static int set_errno(int value);
static ssize_t set_errno_ssize(int value);
static int string_equals(const char *left, const char *right);
static int path_is_directory(const char *path);
int vex_web_resolve_dirfd_path(int dirfd, const char *path, char *buffer, size_t size);
ssize_t vex_web_getdents64(int fd, void *buffer, size_t count);

/* Character-device fds: /dev/urandom, /dev/random, /dev/zero, /dev/null.
 * WebKit/JSC initialise their RNG from /dev/urandom and abort (writing the
 * 0xbbadbeef poison value) if it can't be read, so these must behave like
 * their Linux counterparts. */
#define LADYBIRD_DEV_NULL    0
#define LADYBIRD_DEV_ZERO    1
#define LADYBIRD_DEV_RANDOM  2

typedef struct ladybird_dev_ctx {
    int kind;
} ladybird_dev_ctx_t;

typedef struct ladybird_event_counter_ctx {
    uint64_t capability;
} ladybird_event_counter_ctx_t;

typedef struct ladybird_timerfd_waiter {
    uint32_t tid;
    volatile uint8_t woken;
    struct ladybird_timerfd_waiter *next;
} ladybird_timerfd_waiter_t;

typedef struct ladybird_timerfd_ctx {
    uint64_t deadline_tick;
    uint64_t interval_ticks;
    uint64_t expirations;
    uint32_t clock_id;
    uint8_t armed;
    uint8_t nonblock;
    ladybird_timerfd_waiter_t *waiters;
} ladybird_timerfd_ctx_t;

/* Linux compatibility files under /proc are generated views over VexOS
 * state.  They are ordinary refcounted FD objects, never filesystem entries. */
typedef struct ladybird_proc_ctx {
    char *data;
    size_t size;
    size_t offset;
} ladybird_proc_ctx_t;

static ssize_t dev_read_op(void *ctx, void *buffer, size_t count) {
    ladybird_dev_ctx_t *dev = ctx;
    if (dev == NULL || (buffer == NULL && count != 0u)) {
        return set_errno_ssize(EINVAL);
    }
    switch (dev->kind) {
    case LADYBIRD_DEV_NULL:
        return 0; /* EOF */
    case LADYBIRD_DEV_ZERO:
        memset(buffer, 0, count);
        return (ssize_t)count;
    case LADYBIRD_DEV_RANDOM:
        if (kernel_random_try_fill(buffer, count) != 0) return set_errno_ssize(EAGAIN);
        return (ssize_t)count;
    default:
        return set_errno_ssize(EBADF);
    }
}

static ssize_t dev_write_op(void *ctx, const void *buffer, size_t count) {
    (void)ctx; (void)buffer;
    return (ssize_t)count; /* discard writes (e.g. seeding /dev/random) */
}

static int dev_close_op(void *ctx) { kfree(ctx); return 0; }

static int dev_fstat_op(void *ctx, struct stat *st) {
    (void)ctx;
    if (st == NULL) { return set_errno(EINVAL); }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0666u;
    return 0;
}

static int dev_seek_op(void *ctx, off_t offset, int whence, off_t *result) {
    (void)ctx; (void)offset; (void)whence;
    if (result != NULL) { *result = 0; }
    return 0; /* character devices ignore seeks */
}

static int dev_fsync_op(void *ctx) { (void)ctx; return 0; }

static int dev_poll_op(void *ctx, short events, short *revents) {
    (void)ctx;
    if (revents != NULL) { *revents = events & (short)(POLLIN | POLLOUT); }
    return 1;
}

static const ladybird_fd_ops_t dev_ops = {
    dev_read_op,
    dev_write_op,
    dev_close_op,
    dev_fstat_op,
    dev_seek_op,
    dev_fsync_op,
    dev_poll_op,
    NULL,
    NULL
};

static ssize_t event_counter_read_op(void *ctx, void *buffer, size_t count) {
    ladybird_event_counter_ctx_t *event = ctx;
    uint64_t value;
    int result;
    if (event == NULL || buffer == NULL || count != sizeof(value)) {
        return set_errno_ssize(count == sizeof(value) ? EBADF : EINVAL);
    }
    result = vex_event_counter_read(event->capability, &value, 0u);
    if (result == -2) return set_errno_ssize(EAGAIN);
    if (result == -3) return set_errno_ssize(EINTR);
    if (result != 0) return set_errno_ssize(EBADF);
    memcpy(buffer, &value, sizeof(value));
    return (ssize_t)sizeof(value);
}

static ssize_t event_counter_write_op(void *ctx, const void *buffer, size_t count) {
    ladybird_event_counter_ctx_t *event = ctx;
    uint64_t value;
    if (event == NULL || buffer == NULL || count != sizeof(value)) {
        return set_errno_ssize(count == sizeof(value) ? EBADF : EINVAL);
    }
    memcpy(&value, buffer, sizeof(value));
    if (vex_event_counter_signal(event->capability, value) != 0) {
        return set_errno_ssize(EINVAL);
    }
    return (ssize_t)sizeof(value);
}

static int event_counter_close_op(void *ctx) {
    ladybird_event_counter_ctx_t *event = ctx;
    if (event != NULL) {
        (void)vex_event_counter_release(event->capability);
        kfree(event);
    }
    return 0;
}

static int event_counter_fstat_op(void *ctx, struct stat *st) {
    (void)ctx;
    if (st == NULL) return set_errno(EINVAL);
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0600u;
    return 0;
}

static int event_counter_seek_op(void *ctx, off_t offset, int whence, off_t *result) {
    (void)ctx; (void)offset; (void)whence; (void)result;
    return set_errno(ESPIPE);
}

static int event_counter_fsync_op(void *ctx) { (void)ctx; return 0; }

static int event_counter_poll_op(void *ctx, short events, short *revents) {
    ladybird_event_counter_ctx_t *event = ctx;
    if (event == NULL || revents == NULL) return set_errno(EINVAL);
    if (vex_event_counter_poll(event->capability, events, revents) != 0) {
        return set_errno(EBADF);
    }
    return 0;
}

static const ladybird_fd_ops_t event_counter_ops = {
    event_counter_read_op,
    event_counter_write_op,
    event_counter_close_op,
    event_counter_fstat_op,
    event_counter_seek_op,
    event_counter_fsync_op,
    event_counter_poll_op,
    NULL,
    NULL
};

static ssize_t proc_read_op(void *ctx, void *buffer, size_t count);
static ssize_t proc_pread_op(void *ctx, void *buffer, size_t count, off_t offset);
static ssize_t proc_write_op(void *ctx, const void *buffer, size_t count);
static int proc_close_op(void *ctx);
static int proc_fstat_op(void *ctx, struct stat *st);
static int proc_seek_op(void *ctx, off_t offset, int whence, off_t *result);
static int proc_fsync_op(void *ctx);
static int proc_poll_op(void *ctx, short events, short *revents);

static const ladybird_fd_ops_t proc_ops = {
    proc_read_op,
    proc_write_op,
    proc_close_op,
    proc_fstat_op,
    proc_seek_op,
    proc_fsync_op,
    proc_poll_op,
    proc_pread_op,
    NULL
};

/* Returns a device kind for known /dev paths, or -1 if not a special device. */
static int ladybird_device_kind(const char *path) {
    if (string_equals(path, "/dev/null")) return LADYBIRD_DEV_NULL;
    if (string_equals(path, "/dev/zero")) return LADYBIRD_DEV_ZERO;
    if (string_equals(path, "/dev/urandom")) return LADYBIRD_DEV_RANDOM;
    if (string_equals(path, "/dev/random")) return LADYBIRD_DEV_RANDOM;
    return -1;
}

static int proc_virtual_kind(const char *path) {
    if (path == NULL) return 0;
    if (string_equals(path, "/proc") || string_equals(path, "/proc/self") ||
        string_equals(path, "/proc/sys") || string_equals(path, "/proc/sys/vm")) {
        return 2; /* directory */
    }
    if (string_equals(path, "/etc/resolv.conf") || string_equals(path, "/proc/filesystems") ||
        string_equals(path, "/proc/mounts") ||
        string_equals(path, "/proc/sys/vm/mmap_min_addr") ||
        string_equals(path, "/proc/self/status") ||
        string_equals(path, "/proc/self/stat") ||
        string_equals(path, "/proc/self/maps")) {
        return 1; /* generated regular file */
    }
    return 0;
}

static size_t proc_append(char *buffer, size_t capacity, size_t offset,
                          const char *format, ...) {
    int written;
    va_list ap;

    if (offset >= capacity || format == NULL) return offset;
    va_start(ap, format);
    written = vsnprintf(buffer + offset, capacity - offset, format, ap);
    va_end(ap);
    if (written < 0) return offset;
    if ((size_t)written >= capacity - offset) return capacity - 1u;
    return offset + (size_t)written;
}

static int proc_generate(const char *path, char **data_out, size_t *size_out) {
    char *data;
    size_t used = 0u;
    /* Virtual proc self entries describe the owning process.  A VexOS thread has its
     * own scheduler identity, but all threads in a process share the same
     * process_memory namespace.  Use the process identity here so maps and
     * the other virtual proc views cannot accidentally inspect a sibling
     * thread's namespace. */
    const uint32_t pid = scheduler_current_process_id();

    if (path == NULL || data_out == NULL || size_out == NULL || proc_virtual_kind(path) != 1) {
        return -1;
    }
    data = kmalloc(LADYBIRD_PROC_BUFFER_SIZE);
    if (data == NULL) return -1;
    memset(data, 0, LADYBIRD_PROC_BUFFER_SIZE);

    if(string_equals(path,"/etc/resolv.conf")) {
        uint32_t ip=dhcp_dns_server();if(!ip){kfree(data);return -1;}
        used=proc_append(data,LADYBIRD_PROC_BUFFER_SIZE,used,"nameserver %u.%u.%u.%u\n",ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255);
    } else if (string_equals(path, "/proc/filesystems")) {
        used = proc_append(data, LADYBIRD_PROC_BUFFER_SIZE, used,
                           "vexfs\\nnodev\\tvexproc\\nnodev\\tvexdev\\n");
    } else if (string_equals(path, "/proc/mounts")) {
        const char *backend = disk_type();
        used = proc_append(data, LADYBIRD_PROC_BUFFER_SIZE, used,
                           "vexfs / vexfs rw,relatime,backend=%s 0 0\\n",
                           backend != NULL ? backend : "none");
    } else if (string_equals(path, "/proc/sys/vm/mmap_min_addr")) {
        /* VexOS permanently reserves page zero; its first user mapping may
         * start at the next page, while normal allocation starts much higher. */
        used = proc_append(data, LADYBIRD_PROC_BUFFER_SIZE, used, "4096\\n");
    } else if (string_equals(path, "/proc/self/status")) {
        uint32_t ppid = scheduler_current_ppid();
        uint32_t uid = scheduler_current_uid();
        used = proc_append(data, LADYBIRD_PROC_BUFFER_SIZE, used,
            "Name:\\tvex\\nState:\\tR (running)\\nPid:\\t%u\\nPPid:\\t%u\\nUid:\\t%u\\nGid:\\t%u\\n",
            pid, ppid, uid, uid);
    } else if (string_equals(path, "/proc/self/stat")) {
        uint32_t ppid = scheduler_current_ppid();
        used = proc_append(data, LADYBIRD_PROC_BUFFER_SIZE, used,
            "%u (vex) R %u 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\\n",
            pid, ppid);
    } else if (string_equals(path, "/proc/self/maps")) {
        size_t maps_capacity = LADYBIRD_PROC_BUFFER_SIZE;
        int maps_result = -1;
        unsigned int attempt;

        /* Large processes can legitimately have more regions than fit in the
         * initial buffer.  Retry the same authoritative formatter rather than
         * exposing a silently truncated prefix. */
        for (attempt = 0u; attempt < 5u; ++attempt) {
            maps_result = process_memory_format_maps(pid, data, maps_capacity, &used);
            if (maps_result != -2) {
                break;
            }
            if (maps_capacity > (4u * 1024u * 1024u) / 2u) {
                break;
            }
            {
                const size_t next_capacity = maps_capacity * 2u;
                char *next_data = kmalloc(next_capacity);
                if (next_data == NULL) {
                    break;
                }
                kfree(data);
                data = next_data;
                maps_capacity = next_capacity;
            }
        }
        if (maps_result != 0) {
            kfree(data);
            return -1;
        }
    }

    *data_out = data;
    *size_out = used;
    return 0;
}

static int proc_open_virtual(const char *path) {
    ladybird_proc_ctx_t *ctx;
    ladybird_fd_object_t *object;

    ctx = kmalloc(sizeof(*ctx));
    if (ctx == NULL) return set_errno(ENOMEM);
    memset(ctx, 0, sizeof(*ctx));
    if (proc_generate(path, &ctx->data, &ctx->size) != 0) {
        kfree(ctx);
        return set_errno(ENOENT);
    }
    object = ladybird_fd_object_create(ctx, &proc_ops);
    if (object == NULL) {
        kfree(ctx->data);
        kfree(ctx);
        return set_errno(ENOMEM);
    }
    return ladybird_fd_install(object, -1);
}

static ssize_t proc_read_op(void *ctx, void *buffer, size_t count) {
    ladybird_proc_ctx_t *proc = ctx;
    size_t available;
    if (proc == NULL || (buffer == NULL && count != 0u)) return set_errno_ssize(EINVAL);
    if (proc->offset >= proc->size) return 0;
    available = proc->size - proc->offset;
    if (count > available) count = available;
    memcpy(buffer, proc->data + proc->offset, count);
    proc->offset += count;
    return (ssize_t)count;
}

static ssize_t proc_pread_op(void *ctx, void *buffer, size_t count,
                             off_t offset) {
    ladybird_proc_ctx_t *proc = ctx;
    size_t position;
    size_t available;
    if (proc == NULL || (buffer == NULL && count != 0u))
        return set_errno_ssize(EINVAL);
    if (offset < 0) return set_errno_ssize(EINVAL);
    if (count == 0u || (uint64_t)offset >= (uint64_t)proc->size) return 0;
    position = (size_t)offset;
    available = proc->size - position;
    if (count > available) count = available;
    memcpy(buffer, proc->data + position, count);
    return (ssize_t)count;
}

static ssize_t proc_write_op(void *ctx, const void *buffer, size_t count) {
    (void)ctx; (void)buffer; (void)count;
    return set_errno_ssize(EROFS);
}

static int proc_close_op(void *ctx) {
    ladybird_proc_ctx_t *proc = ctx;
    if (proc != NULL) { kfree(proc->data); kfree(proc); }
    return 0;
}

static int proc_fstat_op(void *ctx, struct stat *st) {
    ladybird_proc_ctx_t *proc = ctx;
    if (proc == NULL || st == NULL) return set_errno(EINVAL);
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0444u;
    st->st_size = proc->size;
    st->st_blksize = 4096u;
    st->st_blocks = (proc->size + 511u) / 512u;
    return 0;
}

static int proc_seek_op(void *ctx, off_t offset, int whence, off_t *result) {
    ladybird_proc_ctx_t *proc = ctx;
    off_t target;
    if (proc == NULL) return set_errno(EBADF);
    if (whence == SEEK_SET) target = offset;
    else if (whence == SEEK_CUR) target = (off_t)proc->offset + offset;
    else if (whence == SEEK_END) target = (off_t)proc->size + offset;
    else return set_errno(EINVAL);
    if (target < 0) return set_errno(EINVAL);
    proc->offset = (size_t)target;
    if (result != NULL) *result = target;
    return 0;
}

static int proc_fsync_op(void *ctx) { (void)ctx; return 0; }
static int proc_poll_op(void *ctx, short events, short *revents) {
    ladybird_proc_ctx_t *proc = ctx;
    if (proc == NULL || revents == NULL) return set_errno(EINVAL);
    *revents = 0;
    if ((events & POLLIN) != 0 && proc->offset < proc->size) *revents |= POLLIN;
    if ((events & POLLOUT) != 0) *revents |= POLLERR;
    return 1;
}

static size_t string_length(const char *text) {
    return text == NULL ? 0u : strlen(text);
}

static int string_equals(const char *left, const char *right) {
    if (left == NULL || right == NULL) {
        return 0;
    }

    return strcmp(left, right) == 0;
}

static int starts_with(const char *text, const char *prefix) {
    size_t i = 0;

    if (text == NULL || prefix == NULL) {
        return 0;
    }

    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }

    return 1;
}

static void copy_string(char *dst, size_t dst_size, const char *src) {
    size_t i = 0;

    if (dst == NULL || dst_size == 0u) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (i + 1u < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }

    dst[i] = '\0';
}

static void debug_log_size(const char *prefix, size_t n) {
    char buf[24];
    size_t i = sizeof(buf) - 1u;
    buf[i] = '\0';
    if (n == 0u) { buf[--i] = '0'; }
    while (n > 0u && i > 0u) { buf[--i] = (char)('0' + (n % 10u)); n /= 10u; }
    debug_log_puts(prefix);
    debug_log_puts(buf + i);
    debug_log_puts("\n");
}

static int set_errno(int value) {
    ladybird_errno_value = value;
    return -1;
}

static uint64_t monotonic_ms(void) {
    return scheduler_time_ns()/1000000ull;
}

static uint64_t ms_to_ticks(uint64_t ms) {
    return (ms + LADYBIRD_TICK_MS - 1ull) / LADYBIRD_TICK_MS;
}

static ssize_t set_errno_ssize(int value) {
    ladybird_errno_value = value;
    return -1;
}

static void path_join(char *dst, size_t dst_size, const char *base, const char *tail) {
    size_t length = 0;

    if (dst == NULL || dst_size == 0u) {
        return;
    }

    dst[0] = '\0';
    if (base == NULL || base[0] == '\0') {
        copy_string(dst, dst_size, tail);
        return;
    }

    while (length + 1u < dst_size && base[length] != '\0') {
        dst[length] = base[length];
        ++length;
    }

    if (length == 0u) {
        dst[length++] = '/';
    }

    if (length > 1u && dst[length - 1u] == '/') {
        --length;
    }

    if (length + 1u < dst_size) {
        dst[length++] = '/';
    }

    for (size_t i = 0; tail != NULL && tail[i] != '\0' && length + 1u < dst_size; ++i) {
        dst[length++] = tail[i];
    }

    dst[length] = '\0';
}

static void normalize_path(const char *path, char out[LADYBIRD_MAX_PATH]) {
    char working[LADYBIRD_MAX_PATH];
    size_t read_index = 0;
    size_t write_index = 0;

    if (path == NULL || path[0] == '\0') {
        copy_string(out, LADYBIRD_MAX_PATH, ladybird_cwd);
        return;
    }

    if (path[0] == '/') {
        copy_string(working, sizeof(working), path);
    } else {
        path_join(working, sizeof(working), ladybird_cwd, path);
    }

    while (working[read_index] != '\0' && write_index + 1u < sizeof(working)) {
        if (working[read_index] == '/') {
            if (write_index == 0u || working[write_index - 1u] != '/') {
                working[write_index++] = '/';
            }
            ++read_index;
            continue;
        }

        if (working[read_index] == '.' &&
            (working[read_index + 1u] == '/' || working[read_index + 1u] == '\0')) {
            read_index += working[read_index + 1u] == '/' ? 2u : 1u;
            continue;
        }

        if (working[read_index] == '.' &&
            working[read_index + 1u] == '.' &&
            (working[read_index + 2u] == '/' || working[read_index + 2u] == '\0')) {
            if (write_index > 1u) {
                --write_index;
                while (write_index > 0u && working[write_index - 1u] != '/') {
                    --write_index;
                }
            }
            read_index += working[read_index + 2u] == '/' ? 3u : 2u;
            continue;
        }

        working[write_index++] = working[read_index++];
    }

    if (write_index == 0u) {
        working[write_index++] = '/';
    }

    if (write_index > 1u && working[write_index - 1u] == '/') {
        --write_index;
    }

    working[write_index] = '\0';
    copy_string(out, LADYBIRD_MAX_PATH, working);
}

/* Rewrite Debian multiarch paths to their VexOS equivalents so the dynamic
 * linker compiled for Debian can find libraries stored at /lib/ and /usr/lib/.
 *
 * Examples:
 *   /lib/x86_64-linux-gnu/libfoo.so   ->  /lib/libfoo.so
 *   /usr/lib/x86_64-linux-gnu/libfoo  ->  /usr/lib/libfoo
 *   /lib/glibc-hwcaps/x86-64-v2/libf ->  /lib/libf
 */
static void rewrite_multiarch_path(char *path)
{
    static const struct { const char *from; const char *to; } table[] = {
        { "/lib/x86_64-linux-gnu/glibc-hwcaps/x86-64-v3/", "/lib/" },
        { "/lib/x86_64-linux-gnu/glibc-hwcaps/x86-64-v2/", "/lib/" },
        { "/lib/x86_64-linux-gnu/",                         "/lib/" },
        { "/lib/x86_64-linux-gnu",                          "/lib"  },
        { "/lib/glibc-hwcaps/x86-64-v3/",                  "/lib/" },
        { "/lib/glibc-hwcaps/x86-64-v2/",                  "/lib/" },
        { "/usr/lib/x86_64-linux-gnu/glibc-hwcaps/x86-64-v3/", "/usr/lib/" },
        { "/usr/lib/x86_64-linux-gnu/glibc-hwcaps/x86-64-v2/", "/usr/lib/" },
        { "/usr/lib/x86_64-linux-gnu/",                    "/usr/lib/" },
        { "/usr/lib/x86_64-linux-gnu",                     "/usr/lib"  },
        { NULL, NULL }
    };
    char tmp[LADYBIRD_MAX_PATH];
    size_t i;

    for (i = 0u; table[i].from != NULL; ++i) {
        size_t flen = string_length(table[i].from);
        if (starts_with(path, table[i].from)) {
            /* Reconstruct: to + suffix (everything after 'from'). */
            copy_string(tmp, sizeof(tmp), table[i].to);
            copy_string(tmp + string_length(table[i].to),
                        sizeof(tmp) - string_length(table[i].to),
                        path + flen);
            if (VEX_COMPAT_TRACE) {
                debug_log_puts("rewrite: ");
                debug_log_puts(path);
                debug_log_puts(" -> ");
                debug_log_puts(tmp);
                debug_log_puts("\n");
            }
            copy_string(path, LADYBIRD_MAX_PATH, tmp);
            return;
        }
    }
}

static void directory_marker_path(const char *path, char out[LADYBIRD_MAX_PATH]) {
    path_join(out, LADYBIRD_MAX_PATH, path, LADYBIRD_DIR_MARKER);
}

typedef struct ladybird_linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[];
} ladybird_linux_dirent64_t;

#define LADYBIRD_DT_UNKNOWN 0u
#define LADYBIRD_DT_DIR     4u
#define LADYBIRD_DT_REG     8u
#define LADYBIRD_DT_LNK     10u

static uint64_t directory_entry_inode(const char *path) {
    uint64_t hash = 1469598103934665603ull;

    if (path == NULL || path[0] == '\0') {
        return 1u;
    }
    while (*path != '\0') {
        hash ^= (uint64_t)(unsigned char)*path++;
        hash *= 1099511628211ull;
    }
    return hash != 0u ? hash : 1u;
}

/* One FD-owned directory snapshot: no shared scratch state and no pointers
 * borrowed from an ISO sector. dup/fork share its cursor through the FD object. */
static int dir_collect_child(const char *name, size_t size, uint8_t type, void *context) {
    (void)size;
    ladybird_dir_ctx_t *dir = context;
    if (!name[0] || string_equals(name, LADYBIRD_DIR_MARKER)) return 0;
    for (ladybird_dir_child_t *child = dir->children; child; child = child->next)
        if (string_equals(child->name, name)) return 0;
    if (string_length(name) >= LADYBIRD_MAX_PATH) return -1;
    ladybird_dir_child_t *child = kmalloc(sizeof(*child));
    if (!child) return -2;
    memset(child, 0, sizeof(*child));
    copy_string(child->name, sizeof(child->name), name);
    char full_path[LADYBIRD_MAX_PATH];
    path_join(full_path, sizeof(full_path), dir->path, name);
    child->inode = directory_entry_inode(full_path);
    child->type = type == FS_ENTRY_DIRECTORY ? LADYBIRD_DT_DIR :
        type == FS_ENTRY_SYMLINK ? LADYBIRD_DT_LNK : LADYBIRD_DT_REG;
    if (dir->tail) dir->tail->next = child;
    else dir->children = child;
    dir->tail = child;
    return 0;
}
static int path_parent(char out[LADYBIRD_MAX_PATH], const char *path) {
    size_t length = string_length(path);

    if (length == 0u) {
        return 0;
    }

    copy_string(out, LADYBIRD_MAX_PATH, path);
    while (length > 1u && out[length - 1u] != '/') {
        out[length - 1u] = '\0';
        --length;
    }

    if (length == 1u) {
        out[1] = '\0';
        return 1;
    }

    out[length - 1u] = '\0';
    return 1;
}

static int directory_exists_child(const char *name,size_t size,uint8_t type,void *context) {
    (void)name;(void)size;(void)type;*(int *)context=1;return 1;
}

static int path_is_directory(const char *path) {
    char marker[LADYBIRD_MAX_PATH];

    if (path == NULL) {
        return 0;
    }

    if (string_equals(path, "/")) {
        return 1;
    }

    if (proc_virtual_kind(path) == 2) {
        return 1;
    }

    directory_marker_path(path, marker);
    if (fs_exists(marker)) {
        return 1;
    }

    int found=0;
    (void)fs_list_directory(path,directory_exists_child,&found);
    if (found) {
        return 1;
    }

    return iso9660_is_directory(path);
}

static int path_exists_any(const char *path) {
    return fs_exists(path) || path_is_directory(path);
}

static int resolve_fs_path(const char *path, char out[LADYBIRD_MAX_PATH]) {
    char resolved[LADYBIRD_MAX_PATH];

    if (path == NULL || out == NULL || fs_resolve_path(path, resolved, sizeof(resolved)) != 0) {
        return -1;
    }
    copy_string(out, LADYBIRD_MAX_PATH, resolved);
    return 0;
}

static int path_access_allowed(const char *path, int require_read, int require_write, int require_exec, uint8_t follow_links) {
    return fs_access_path(path, require_read, require_write, require_exec, follow_links) == 0;
}

static int ensure_file_capacity(ladybird_file_ctx_t *file, size_t required) {
    uint8_t *resized;
    size_t new_capacity;

    if (file == NULL) {
        return -1;
    }

    if (required <= file->capacity) {
        return 0;
    }

    new_capacity = file->capacity == 0u ? 256u : file->capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2u) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2u;
    }

    resized = krealloc(file->buffer, new_capacity);
    if (resized == NULL) {
        return -1;
    }

    file->buffer = resized;
    file->capacity = new_capacity;
    return 0;
}

static int path_requires_admin_for_write(const char *path, int flags) {
    const int write_intent = (flags & O_CREAT) != 0 ||
        (flags & O_TRUNC) != 0 ||
        (flags & O_APPEND) != 0 ||
        (flags & O_ACCMODE) == O_WRONLY ||
        (flags & O_ACCMODE) == O_RDWR;

    if (!write_intent) {
        return 0;
    }

    return fs_is_protected(path) && !(fs_root_credential_caps(scheduler_current_capabilities())&VEX_CAP_FS_ADMIN);
}

static int root_flush_file(const char *path,void *opaque) {
    ladybird_file_ctx_t *file=opaque;
    return fs_write(path,file->size?file->buffer:NULL,file->size);
}
static int flush_file(ladybird_file_ctx_t *file) {
    if (file == NULL) {
        return 0;
    }
    if (file->object && file->object->shared_mapping_pages && file->write_allowed &&
        !file->volatile_shm && file->buffer && file->size) {
        process_memory_file_object_cache_copy(file->object, 0, file->buffer, file->size, 0);
        file->dirty = 1u;
    }
    if (!file->dirty) {
        return 0;
    }
    if (file->volatile_shm != 0u || file->unlinked != 0u) {
        file->dirty = 0u;
        return 0;
    }
    if (!file->write_allowed) {
        return -1;
    }

    int written;
    if(file->root) {
        char relative[512];
        if(fs_root_relative(file->root,file->path,relative,sizeof(relative)))return -1;
        written=fs_root_invoke(file->root,file->root_rights,file->granted_uid,file->granted_caps,relative,root_flush_file,file);
    } else written=fs_write(file->path,file->size?file->buffer:NULL,file->size);
    if (written != 0) {
        return -1;
    }

    file->dirty = 0;
    return 0;
}

static int fill_stat_for_path(const char *path, struct stat *st) {
    ora_view_t fs_lease_0 __attribute__((cleanup(ora_view_release)))={0};
    void *data = NULL;
    size_t size = 0;
    char marker[LADYBIRD_MAX_PATH];
    fs_metadata_t metadata;

    if (path == NULL || st == NULL) {
        return set_errno(EINVAL);
    }
    if(!fs_root_scope_allows(path,0))return set_errno(EACCES);

    memset(st, 0, sizeof(*st));
    st->st_blksize = 4096u;

    if (proc_virtual_kind(path) == 2) {
        st->st_mode = S_IFDIR | 0555u;
        st->st_uid = 0u;
        st->st_gid = 0u;
        return 0;
    }
    if (proc_virtual_kind(path) == 1) {
        st->st_mode = S_IFREG | 0444u;
        st->st_uid = 0u;
        st->st_gid = 0u;
        return 0;
    }

    if (string_equals(path, "/") || path_is_directory(path)) {
        directory_marker_path(path, marker);
        st->st_mode = S_IFDIR | FS_MODE_DEFAULT_DIR;
        st->st_uid = 0u;
        st->st_gid = 0u;
        st->st_size = 0u;
        st->st_blocks = fs_exists(marker) ? 1u : 0u;
        if (st->st_blocks != 0u && fs_get_metadata(marker, &metadata, 0u) == 0) {
            st->st_mode = S_IFDIR | (metadata.mode & 07777u);
            st->st_uid = metadata.uid;
            st->st_gid = metadata.gid;
        }
        return 0;
    }

    if (fs_get_metadata(path, &metadata, 1u) == 0) {
        st->st_uid = metadata.uid;
        st->st_gid = metadata.gid;
        st->st_mode = (metadata.type == FS_ENTRY_DIRECTORY ? S_IFDIR : S_IFREG) | metadata.mode;
    } else {
        st->st_uid = user_current_uid();
        st->st_gid = user_current_uid();
    }

    if (fs_read_view(path,&data,&size, &fs_lease_0) != 0) {
        /* fs_read fails for large ISO files — check iso9660 directly for the size. */
        uint32_t iso_lba, iso_sz;
        if (iso9660_get_location(path, &iso_lba, &iso_sz) == 0) {
            st->st_mode  = S_IFREG | FS_MODE_DEFAULT_FILE;
            st->st_uid   = 0u;
            st->st_gid   = 0u;
            st->st_size  = (off_t)iso_sz;
            st->st_blocks = (iso_sz + 511u) / 512u;
            return 0;
        }
        return set_errno(ENOENT);
    }

    if ((st->st_mode & S_IFMT) == 0u) {
        st->st_mode = S_IFREG | FS_MODE_DEFAULT_FILE;
    }
    st->st_size = size;
    st->st_blocks = (size + 511u) / 512u;
    return 0;
}

static uint32_t ladybird_fd_owner_pid(void) {
    return scheduler_current_process_id();
}

static int ladybird_fd_is_open(uint32_t owner_pid, int fd) {
    uint64_t flags=fd_inventory_lock();
    int found=ladybird_fd_find_slot(owner_pid,fd)!=NULL;
    fd_inventory_unlock(flags);return found;
}

static int create_console_fd(int fd, int readable, int writable) {
    ladybird_console_ctx_t *ctx;
    ladybird_fd_object_t *object;

    ctx = kmalloc(sizeof(*ctx));
    if (ctx == NULL) {
        return -1;
    }

    ctx->readable = readable;
    ctx->writable = writable;
    ctx->fd = fd;
    ctx->pending_char = 0;
    object = ladybird_fd_object_create(ctx, &console_ops);
    if (object == NULL) {
        kfree(ctx);
        return -1;
    }

    /* Initial stdio publication must not replace a descriptor concurrently
       installed by another thread (dup/redirect/another initializer). */
    uint32_t owner=ladybird_fd_owner_pid();
    if(!ladybird_fd_get_table(owner)){ladybird_fd_release(object);return -1;}
    uint64_t flags=fd_inventory_lock();
    ladybird_process_fd_table_t *table=ladybird_fd_find_table(owner);
    ladybird_fd_slot_t *slot=table?&table->slots[fd]:NULL;
    int installed=slot && !slot->object;
    if(installed){slot->object=object;slot->owner_pid=owner;slot->close_on_exec=0u;}
    fd_inventory_unlock(flags);
    if(!installed)ladybird_fd_release(object);
    return slot?fd:-1;
}

static void ladybird_ensure_standard_fds(void) {
    const uint32_t owner_pid = ladybird_fd_owner_pid();

    process_t *thread=scheduler_current_process();
    if(thread && thread->group && thread->group->ultra_space)return;
    uint8_t *ensuring=thread?&thread->compat_stdio_ensuring:&bootstrap_stdio_ensuring;
    if (*ensuring != 0u) {
        return;
    }
    *ensuring = 1u;

    if (!ladybird_fd_is_open(owner_pid, STDIN_FILENO)) {
        (void)create_console_fd(STDIN_FILENO, 1, 0);
    }
    if (!ladybird_fd_is_open(owner_pid, STDOUT_FILENO)) {
        (void)create_console_fd(STDOUT_FILENO, 0, 1);
    }
    if (!ladybird_fd_is_open(owner_pid, STDERR_FILENO)) {
        (void)create_console_fd(STDERR_FILENO, 0, 1);
    }

    *ensuring = 0u;
}

int ladybird_fd_clone_process(uint32_t parent_pid, uint32_t child_pid) {
    ladybird_posix_init();
    if(!parent_pid || !child_pid || parent_pid==child_pid)return -EINVAL;
    return ladybird_fd_clone_inventory(parent_pid,child_pid);
}

void ladybird_fd_close_process(uint32_t pid) {
    if(!pid)return;
    ladybird_fcntl_drop_process(pid);
    ladybird_process_fd_table_t *table=ladybird_fd_detach_table(pid);
    if(!table)return;
    for(unsigned i=0;i<LADYBIRD_MAX_FDS;i++)
        if(table->slots[i].object)ladybird_fd_release(table->slots[i].object);
    kfree(table);
}

int *ladybird_errno_location(void) {
    process_t *thread=scheduler_current_process();
    return thread ? &thread->kernel_errno : &bootstrap_kernel_errno;
}

ladybird_fd_object_t *ladybird_fd_object_create(void *ctx, const ladybird_fd_ops_t *ops) {
    ladybird_fd_object_t *object = kmalloc(sizeof(*object));

    if (object == NULL) {
        return NULL;
    }

    object->refcount = 1u;
    object->ctx = ctx;
    object->ops = ops;
    object->shared_mapping_pages = NULL;
    object->name_protection = NULL;
    if (ops == &file_ops && ctx) {
        ladybird_file_ctx_t *file=ctx;file->object=object;
        file->identity=fs_file_identity(file->path);
        uint64_t flags=fd_inventory_lock();
        file->live_next=live_file_contexts;live_file_contexts=file;
        fd_inventory_unlock(flags);
    }
    return object;
}

static int ladybird_fd_install_from(ladybird_fd_object_t *object, int preferred_fd,
                                   int minimum_fd, uint8_t close_on_exec) {
    if(!object)return set_errno(EBADF);
    ladybird_posix_init();
    if(preferred_fd>=LADYBIRD_MAX_FDS){ladybird_fd_release(object);return set_errno(EMFILE);}
    const uint32_t owner=ladybird_fd_owner_pid();
    ladybird_process_fd_table_t *table=ladybird_fd_get_table(owner);
    if(!table){ladybird_fd_release(object);return set_errno(ENOMEM);}
    uint64_t flags=fd_inventory_lock();
    /* No raw table pointer crosses the inventory transaction. Final process
       teardown may have detached it since get_table allocated/published it. */
    table=ladybird_fd_find_table(owner);
    if(!table){fd_inventory_unlock(flags);ladybird_fd_release(object);return set_errno(EBADF);}
    int fd=preferred_fd;
    if(fd<0)for(fd=minimum_fd;fd<LADYBIRD_MAX_FDS && table->slots[fd].object;fd++){}
    if(fd==LADYBIRD_MAX_FDS){fd_inventory_unlock(flags);ladybird_fd_release(object);return set_errno(EMFILE);}
    ladybird_fd_object_t *previous=table->slots[fd].object;
    table->slots[fd]=(ladybird_fd_slot_t){.owner_pid=owner,.fd=fd,
        .close_on_exec=close_on_exec!=0,.object=object};
    fd_inventory_unlock(flags);
    /* Replacement is atomic; any object-specific close runs after unlocking. */
    if(previous){description_range_closed(owner,previous,0);ladybird_fd_release(previous);}
    return fd;
}

int ladybird_fd_install_flags(ladybird_fd_object_t *object, int preferred_fd,
                              uint8_t close_on_exec) {
    return ladybird_fd_install_from(object, preferred_fd, 0, close_on_exec);
}

int ladybird_fd_install(ladybird_fd_object_t *object, int preferred_fd) {
    return ladybird_fd_install_flags(object, preferred_fd, 0u);
}

int vex_fd_install_prepared(uint32_t owner,void *opaque,int fd,uint8_t cloexec) {
    ladybird_fd_object_t *object=opaque;
    if(!object || fd<0 || fd>=LADYBIRD_MAX_FDS)return -EINVAL;
    if(!scheduler_construction_owned(owner))return -EPERM;
    ladybird_process_fd_table_t *table=ladybird_fd_get_table(owner);if(!table)return -ENOMEM;
    uint64_t irq=fd_inventory_lock();table=ladybird_fd_find_table(owner);
    int error=0;
    if(!table || table->slots[fd].object)error=-EINVAL;
    else if(!object->refcount || object->refcount==UINT32_MAX)error=-EMFILE;
    else {++object->refcount;table->slots[fd]=(ladybird_fd_slot_t){.owner_pid=owner,.fd=fd,.close_on_exec=cloexec!=0,.object=object};}
    fd_inventory_unlock(irq);return error;
}

static int vex_open_mode_impl(const char *,int,uint32_t,int);
#include "vex_fs_root_provider.inc"

int vex_web_fd_is_wk2_shared_memory(int fd) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);
    int matches = 0;
    if (object != NULL && object->ops == &file_ops && object->ctx != NULL) {
        matches = wk2_shared_path(((ladybird_file_ctx_t *)object->ctx)->path);
    }
    if (object != NULL) (void)ladybird_fd_release(object);
    return matches;
}

void ladybird_fd_close_exec(uint32_t pid) {
    if(!pid)return;
    for(unsigned i=0;i<LADYBIRD_MAX_FDS;i++) {
        uint64_t flags=fd_inventory_lock();
        ladybird_fd_slot_t *slot=ladybird_fd_find_slot(pid,(int)i);
        ladybird_fd_object_t *object=NULL;
        if(slot && slot->close_on_exec){object=slot->object;*slot=(ladybird_fd_slot_t){0};}
        fd_inventory_unlock(flags);
        if(object)description_range_closed(pid,object,0);
        if(object && object->ops==&file_ops && object->ctx)
            ladybird_fcntl_drop_path(pid,((ladybird_file_ctx_t *)object->ctx)->path);
        if(object)ladybird_fd_release(object);
    }
}

ladybird_fd_object_t *ladybird_fd_acquire(int fd) {
    ladybird_posix_init();
    uint64_t flags=fd_inventory_lock();
    ladybird_fd_slot_t *slot=ladybird_fd_find_slot(ladybird_fd_owner_pid(),fd);
    ladybird_fd_object_t *object=slot?slot->object:NULL;
    if(object && object->refcount<UINT32_MAX)++object->refcount;
    else object=NULL;
    fd_inventory_unlock(flags);return object;
}

static void fd_cancel_stopped_object(ladybird_fd_object_t *object,uint32_t tid);
#define FD_BORROW_PRESERVE_ERRNO 1
#define FD_BORROW_CANCEL_OBJECT(object,tid) fd_cancel_stopped_object(object,tid)
#include "fd_borrow.inc"
#undef FD_BORROW_CANCEL_OBJECT
#undef FD_BORROW_PRESERVE_ERRNO

int ladybird_fd_release(ladybird_fd_object_t *object) {
    int close_result = 0;

    if (object == NULL) {
        return 0;
    }

    uint64_t flags=fd_inventory_lock();
    const uint8_t last=object->refcount && --object->refcount==0u;
    fd_inventory_unlock(flags);
    if (last) {
        /* Materialized shared pages belong to this object. Mappings retain
         * the object, so this is also after its last mapping/IPC reference. */
        if (object->shared_mapping_pages && object->ops == &file_ops && object->ctx) {
            ladybird_file_ctx_t *file = object->ctx;
            if (file->buffer && file->size) {
                process_memory_file_object_cache_copy(object, 0, file->buffer, file->size, 0);
                if (file->write_allowed && !file->volatile_shm) file->dirty = 1u;
            }
        }
        process_memory_file_object_release(object);
        if (object->ops != NULL && object->ops->close != NULL) {
            close_result = object->ops->close(object->ctx);
        }
        fs_name_protection_release(object->name_protection);
        kfree(object);
    }
    return close_result;
}

int ladybird_fd_dup_object(int fd, int preferred_fd) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);

    if (object == NULL) {
        return set_errno(EBADF);
    }

    return ladybird_fd_install(object, preferred_fd);
}

int vex_web_eventfd_create(uint64_t initial, uint32_t flags) {
    ladybird_event_counter_ctx_t *event;
    ladybird_fd_object_t *object;
    uint64_t capability = 0u;
    uint32_t native_flags = 0u;

    /* Linux EFD_CLOEXEC is currently a descriptor policy bit.  It is safe to
     * accept it here because exec closes/rebuilds the compatibility table;
     * the counter itself only receives native semantic flags. */
    if ((flags & ~(uint32_t)(0x1u | 0x800u | 0x80000u)) != 0u) {
        return set_errno(EINVAL);
    }
    if ((flags & 0x1u) != 0u) native_flags |= VEX_EVENT_COUNTER_SEMAPHORE;
    if ((flags & 0x800u) != 0u) native_flags |= VEX_EVENT_COUNTER_NONBLOCK;
    if (vex_event_counter_create(initial, native_flags, &capability) != 0) {
        return set_errno(ENOMEM);
    }
    event = kmalloc(sizeof(*event));
    if (event == NULL) {
        (void)vex_event_counter_release(capability);
        return set_errno(ENOMEM);
    }
    event->capability = capability;
    object = ladybird_fd_object_create(event, &event_counter_ops);
    if (object == NULL) {
        (void)vex_event_counter_release(capability);
        kfree(event);
        return set_errno(ENOMEM);
    }
    return ladybird_fd_install_flags(object, -1,
                                     (flags & 0x80000u) != 0u ? 1u : 0u);
}

void ladybird_posix_init(void) {
    if (__atomic_load_n(&ladybird_posix_ready,__ATOMIC_ACQUIRE)==2u) {
        ladybird_ensure_standard_fds();
        return;
    }

    KERNEL_GUARD(posix_init_gate);
    /* State 1 is visible only to recursive initialization on this thread.
       Other CPUs must wait for the release publication of complete defaults. */
    if(ladybird_posix_ready) {ladybird_ensure_standard_fds();return;}
    memset(ladybird_mappings, 0, sizeof(ladybird_mappings));
    memset(ladybird_env, 0, sizeof(ladybird_env));
    copy_string(ladybird_cwd, sizeof(ladybird_cwd), "/");
    ladybird_errno_value = 0;
    __atomic_store_n(&ladybird_posix_ready,1u,__ATOMIC_RELAXED);

    /* Default environment for all Linux-ABI processes.
     * These mirror what a real Linux distro would have in /etc/environment.
     * Processes can still override via setenv(name, value, 1). */
    setenv("HOME",              "/home/root",                               0);
    setenv("XDG_DATA_HOME",     "/home/root/.local/share",                   0);
    setenv("XDG_CONFIG_HOME",   "/home/root/.config",                        0);
    setenv("XDG_STATE_HOME",    "/home/root/.local/state",                   0);

    /* TLS / HTTPS — point libsoup + GnuTLS at the CA bundle on the CD */
    setenv("SSL_CERT_FILE",    "/etc/ssl/certs/ca-certificates.crt", 0);
    setenv("SSL_CERT_DIR",     "/etc/ssl/certs",                     0);
    setenv("CURL_CA_BUNDLE",   "/etc/ssl/certs/ca-certificates.crt", 0);

    /* Fonts — tell fontconfig where its config and cache live */
    setenv("FONTCONFIG_PATH",  "/etc/fonts",   0);

    /* GLib / GIO data dirs — required for gio TLS module discovery */
    setenv("XDG_DATA_DIRS",    "/usr/share:/usr/local/share", 0);
    setenv("XDG_CACHE_HOME",   "/home/root/.cache",           0);

    ladybird_ensure_standard_fds();
    __atomic_store_n(&ladybird_posix_ready,2u,__ATOMIC_RELEASE);
}

static ssize_t console_read(void *ctx, void *buffer, size_t count) {
    ladybird_console_ctx_t *console = ctx;
    char *bytes = buffer;
    char value;

    if (console == NULL || !console->readable || buffer == NULL) {
        return set_errno_ssize(EBADF);
    }
    if (count == 0u) {
        return 0;
    }

    value = console->pending_char;
    console->pending_char = 0;
    if (value == 0) {
        value = keyboard_pollchar();
    }
    if (value == 0) {
        return set_errno_ssize(EAGAIN);
    }

    bytes[0] = value;
    return 1;
}

static ssize_t console_write(void *ctx, const void *buffer, size_t count) {
    GUI_GUARD;
    const uint8_t *bytes = buffer;
    ladybird_console_ctx_t *console = ctx;

    if (console == NULL || !console->writable || buffer == NULL) {
        return set_errno_ssize(EBADF);
    }

    for (size_t i = 0; i < count; ++i) {
        tty_putchar((char)bytes[i]);
    }

    /* Preserve the complete stderr write, including fatal diagnostics, while
       letting the logger batch UART output rather than entering once/byte. */
    if (console->fd == STDERR_FILENO) debug_log_write(bytes, count);

    return (ssize_t)count;
}

static int console_close(void *ctx) {
    kfree(ctx);
    return 0;
}

static int console_fstat(void *ctx, struct stat *st) {
    (void)ctx;

    if (st == NULL) {
        return set_errno(EINVAL);
    }

    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    return 0;
}

static int console_seek(void *ctx, off_t offset, int whence, off_t *result) {
    (void)ctx;
    (void)offset;
    (void)whence;
    (void)result;
    return set_errno(ESPIPE);
}

static int console_fsync(void *ctx) {
    (void)ctx;
    return 0;
}

static int console_poll(void *ctx, short events, short *revents) {
    ladybird_console_ctx_t *console = ctx;

    if (console == NULL || revents == NULL) {
        return set_errno(EINVAL);
    }

    *revents = 0;
    if ((events & POLLIN) != 0 && console->readable) {
        if (console->pending_char == 0) {
            console->pending_char = keyboard_pollchar();
        }
        if (console->pending_char != 0) {
            *revents |= POLLIN;
        }
    }
    if ((events & POLLOUT) != 0 && console->writable) {
        *revents |= POLLOUT;
    }
    return 0;
}

static ssize_t file_read_op(void *ctx, void *buffer, size_t count) {
    ladybird_file_ctx_t *file = ctx;
    size_t available;
    if(!file)return set_errno_ssize(EINVAL);
    KERNEL_GUARD(file->data_gate);

    if (file == NULL || (buffer == NULL && count != 0u)) {
        return set_errno_ssize(EINVAL);
    }
    if ((file->flags & O_ACCMODE) == O_WRONLY) {
        return set_errno_ssize(EBADF);
    }

    if (file->offset >= file->size) {
        return 0;
    }

    available = file->size - file->offset;
    if (count > available) {
        count = available;
    }

    memcpy(buffer, file->buffer + file->offset, count);
    file->offset += count;
    return (ssize_t)count;
}

static ssize_t file_pread_op(void *ctx, void *buffer, size_t count,
                             off_t offset) {
    ladybird_file_ctx_t *file = ctx;
    size_t position;
    size_t available;
    if(!file)return set_errno_ssize(EINVAL);
    KERNEL_GUARD(file->data_gate);

    if (file == NULL || (buffer == NULL && count != 0u)) {
        return set_errno_ssize(EINVAL);
    }
    if ((file->flags & O_ACCMODE) == O_WRONLY) {
        return set_errno_ssize(EBADF);
    }
    if (offset < 0) {
        return set_errno_ssize(EINVAL);
    }
    if (count == 0u || (uint64_t)offset >= (uint64_t)file->size) {
        return 0;
    }
    if (file->buffer == NULL) return set_errno_ssize(EIO);
    position = (size_t)offset;
    available = file->size - position;
    if (count > available) count = available;
    memcpy(buffer, file->buffer + position, count);
    return (ssize_t)count;
}

static ssize_t file_pwrite_op(void *ctx, const void *buffer, size_t count,
                              off_t offset) {
    ladybird_file_ctx_t *file = ctx;
    size_t position;
    size_t target_end;
    if(!file)return set_errno_ssize(EINVAL);
    KERNEL_GUARD(file->data_gate);

    if (file == NULL || (buffer == NULL && count != 0u)) {
        return set_errno_ssize(EINVAL);
    }
    if ((file->flags & O_ACCMODE) == O_RDONLY) {
        return set_errno_ssize(EBADF);
    }
    if (!file->write_allowed) {
        return set_errno_ssize(EACCES);
    }
    if (offset < 0) {
        return set_errno_ssize(EINVAL);
    }
    if (count == 0u) return 0;
    if ((uint64_t)offset > (uint64_t)SIZE_MAX)
        return set_errno_ssize(EINVAL);
    position = (size_t)offset;

    if (count > SIZE_MAX - position) {
        return set_errno_ssize(EINVAL);
    }
    target_end = position + count;
    if (ensure_file_capacity(file, target_end) != 0) {
        return set_errno_ssize(ENOMEM);
    }

    /* POSIX requires sparse holes created by positional writes to read as
     * zeroes, even when the backing allocation contains previously unused
     * bytes rather than freshly zeroed memory. */
    if (position > file->size) {
        memset(file->buffer + file->size, 0, position - file->size);
    }

    memcpy(file->buffer + position, buffer, count);
    if (target_end > file->size) {
        file->size = target_end;
    }

    file->dirty = 1;
    return (ssize_t)count;
}

static ssize_t file_write_op(void *ctx, const void *buffer, size_t count) {
    ladybird_file_ctx_t *file = ctx;
    size_t position;
    ssize_t result;

    if (file == NULL) return set_errno_ssize(EINVAL);
    KERNEL_GUARD(file->data_gate);
    position = (file->flags & O_APPEND) != 0u ? file->size : file->offset;
    if (position > (size_t)INT64_MAX) return set_errno_ssize(EINVAL);
    result = file_pwrite_op(file, buffer, count, (off_t)position);
    if (result > 0) file->offset = position + (size_t)result;
    return result;
}

static int file_close_op(void *ctx) {
    ladybird_file_ctx_t *file = ctx;
    int flush_result = 0;

    if (file == NULL) {
        return 0;
    }
    description_range_closed(0,file->object,1);

    flush_result = flush_file(file);
    /* Remain visible until the final flush completes: a concurrent confined
     * REMOVE must not delete a name which this closing object can recreate. */
    uint64_t inventory=fd_inventory_lock();
    ladybird_file_ctx_t **entry=&live_file_contexts;
    while(*entry && *entry!=file)entry=&(*entry)->live_next;
    if(*entry)*entry=file->live_next;
    fd_inventory_unlock(inventory);
    if(flush_result)__atomic_add_fetch(&file_close_failures,1,__ATOMIC_RELEASE);
    if (file->buffer_owned != 0u) {
        kfree(file->buffer);
    }
    ora_view_release(&file->content);
    fs_root_release(file->root);
    kfree(file);
    if (flush_result != 0) {
        return set_errno(EIO);
    }
    return 0;
}

typedef struct {void *file;struct stat *stat;} rooted_file_stat;
static int rooted_file_stat_call(const char *path,void *opaque) {
    (void)path;rooted_file_stat *r=opaque;return file_fstat_op(r->file,r->stat);
}
static int file_fstat_op(void *ctx, struct stat *st) {
    ladybird_file_ctx_t *file = ctx;
    if(!file)return set_errno(EINVAL);
    KERNEL_GUARD(file->data_gate);
    if(file->root && !fs_root_active(file->root)) {
        char relative[512];rooted_file_stat r={file,st};
        if(fs_root_relative(file->root,file->path,relative,sizeof(relative)))return set_errno(EACCES);
        return fs_root_invoke(file->root,file->root_rights,file->granted_uid,file->granted_caps,relative,rooted_file_stat_call,&r);
    }

    if (file == NULL || st == NULL) {
        return set_errno(EINVAL);
    }

    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | FS_MODE_DEFAULT_FILE;
    st->st_uid = file->granted_uid;
    st->st_gid = file->granted_uid;
    if (!file->unlinked && !file->volatile_shm) {
        fs_metadata_t metadata;
        if (fs_get_metadata(file->path, &metadata, 1u) == 0) {
            st->st_mode = S_IFREG | (metadata.mode & 07777u);
            st->st_uid = metadata.uid;
            st->st_gid = metadata.gid;
        }
    }
    st->st_size = file->size;
    st->st_blksize = 4096u;
    st->st_blocks = (file->size + 511u) / 512u;
    return 0;
}

static int file_seek_op(void *ctx, off_t offset, int whence, off_t *result) {
    ladybird_file_ctx_t *file = ctx;
    uint64_t base=0,target;

    if (file == NULL) {
        return set_errno(EBADF);
    }
    KERNEL_GUARD(file->data_gate);

    switch (whence) {
        case SEEK_SET:
            break;
        case SEEK_CUR:
            base=file->offset;
            break;
        case SEEK_END:
            base=file->size;
            break;
        default:
            return set_errno(EINVAL);
    }

    if(base>INT64_MAX)return set_errno(EINVAL);
    if(offset<0) {
        uint64_t magnitude=(uint64_t)(-(offset+1))+1u;
        if(magnitude>base)return set_errno(EINVAL);target=base-magnitude;
    } else {
        if((uint64_t)offset>INT64_MAX-base)return set_errno(EINVAL);target=base+(uint64_t)offset;
    }

    file->offset = (size_t)target;
    if (result != NULL) {
        *result = (off_t)target;
    }
    return 0;
}

static int file_fsync_op(void *ctx) {
    ladybird_file_ctx_t *file=ctx;
    if(!file)return set_errno(EBADF);
    /* Shared-page cache callbacks use memory -> file. Keep that order even
       when flush needs to overlay materialized MAP_SHARED pages. */
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    KERNEL_GUARD(file->data_gate);
    if (flush_file((ladybird_file_ctx_t *)ctx) != 0) {
        return set_errno(EIO);
    }
    return 0;
}

static int file_poll_op(void *ctx, short events, short *revents) {
    ladybird_file_ctx_t *file = ctx;
    if(!file)return set_errno(EINVAL);
    KERNEL_GUARD(file->data_gate);

    if (file == NULL || revents == NULL) {
        return set_errno(EINVAL);
    }

    *revents = 0;
    if ((events & POLLIN) != 0 && file->offset < file->size) {
        *revents |= POLLIN;
    }
    if ((events & POLLOUT) != 0) {
        *revents |= POLLOUT;
    }
    return 0;
}
int vex_fd_sync_all(void) {
    size_t capacity=0,count=0;uint64_t flags=fd_inventory_lock();
    for(ladybird_process_fd_table_t *t=ladybird_fd_tables;t;t=t->next)
        for(unsigned i=0;i<LADYBIRD_MAX_FDS;i++)capacity+=t->slots[i].object!=NULL;
    fd_inventory_unlock(flags);
    if(capacity>SIZE_MAX/sizeof(ladybird_fd_object_t *))return -1;
    ladybird_fd_object_t **objects=capacity?kmalloc(capacity*sizeof(*objects)):NULL;
    if(capacity && !objects)return -1;
    int result=0;flags=fd_inventory_lock();
    for(ladybird_process_fd_table_t *t=ladybird_fd_tables;t;t=t->next)for(unsigned i=0;i<LADYBIRD_MAX_FDS;i++) {
        ladybird_fd_object_t *o=t->slots[i].object;
        if(!o || o->ops!=&file_ops)continue;
        size_t j=0;while(j<count && objects[j]!=o)++j;if(j<count)continue;
        if(count==capacity || o->refcount==UINT32_MAX){result=-1;continue;}
        ++o->refcount;objects[count++]=o;
    }
    fd_inventory_unlock(flags);
    for(size_t i=0;i<count;i++) {
        if(file_fsync_op(objects[i]->ctx))result=-1;
        if(ladybird_fd_release(objects[i]))result=-1;
    }
    kfree(objects);return result;
}

static ssize_t pipe_read_op(void *ctx,void *buffer,size_t count) {
    ladybird_pipe_ctx_t *pipe=ctx;
    if(!pipe || !pipe->readable || !buffer)return set_errno_ssize(EBADF);
    if(!count)return 0;
    for(;;) {
        int n=pipe_read(pipe->pipe_id,buffer,count);
        if(n<0)return set_errno_ssize(EIO);
        if(n || !pipe_has_writers(pipe->pipe_id))return n;
        if(pipe->flags&O_NONBLOCK)return set_errno_ssize(EAGAIN);
        if(pipe_wait(pipe->pipe_id,0)<0)return set_errno_ssize(EINTR);
    }
}

static ssize_t pipe_write_op(void *ctx, const void *buffer, size_t count) {
    ladybird_pipe_ctx_t *pipe = ctx;
    int write_result;

    if (pipe == NULL || !pipe->writable || buffer == NULL) {
        return set_errno_ssize(EBADF);
    }

    if (!pipe_has_readers(pipe->pipe_id)) {
        return set_errno_ssize(EPIPE);
    }

    do {
        write_result = pipe_write(pipe->pipe_id, buffer, count);
        if(write_result || !count)break;
        if((pipe->flags&O_NONBLOCK)!=0)return set_errno_ssize(EAGAIN);
        if(pipe_wait(pipe->pipe_id,1)<0)return set_errno_ssize(EINTR);
    } while(pipe_has_readers(pipe->pipe_id));
    if(!write_result && count && !pipe_has_readers(pipe->pipe_id))return set_errno_ssize(EPIPE);
    if (write_result < 0) {
        return set_errno_ssize(EPIPE);
    }
    if (write_result == 0 && count != 0u && (pipe->flags & O_NONBLOCK) != 0) {
        return set_errno_ssize(EAGAIN);
    }

    return (ssize_t)write_result;
}

static int pipe_close_op(void *ctx) {
    ladybird_pipe_ctx_t *pipe = ctx;

    if (pipe == NULL) {
        return 0;
    }

    pipe_endpoint_close(pipe->pipe_id, pipe->readable != 0u, pipe->writable != 0u);
    kfree(pipe);
    return 0;
}

static int pipe_fstat_op(void *ctx, struct stat *st) {
    (void)ctx;

    if (st == NULL) {
        return set_errno(EINVAL);
    }

    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    return 0;
}

static int pipe_seek_op(void *ctx, off_t offset, int whence, off_t *result) {
    (void)ctx;
    (void)offset;
    (void)whence;
    (void)result;
    return set_errno(ESPIPE);
}

static int pipe_fsync_op(void *ctx) {
    (void)ctx;
    return 0;
}

static int pipe_poll_op(void *ctx, short events, short *revents) {
    ladybird_pipe_ctx_t *pipe = ctx;

    if (pipe == NULL || revents == NULL) {
        return set_errno(EINVAL);
    }

    *revents = 0;
    if ((events & POLLIN) != 0 && pipe->readable) {
        if (pipe_readable(pipe->pipe_id) != 0u || !pipe_has_writers(pipe->pipe_id)) {
            *revents |= POLLIN;
        }
        if (!pipe_has_writers(pipe->pipe_id)) {
            *revents |= POLLHUP;
        }
    }
    if ((events & POLLOUT) != 0 && pipe->writable) {
        if (pipe_has_readers(pipe->pipe_id) && pipe_writable(pipe->pipe_id) != 0u) {
            *revents |= POLLOUT;
        }
        if (!pipe_has_readers(pipe->pipe_id)) {
            *revents |= POLLERR;
        }
    }
    return 0;
}

/* ── iso9660 streaming fd operations ────────────────────────────────────── */

static ssize_t iso_stream_read_op(void *ctx, void *buffer, size_t count) {
    ladybird_iso_stream_ctx_t *f = (ladybird_iso_stream_ctx_t *)ctx;
    int n;
    static unsigned iso_read_reports;
    if (VEX_COMPAT_TRACE && iso_read_reports++ < 64u)
        debug_log_size("iso-read: count=", count);
    n = iso9660_pread(f->lba, f->file_size, f->offset, buffer, count);
    if (n > 0) f->offset += (size_t)n;
    if (n < 0) debug_log_puts("iso-read: pread failed\n");
    return (ssize_t)n;
}

static ssize_t iso_stream_pread_op(void *ctx, void *buffer, size_t count,
                                   off_t offset) {
    ladybird_iso_stream_ctx_t *f = (ladybird_iso_stream_ctx_t *)ctx;
    if (f == NULL || (buffer == NULL && count != 0u))
        return set_errno_ssize(EINVAL);
    if (offset < 0) return set_errno_ssize(EINVAL);
    if ((uint64_t)offset >= (uint64_t)f->file_size) return 0;
    return (ssize_t)iso9660_pread(f->lba, f->file_size, (size_t)offset,
                                  buffer, count);
}

static ssize_t iso_stream_write_op(void *ctx, const void *buffer, size_t count) {
    (void)ctx; (void)buffer; (void)count;
    ladybird_errno_value = EROFS;
    return -1;
}

static int iso_stream_close_op(void *ctx) {
    fs_root_release(((ladybird_iso_stream_ctx_t *)ctx)->root);
    kfree(ctx);
    return 0;
}

static int iso_stream_fstat_op(void *ctx, struct stat *st) {
    ladybird_iso_stream_ctx_t *f = (ladybird_iso_stream_ctx_t *)ctx;
    if (st == NULL) return set_errno(EINVAL);
    memset(st, 0, sizeof(*st));
    st->st_mode    = S_IFREG | 0444u;
    st->st_size    = (off_t)f->file_size;
    st->st_blksize = 2048;
    st->st_blocks  = (f->file_size + 511u) / 512u;
    if (VEX_COMPAT_TRACE) debug_log_size("iso-fstat: size=", f->file_size);
    return 0;
}

static int iso_stream_seek_op(void *ctx, off_t offset, int whence, off_t *result) {
    ladybird_iso_stream_ctx_t *f = (ladybird_iso_stream_ctx_t *)ctx;
    off_t new_off;

    switch (whence) {
    case SEEK_SET: new_off = offset; break;
    case SEEK_CUR: new_off = (off_t)f->offset + offset; break;
    case SEEK_END: new_off = (off_t)f->file_size + offset; break;
    default:       return set_errno(EINVAL);
    }

    if (new_off < 0) return set_errno(EINVAL);
    f->offset = (size_t)new_off;
    if (result != NULL) *result = new_off;
    return 0;
}

static int iso_stream_fsync_op(void *ctx) { (void)ctx; return 0; }

static int iso_stream_poll_op(void *ctx, short events, short *revents) {
    (void)ctx;
    if (revents != NULL) *revents = events & (short)(POLLIN | POLLOUT);
    return 1;
}

int open(const char *path, int flags, ...) {
    /* Kernel callers never pass a creation mode; native userspace uses
       VEX_SYS_OPEN_EX, which reaches vex_open_mode() directly. */
    return vex_open_mode(path, flags, FS_MODE_DEFAULT_FILE);
}

static unsigned root_file_rights(int flags) {
    unsigned access=(flags&O_ACCMODE)==O_RDONLY?FS_ROOT_READ:
        ((flags&O_ACCMODE)==O_WRONLY?FS_ROOT_WRITE:(FS_ROOT_READ|FS_ROOT_WRITE));
    return access|((flags&O_CREAT)?FS_ROOT_CREATE:0)|(fs_root_current_rights()&FS_ROOT_TRANSFER);
}
static int vex_open_mode_impl(const char *path, int flags, uint32_t create_mode,int protect_name) {
    /* Publication must participate in the same namespace transaction as
     * confined REMOVE. A reader snapshot not yet in the FD inventory cannot
     * race deletion and later recreate that pathname during final flush. */
    kernel_guard_t namespace __attribute__((cleanup(fs_namespace_end)))=fs_namespace_begin();
    fs_name_protection_t *protection __attribute__((cleanup(fs_name_protection_cleanup)))=NULL;
    ora_view_t boot_content __attribute__((cleanup(ora_view_release)))={0};
    ora_view_t fs_lease_1 __attribute__((cleanup(ora_view_release)))={0};
    char normalized[LADYBIRD_MAX_PATH];
    char resolved[LADYBIRD_MAX_PATH];
    ladybird_file_ctx_t *file;
    ladybird_fd_object_t *object;
    void *data = NULL;
    size_t size = 0;
    uint32_t iso_lba = 0u, iso_size = 0u;
    const void *boot_data = NULL;
    size_t boot_size = 0u;
    int boot_borrowed = 0;
    int installed_fd;
    const uint8_t write_allowed =
        ((flags & O_CREAT) != 0) ||
        ((flags & O_TRUNC) != 0) ||
        ((flags & O_APPEND) != 0) ||
        (flags & O_ACCMODE) == O_WRONLY ||
        (flags & O_ACCMODE) == O_RDWR;

    ladybird_posix_init();
    normalize_path(path, normalized);
    if(!fs_root_scope_allows(normalized,0))return set_errno(EACCES);
    rewrite_multiarch_path(normalized);
    wk2_shared_trace("open.begin", normalized, (uint32_t)flags,
                     0u, 0u);
    hsts_file_trace("open.begin", normalized, (uint32_t)flags,
                    (uint64_t)fs_exists(normalized), 0);
    copy_string(resolved, sizeof(resolved), normalized);
    if (!wk2_shared_path(normalized) && fs_exists(normalized)) {
        (void)resolve_fs_path(normalized, resolved);
    }
    if(protect_name) {
        if(write_allowed)return set_errno(EINVAL);
        if(ladybird_device_kind(normalized)>=0 || proc_virtual_kind(normalized) || wk2_shared_path(normalized))return set_errno(ENOTSUP);
        int error;protection=fs_name_protect(resolved,&error);
        if(!protection)return set_errno(-error);
    }

    if(VEX_COMPAT_TRACE) {
        debug_log_puts("open: ");
        debug_log_puts(normalized);
        debug_log_puts("\n");
    }

    {
        const int dev_kind = ladybird_device_kind(normalized);
        if (dev_kind >= 0) {
            ladybird_dev_ctx_t *dev = kmalloc(sizeof(*dev));
            if (dev == NULL) {
                return set_errno(ENOMEM);
            }
            dev->kind = dev_kind;
            object = ladybird_fd_object_create(dev, &dev_ops);
            if (object == NULL) {
                kfree(dev);
                return set_errno(ENOMEM);
            }
            return ladybird_fd_install(object, -1);
        }
    }

    if (proc_virtual_kind(normalized) == 1) {
        if (write_allowed) return set_errno(EACCES);
        return proc_open_virtual(normalized);
    }

    /* WebKit's POSIX shm_open names are a Linux compatibility view. Keep
     * these descriptors in a refcounted in-memory object; routing ftruncate
     * through VexFS would turn temporary shared memory into persistent files
     * and exhaust the filesystem's fixed file table during repeated frames. */
    if (wk2_shared_path(normalized)) {
        const uint32_t owner_pid = ladybird_fd_owner_pid();
        ladybird_fd_object_t *existing = NULL;
        uint64_t inventory_flags=fd_inventory_lock();
        ladybird_process_fd_table_t *table=ladybird_fd_find_table(owner_pid);
        for (int i = 0; table && i < LADYBIRD_MAX_FDS; ++i) {
            ladybird_fd_object_t *candidate = table->slots[i].object;
            ladybird_file_ctx_t *candidate_file;
            if (candidate == NULL ||
                candidate->ops != &file_ops || candidate->ctx == NULL) {
                continue;
            }
            candidate_file = (ladybird_file_ctx_t *)candidate->ctx;
            if (candidate_file->volatile_shm != 0u &&
                candidate_file->unlinked == 0u &&
                string_equals(candidate_file->path, normalized)) {
                existing = candidate;
                break;
            }
        }
        int existing_error=0;
        if(existing) {
            if((flags&O_CREAT) && (flags&LADYBIRD_O_EXCL))existing_error=EEXIST;
            else if(existing->refcount==UINT32_MAX)existing_error=EMFILE;
            else ++existing->refcount;
        }
        fd_inventory_unlock(inventory_flags);
        if (existing != NULL) {
            if ((flags & O_CREAT) != 0 && (flags & LADYBIRD_O_EXCL) != 0) {
                wk2_shared_trace("open.fail", normalized, (uint32_t)flags,
                                 0u, (uint64_t)(int64_t)-EEXIST);
                return set_errno(existing_error);
            }
            if(existing_error)return set_errno(existing_error);
            installed_fd = ladybird_fd_install(existing, -1);
            wk2_shared_trace("open.end", normalized, (uint32_t)flags, 0u,
                             (uint64_t)(int64_t)installed_fd);
            return installed_fd;
        }
        if ((flags & O_CREAT) == 0) {
            wk2_shared_trace("open.fail", normalized, (uint32_t)flags,
                             0u, (uint64_t)(int64_t)-ENOENT);
            return set_errno(ENOENT);
        }
        file = kmalloc(sizeof(*file));
        if (file == NULL) return set_errno(ENOMEM);
        memset(file, 0, sizeof(*file));
        file->data_gate=(kernel_gate_t)KERNEL_GATE_INIT;
        copy_string(file->path, sizeof(file->path), normalized);
        file->flags = flags;
        file->write_allowed = write_allowed;
        file->buffer_owned = 1u;
        file->volatile_shm = 1u;
        file->granted_uid = fs_root_credential_uid(user_current_uid());
        file->granted_caps = fs_root_credential_caps(scheduler_current_capabilities());
        file->root=fs_root_retain(fs_root_current());file->root_rights=root_file_rights(flags);
        object = ladybird_fd_object_create(file, &file_ops);
        if (object == NULL) {
            fs_root_release(file->root);
            kfree(file);
            return set_errno(ENOMEM);
        }
        installed_fd = ladybird_fd_install(object, -1);
        wk2_shared_trace("open.end", normalized, (uint32_t)flags, 0u,
                         (uint64_t)(int64_t)installed_fd);
        return installed_fd;
    }

    /* A failed access check on the final component must not turn a missing
     * read-only path into EACCES.  fs_access_path() quite correctly reports
     * "no entry" for that case, but POSIX open() must then continue to the
     * normal ENOENT/ISO lookup below.  Only check read permissions when the
     * path (or directory) is known to exist; the parent-directory policy is
     * enforced by the filesystem access layer for existing entries. */
    /* O_EXCL: creating an existing name fails, whatever its type. */
    if ((flags & O_CREAT) != 0 && (flags & LADYBIRD_O_EXCL) != 0 &&
        path_exists_any(normalized)) {
        return set_errno(EEXIST);
    }

    if (path_requires_admin_for_write(normalized, flags) ||
        (!write_allowed && path_exists_any(normalized) &&
         !path_access_allowed(resolved, 1, 0, 0, 1u)) ||
        (write_allowed && path_exists_any(normalized) && !path_access_allowed(resolved, 1, 1, 0, 1u))) {
        debug_log_puts("open: EACCES ");
        debug_log_puts(normalized);
        debug_log_puts("\n");
        return set_errno(EACCES);
    }

    if ((flags & LINUX_O_DIRECTORY) != 0 || path_is_directory(normalized)) {
        if (!path_is_directory(normalized)) {
            const int path_exists = path_exists_any(normalized);
            debug_log_puts(path_exists ? "open: ENOTDIR " : "open: ENOENT directory ");
            debug_log_puts(normalized);
            debug_log_puts("\n");
            return set_errno(path_exists ? ENOTDIR : ENOENT);
        }
        {
            ladybird_dir_ctx_t *dctx = kmalloc(sizeof(*dctx));
            if (dctx == NULL) return set_errno(ENOMEM);
            memset(dctx, 0, sizeof(*dctx));
            dctx->data_gate=(kernel_gate_t)KERNEL_GATE_INIT;
            copy_string(dctx->path, sizeof(dctx->path), normalized);
            dctx->root=fs_root_retain(fs_root_current());dctx->root_rights=fs_root_current_rights();
            dctx->root_uid=fs_root_credential_uid(user_current_uid());dctx->root_caps=fs_root_credential_caps(scheduler_current_capabilities());
            object = ladybird_fd_object_create(dctx, &dir_ops);
            if (object == NULL) { fs_root_release(dctx->root);kfree(dctx); return set_errno(ENOMEM); }
            if(VEX_COMPAT_TRACE) {
            debug_log_puts("open: dir-fd ");
            debug_log_puts(normalized);
            debug_log_puts("\n");
            }
            return ladybird_fd_install(object, -1);
        }
    }

    /* Runtime libraries are also present as ISO/Limine payloads.  Prefer the
     * sector-backed descriptor for a large read-only payload so opening a WPE
     * child does not duplicate the entire boot module in kernel heap before
     * mmap() can establish its lazy file backing. */
    /* Limine payloads already have an immutable, FS-owned backing. Borrow it
     * rather than reading the same bytes again from the DVD. mmap remains
     * lazy and PRIVATE faults still create independent writable pages. */
    if (!write_allowed && fs_read_boot_module_view(normalized, &boot_data, &boot_size, &boot_content) == 0) {
        data = (void *)boot_data;
        size = boot_size;
        boot_borrowed = 1;
        goto open_memory_file;
    }
    if (!write_allowed &&
        iso9660_get_location(normalized, &iso_lba, &iso_size) == 0 &&
        iso_size > (1u << 20u)) {
        if(VEX_COMPAT_TRACE) {
        debug_log_puts("open: iso-stream(large) ");
        debug_log_puts(normalized);
        debug_log_puts("\n");
        }
        goto open_iso_stream;
    }

    if (!fs_exists(normalized)) {
        if ((flags & O_CREAT) == 0) {
            /* Last resort: the file might be a large iso9660 file that
             * fs_exists missed (e.g. iso9660_exists returned 0 due to a
             * transient disk error).  Try a direct location lookup. */
            if (iso9660_get_location(normalized, &iso_lba, &iso_size) == 0) {
                if(VEX_COMPAT_TRACE) {
                debug_log_puts("open: iso-stream(noexist) ");
                debug_log_puts(normalized);
                debug_log_puts("\n");
                }
                goto open_iso_stream;
            }
            if(VEX_COMPAT_TRACE) {
            debug_log_puts("open: ENOENT ");
            debug_log_puts(normalized);
            debug_log_puts("\n");
            }
            return set_errno(ENOENT);
        }
        /* POSIX: the new file exists, with its requested mode, as soon as
           open() returns, not only once the descriptor is first flushed. */
        if (fs_write(normalized, NULL, 0u) != 0) {
            const int error = *ladybird_errno_location();
            return set_errno(error != 0 ? error : ENOSPC);
        }
        (void)fs_set_mode(normalized, create_mode & 07777u);
        copy_string(resolved, sizeof(resolved), normalized);
    } else if (fs_read_view(normalized,&data,&size, &fs_lease_1) != 0) {
        /* fs_read failed — typically because the file is too large to load
         * into a contiguous physical buffer (e.g. a 100+ MB shared library).
         * Fall back to a lazy streaming fd backed by the CD-ROM. */
        if (iso9660_get_location(normalized, &iso_lba, &iso_size) == 0) {
            if (VEX_COMPAT_TRACE) {
                debug_log_puts("open: iso-stream(fsread-fail) ");
                debug_log_puts(normalized);
                debug_log_puts("\n");
            }
            goto open_iso_stream;
        }
        debug_log_puts("open: EIO(no-iso) ");
        debug_log_puts(normalized);
        debug_log_puts("\n");
        return set_errno(EIO);
    }

    if (!write_allowed && fs_read_boot_module_view(normalized, &boot_data, &boot_size, &boot_content) == 0) {
        data = (void *)boot_data;
        size = boot_size;
        boot_borrowed = 1;
    }

open_memory_file:
    /* ── normal in-memory file path ─────────────────────────────────────── */
    file = kmalloc(sizeof(*file));
    if (file == NULL) {
        return set_errno(ENOMEM);
    }

    memset(file, 0, sizeof(*file));
    file->data_gate=(kernel_gate_t)KERNEL_GATE_INIT;
    copy_string(file->path, sizeof(file->path), resolved);
    file->flags = flags;
    file->write_allowed = write_allowed;
    file->buffer_owned = 1u;
    file->granted_uid = fs_root_credential_uid(user_current_uid());
    file->granted_caps = fs_root_credential_caps(scheduler_current_capabilities());
    file->root=fs_root_retain(fs_root_current());file->root_rights=root_file_rights(flags);

    if ((flags & O_TRUNC) != 0) {
        size = 0u;
        file->dirty = 1u;
    } else if ((flags & O_CREAT) != 0 && !fs_exists(normalized)) {
        /* O_CREAT must materialize an empty regular file even when the
           caller closes it without writing any bytes. */
        file->dirty = 1u;
    }

    if (size > 0u) {
        if (boot_borrowed != 0) {
            file->buffer = (uint8_t *)data;
            file->size = size;
            file->capacity = size;
            file->buffer_owned = 0u;
            file->content=boot_content;boot_content=(ora_view_t){0};
        } else {
            file->buffer = kmalloc(size);
            if (file->buffer == NULL) {
                fs_root_release(file->root);
                kfree(file);
                return set_errno(ENOMEM);
            }
            memcpy(file->buffer, data, size);
            file->size = size;
            file->capacity = size;
        }
    }

    if ((flags & O_APPEND) != 0) {
        file->offset = file->size;
    }

    object = ladybird_fd_object_create(file, &file_ops);
    if (object == NULL) {
        if (file->buffer_owned != 0u) {
            kfree(file->buffer);
        }
        ora_view_release(&file->content);
        fs_root_release(file->root);
        kfree(file);
        return set_errno(ENOMEM);
    }

    object->name_protection=protection;protection=NULL;
    installed_fd = ladybird_fd_install(object, -1);
    wk2_shared_trace("open.end", normalized, (uint32_t)flags,
                     wk2_shared_path(normalized)
                         ? (uint64_t)(fs_exists(normalized) != 0) : 0u,
                     (uint64_t)(int64_t)installed_fd);
    hsts_file_trace("open.end", normalized, (uint32_t)flags,
                    (uint64_t)size, installed_fd);
    return installed_fd;

open_iso_stream:
    {
        /* Create a lazy streaming fd — reads sectors from the CD-ROM on demand
         * instead of loading the whole file into memory at open() time. */
        ladybird_iso_stream_ctx_t *stream = kmalloc(sizeof(*stream));
        if (stream == NULL) {
            return set_errno(ENOMEM);
        }
        stream->lba       = iso_lba;
        stream->file_size = iso_size;
        stream->offset    = 0u;
        stream->root_rights=root_file_rights(flags);
        stream->root=fs_root_retain(fs_root_current());
        memset(&stream->times,0,sizeof(stream->times));
        (void)fs_get_times(normalized,&stream->times);
        if (VEX_COMPAT_TRACE) debug_log_size("iso-fd: size=", iso_size);
        object = ladybird_fd_object_create(stream, &iso_stream_ops);
        if (object == NULL) {
            fs_root_release(stream->root);
            kfree(stream);
            return set_errno(ENOMEM);
        }
        object->name_protection=protection;protection=NULL;
        return ladybird_fd_install(object, -1);
    }
}

int vex_open_mode(const char *path,int flags,uint32_t mode) {return vex_open_mode_impl(path,flags,mode,0);}

int vex_web_resolve_dirfd_path(int dirfd, const char *path, char *buffer, size_t size) {
    char full_path[LADYBIRD_MAX_PATH];
    ladybird_fd_object_t *dir_object;
    ladybird_dir_ctx_t *dctx;
    char normalized[LADYBIRD_MAX_PATH];

    if (path == NULL || buffer == NULL || size == 0u) {
        return set_errno(EINVAL);
    }

    ladybird_posix_init();

    if (path[0] == '/' || dirfd == LADYBIRD_AT_FDCWD) {
        normalize_path(path, normalized);
        copy_string(buffer, size, normalized);
        return 0;
    }

    dir_object = ladybird_fd_acquire(dirfd);
    if (dir_object == NULL) {
        return set_errno(EBADF);
    }
    if (dir_object->ops != &dir_ops || dir_object->ctx == NULL) {
        ladybird_fd_release(dir_object);
        return set_errno(EBADF);
    }
    dctx = (ladybird_dir_ctx_t *)dir_object->ctx;
    path_join(full_path, sizeof(full_path), dctx->path, path);
    ladybird_fd_release(dir_object);

    normalize_path(full_path, normalized);
    copy_string(buffer, size, normalized);
    return 0;
}

typedef struct {ladybird_dir_ctx_t *dir;void *buffer;size_t count;} root_dir_read;
static int root_dir_read_call(const char *path,void *opaque) {
    (void)path;root_dir_read *c=opaque;ladybird_dir_ctx_t *dir=c->dir;
    void *buffer=c->buffer;size_t count=c->count;
    if (!dir->enumerated) {
        int result = dir_collect_child(".", 0, FS_ENTRY_DIRECTORY, dir);
        if (!result) result = dir_collect_child("..", 0, FS_ENTRY_DIRECTORY, dir);
        if (!result) result = fs_list_directory(dir->path, dir_collect_child, dir);
        if (result) {
            dir_free_children(dir);

            return set_errno_ssize(result == -2 ? ENOMEM : EIO);
        }
        dir->cursor = dir->children;
        dir->enumerated = 1u;
    }
    size_t used = 0u;
    while (dir->cursor) {
        ladybird_dir_child_t *child = dir->cursor;
        size_t bytes = (offsetof(ladybird_linux_dirent64_t, d_name) + string_length(child->name) + 1u + 7u) & ~7u;
        if (bytes > count - used) break;
        ladybird_linux_dirent64_t *entry = (void *)((uint8_t *)buffer + used);
        memset(entry, 0, bytes);
        entry->d_ino = child->inode;
        entry->d_off = (int64_t)++dir->position;
        entry->d_reclen = (uint16_t)bytes;
        entry->d_type = child->type;
        copy_string(entry->d_name, bytes - offsetof(ladybird_linux_dirent64_t, d_name), child->name);
        used += bytes;
        dir->cursor = child->next;
    }
    const int too_small = !used && dir->cursor;

    return too_small ? set_errno_ssize(EINVAL) : (ssize_t)used;
}
ssize_t vex_web_getdents64(int fd, void *buffer, size_t count) {
    if(!buffer || count<24u || count>INT32_MAX)return set_errno_ssize(EINVAL);
    ladybird_fd_object_t *object=ladybird_fd_acquire(fd);
    if(!object || object->ops!=&dir_ops || !object->ctx) {
        if(object)ladybird_fd_release(object);return set_errno_ssize(EBADF);
    }
    ladybird_dir_ctx_t *dir=object->ctx;
    kernel_guard_t lock=kernel_guard_enter(&dir->data_gate);
    root_dir_read c={dir,buffer,count};int result;
    if(dir->root) {
        char relative[512];
        result=fs_root_relative(dir->root,dir->path,relative,sizeof(relative))?-1:
            fs_root_invoke(dir->root,dir->root_rights,dir->root_uid,dir->root_caps,relative,root_dir_read_call,&c);
    } else result=root_dir_read_call(dir->path,&c);
    kernel_guard_leave(&lock);ladybird_fd_release(object);return result;
}
int openat(int dirfd, const char *path, int flags) {
    char full_path[LADYBIRD_MAX_PATH];

    if (vex_web_resolve_dirfd_path(dirfd, path, full_path, sizeof(full_path)) != 0) {
        return -1;
    }

    return open(full_path, flags);
}

int close(int fd) {
    ladybird_posix_init();
    const uint32_t owner=ladybird_fd_owner_pid();
    uint64_t flags=fd_inventory_lock();
    ladybird_fd_slot_t *slot=ladybird_fd_find_slot(owner,fd);
    if(!slot){fd_inventory_unlock(flags);return set_errno(EBADF);}
    ladybird_fd_object_t *object=slot->object;*slot=(ladybird_fd_slot_t){0};
    fd_inventory_unlock(flags);
    description_range_closed(owner,object,0);
    if (object && object->ops == &file_ops && object->ctx)
        hsts_file_trace("close", ((ladybird_file_ctx_t *)object->ctx)->path,
                        (uint64_t)(uint32_t)fd, object->refcount, 0);
    if(object && object->ops==&file_ops && object->ctx)
        ladybird_fcntl_drop_path(owner,((ladybird_file_ctx_t *)object->ctx)->path);
    return ladybird_fd_release(object);
}

/* Cursor selection, contents and materialized MAP_SHARED overlay form ONE
   operation. Sampling SEEK_CUR outside the file transaction races another
   reader/writer, even when the read callback itself protects its cursor. */
static ssize_t file_io_transaction(ladybird_fd_object_t *object,void *buffer,
                                   size_t count,off_t offset,int writing,int positional) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    ladybird_file_ctx_t *file=object->ctx;
    KERNEL_GUARD(file->data_gate);
    /* Internal backing fills supply HHDM destinations; user buffers are
       revalidated while unmap/protection changes are excluded. */
    if((uint64_t)(uintptr_t)buffer<UINT64_C(0x800000000000) &&
       !vex_syscall_user_range_valid((uint64_t)(uintptr_t)buffer,count,!writing))
        return set_errno_ssize(EFAULT);
    uint64_t position=positional?(uint64_t)offset:
        (writing && (file->flags&O_APPEND)?file->size:file->offset);
    if(description_range_access(file,position,count,writing))return set_errno_ssize(EAGAIN);
    ssize_t result=writing?
        (positional?file_pwrite_op(file,buffer,count,offset):file_write_op(file,buffer,count)):
        (positional?file_pread_op(file,buffer,count,offset):file_read_op(file,buffer,count));
    if(result>0)process_memory_file_object_cache_copy(object,position,buffer,(size_t)result,writing);
    return result;
}

ssize_t read(int fd, void *buffer, size_t count) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object = vex_fd_borrow_acquire(&operation_ref,fd);
    ssize_t result;

    if (object == NULL || object->ops == NULL || object->ops->read == NULL) {
        return set_errno_ssize(EBADF);
    }
    if(object->ops==&file_ops)
        return file_io_transaction(object,buffer,count,0,0,0);

    off_t position = 0;
    const int cached = object->shared_mapping_pages && object->ops->seek &&
        object->ops->seek(object->ctx, 0, SEEK_CUR, &position) == 0 && position >= 0;
    result = object->ops->read(object->ctx, buffer, count);
    if (object->ops == &file_ops && object->ctx)
        hsts_file_trace("read", ((ladybird_file_ctx_t *)object->ctx)->path,
                        count, 0u, result);
    if (cached && result > 0)
        process_memory_file_object_cache_copy(object, (uint64_t)position, buffer, (size_t)result, 0);
    vex_fd_borrow_release(&operation_ref);
    return result;
}

ssize_t write(int fd, const void *buffer, size_t count) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object = vex_fd_borrow_acquire(&operation_ref,fd);
    ssize_t result;

    if (object == NULL || object->ops == NULL || object->ops->write == NULL) {
        return set_errno_ssize(EBADF);
    }
    if(object->ops==&file_ops)
        return file_io_transaction(object,(void *)buffer,count,0,1,0);

    off_t position = 0;
    const int cached = object->shared_mapping_pages && object->ops->seek &&
        object->ops->seek(object->ctx, 0, SEEK_CUR, &position) == 0 && position >= 0;
    if (cached && object->ops == &file_ops &&
        (((ladybird_file_ctx_t *)object->ctx)->flags & O_APPEND))
        position = (off_t)((ladybird_file_ctx_t *)object->ctx)->size;
    result = object->ops->write(object->ctx, buffer, count);
    if (object->ops == &file_ops && object->ctx)
        hsts_file_trace("write", ((ladybird_file_ctx_t *)object->ctx)->path,
                        count, cached ? (uint64_t)position : 0u, result);
    if (cached && result > 0)
        process_memory_file_object_cache_copy(object, (uint64_t)position, (void *)buffer, (size_t)result, 1);
    vex_fd_borrow_release(&operation_ref);
    return result;
}

ssize_t pread(int fd, void *buffer, size_t count, off_t offset) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object = vex_fd_borrow_acquire(&operation_ref,fd);
    ssize_t result;

    if (offset < 0) {
        if (object != NULL) vex_fd_borrow_release(&operation_ref);
        return set_errno_ssize(EINVAL);
    }
    if (object == NULL) return set_errno_ssize(EBADF);
    if (object->ops == NULL || object->ops->pread == NULL) {
        vex_fd_borrow_release(&operation_ref);
        return set_errno_ssize(ESPIPE);
    }
    if(object->ops==&file_ops)
        return file_io_transaction(object,buffer,count,offset,0,1);

    result = object->ops->pread(object->ctx, buffer, count, offset);
    if (object->ops == &file_ops && object->ctx)
        hsts_file_trace("pread", ((ladybird_file_ctx_t *)object->ctx)->path,
                        (uint64_t)offset, count, result);
    if (result > 0)
        process_memory_file_object_cache_copy(object, (uint64_t)offset,
                                             buffer, (size_t)result, 0);
    vex_fd_borrow_release(&operation_ref);
    return result;
}

/* Positional I/O never moves the shared open-file-description cursor.  This
 * matters for both SQLite's concurrent page reads/writes and file-backed
 * mappings materialized by independent process threads. */
ssize_t pwrite(int fd, const void *buffer, size_t count, off_t offset) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object = vex_fd_borrow_acquire(&operation_ref,fd);
    ssize_t result;

    if (offset < 0) {
        if (object != NULL) vex_fd_borrow_release(&operation_ref);
        return set_errno_ssize(EINVAL);
    }
    if (object == NULL) return set_errno_ssize(EBADF);
    if (object->ops == NULL || object->ops->pwrite == NULL) {
        vex_fd_borrow_release(&operation_ref);
        return set_errno_ssize(ESPIPE);
    }
    if(object->ops==&file_ops)
        return file_io_transaction(object,(void *)buffer,count,offset,1,1);

    result = object->ops->pwrite(object->ctx, buffer, count, offset);
    if (object->ops == &file_ops && object->ctx)
        hsts_file_trace("pwrite", ((ladybird_file_ctx_t *)object->ctx)->path,
                        (uint64_t)offset, count, result);
    if (result > 0)
        process_memory_file_object_cache_copy(object, (uint64_t)offset,
                                             (void *)buffer, (size_t)result, 1);
    vex_fd_borrow_release(&operation_ref);
    return result;
}

/* Keep mapped file objects usable after their numeric FD is closed. */
ssize_t vex_web_fd_pread_object(vex_web_fd_object_t *object, void *buffer,
                                size_t count, off_t offset) {
    ssize_t result;
    if (object == NULL || object->ops == NULL)
        return set_errno_ssize(EBADF);
    if (object->ops->pread == NULL) return set_errno_ssize(ESPIPE);
    if ((buffer == NULL && count != 0u) || offset < 0)
        return set_errno_ssize(EINVAL);
    if(object->ops==&file_ops)
        return file_io_transaction(object,buffer,count,offset,0,1);
    result = object->ops->pread(object->ctx, buffer, count, offset);
    if (result > 0)
        process_memory_file_object_cache_copy(object, (uint64_t)offset,
                                             buffer, (size_t)result, 0);
    return result;
}

off_t lseek(int fd, off_t offset, int whence) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object = vex_fd_borrow_acquire(&operation_ref,fd);
    off_t result = -1;

    if (object == NULL || object->ops == NULL || object->ops->seek == NULL) {
        return (off_t)set_errno(EBADF);
    }

    if (object->ops->seek(object->ctx, offset, whence, &result) != 0) {
        vex_fd_borrow_release(&operation_ref);
        return -1;
    }

    vex_fd_borrow_release(&operation_ref);
    return result;
}

int fsync(int fd) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object = vex_fd_borrow_acquire(&operation_ref,fd);
    int result;

    if (object == NULL || object->ops == NULL || object->ops->fsync == NULL) {
        return set_errno(EBADF);
    }

    result = object->ops->fsync(object->ctx);
    if (object->ops == &file_ops && object->ctx)
        hsts_file_trace("fsync", ((ladybird_file_ctx_t *)object->ctx)->path,
                        object->refcount, 0u, result);
    vex_fd_borrow_release(&operation_ref);
    return result;
}

int stat(const char *path, struct stat *st) {
    char normalized[LADYBIRD_MAX_PATH];
    int r;

    ladybird_posix_init();
    normalize_path(path, normalized);
    rewrite_multiarch_path(normalized);
    if(VEX_COMPAT_TRACE) {
        debug_log_puts("stat: ");
        debug_log_puts(normalized);
        debug_log_puts("\n");
    }
    r = fill_stat_for_path(normalized, st);
    if (VEX_COMPAT_TRACE && r != 0) { debug_log_puts("stat: ENOENT\n"); }
    return r;
}

int lstat(const char *path, struct stat *st) {
    char normalized[LADYBIRD_MAX_PATH];
    fs_metadata_t metadata;

    if (st == NULL) {
        return set_errno(EINVAL);
    }

    ladybird_posix_init();
    normalize_path(path, normalized);
    rewrite_multiarch_path(normalized);
    if (fs_get_metadata(normalized, &metadata, 0u) != 0) {
        return stat(path, st);
    }
    if (metadata.type != FS_ENTRY_SYMLINK) return stat(path, st);

    memset(st, 0, sizeof(*st));
    st->st_blksize = 4096u;
    st->st_uid = metadata.uid;
    st->st_gid = metadata.gid;
    st->st_mode = (metadata.type == FS_ENTRY_DIRECTORY ? S_IFDIR :
                   metadata.type == FS_ENTRY_SYMLINK ? S_IFLNK : S_IFREG) | metadata.mode;
    if (metadata.type == FS_ENTRY_SYMLINK) {
        char target[LADYBIRD_MAX_PATH];
        int count = fs_readlink(normalized, target, sizeof(target));
        st->st_size = count > 0 ? (uint64_t)count : 0u;
    }
    return 0;
}

/* Stable per-file identifier for Linux stat emulation.
 * ld.so deduplicates shared libraries by (st_dev, st_ino); returning the
 * same inode for every fd makes it treat all libraries as one single file. */
uint64_t ladybird_fd_inode_hint(int fd) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);
    uint64_t hint = 0u;

    if (object == NULL) {
        return 0u;
    }
    if (object->ctx != NULL && object->ops == &file_ops) {
        const ladybird_file_ctx_t *file = object->ctx;
        const char *p = file->path;
        hint = 1469598103934665603ull; /* FNV-1a 64 offset basis */
        while (*p != '\0') {
            hint ^= (uint64_t)(unsigned char)*p++;
            hint *= 1099511628211ull;
        }
    } else if (object->ctx != NULL && object->ops == &iso_stream_ops) {
        const ladybird_iso_stream_ctx_t *stream = object->ctx;
        hint = 0x4953414000000000ull | (uint64_t)stream->lba;
    }
    ladybird_fd_release(object);
    return hint;
}

static int root_file_times_call(const char *path,void *out) {return fs_get_times(path,out);}
int vex_web_fd_times(int fd,void *times) {
    ladybird_fd_object_t *object=ladybird_fd_acquire(fd);
    int result=-1;
    if(object && object->ctx && object->ops==&file_ops) {
        ladybird_file_ctx_t *f=object->ctx;
        if(f->root) {
            char relative[512];
            if(!fs_root_relative(f->root,f->path,relative,sizeof(relative)))
                result=fs_root_invoke(f->root,f->root_rights,f->granted_uid,f->granted_caps,relative,root_file_times_call,times);
        } else result=fs_get_times(f->path,times);
    }
    if(object && object->ctx && object->ops==&iso_stream_ops){
        *(vex_file_times_t *)times=((ladybird_iso_stream_ctx_t *)object->ctx)->times;result=0;
    }
    if(object)ladybird_fd_release(object);
    return result;
}

int fstat(int fd, struct stat *st) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);
    int result;

    if (object == NULL || object->ops == NULL || object->ops->fstat == NULL) {
        return set_errno(EBADF);
    }

    result = object->ops->fstat(object->ctx, st);
    ladybird_fd_release(object);
    return result;
}

/* VEX_SYS_STAT_EX: the legacy record plus identity and timestamps. The
   inode is the FNV-1a hash of the normalized path, the same identity that
   ladybird_fd_inode_hint() reports for an open regular file. */
static uint64_t stat_ex_path_inode(const char *normalized) {
    uint64_t hash = 1469598103934665603ull;
    for (const char *p = normalized; *p != '\0'; ++p) {
        hash ^= (uint64_t)(unsigned char)*p;
        hash *= 1099511628211ull;
    }
    return hash != 0u ? hash : 1u;
}

static void stat_ex_fill(vex_stat_ex_t *out, const struct stat *st, uint64_t inode,
                         const vex_file_times_t *times) {
    memset(out, 0, sizeof(*out));
    out->dev = 1u;
    out->ino = inode != 0u ? inode : 1u;
    out->mode = st->st_mode != 0u ? st->st_mode : (S_IFREG | FS_MODE_DEFAULT_FILE);
    out->nlink = 1u;
    out->uid = st->st_uid;
    out->gid = st->st_gid;
    out->size = (int64_t)st->st_size;
    out->blksize = st->st_blksize != 0u ? (int64_t)st->st_blksize : 4096;
    out->blocks = st->st_blocks != 0u ? (int64_t)st->st_blocks
                                      : (int64_t)((st->st_size + 511u) / 512u);
    if (times == NULL) return;
    if (times->valid & VEX_FILE_ACCESS_TIME) {
        out->atime_sec = times->seconds[0];
        out->atime_nsec = times->nanoseconds[0];
        out->valid |= VEX_STAT_EX_HAS_ATIME;
    }
    if (times->valid & VEX_FILE_MODIFY_TIME) {
        out->mtime_sec = times->seconds[1];
        out->mtime_nsec = times->nanoseconds[1];
        out->valid |= VEX_STAT_EX_HAS_MTIME;
    }
    if (times->valid & VEX_FILE_CHANGE_TIME) {
        out->ctime_sec = times->seconds[2];
        out->ctime_nsec = times->nanoseconds[2];
        out->valid |= VEX_STAT_EX_HAS_CTIME;
    }
}

static int stat_ex_error(void) {
    const int error = *ladybird_errno_location();
    return -(error != 0 ? error : ENOENT);
}

int vex_stat_ex_path(const char *path, int follow, vex_stat_ex_t *out) {
    char normalized[LADYBIRD_MAX_PATH];
    struct stat st;
    vex_file_times_t times = {0};

    if (path == NULL || out == NULL) return -EINVAL;
    if (path[0] == '\0') return -ENOENT;
    *ladybird_errno_location() = 0;
    if ((follow ? stat(path, &st) : lstat(path, &st)) != 0) return stat_ex_error();
    ladybird_posix_init();
    normalize_path(path, normalized);
    rewrite_multiarch_path(normalized);
    stat_ex_fill(out, &st, stat_ex_path_inode(normalized),
                 fs_get_times(normalized, &times) == 0 ? &times : NULL);
    return 0;
}

int vex_stat_ex_fd(int fd, vex_stat_ex_t *out) {
    struct stat st;
    vex_file_times_t times = {0};

    if (out == NULL) return -EINVAL;
    *ladybird_errno_location() = 0;
    if (fstat(fd, &st) != 0) return stat_ex_error();
    stat_ex_fill(out, &st, ladybird_fd_inode_hint(fd),
                 vex_web_fd_times(fd, &times) == 0 ? &times : NULL);
    return 0;
}
typedef struct {ladybird_file_ctx_t *file;vex_stat_ex_t *out;} object_information;
static int object_information_call(const char *path,void *opaque){
    object_information *q=opaque;struct stat st;vex_file_times_t times;
    if(file_fstat_op(q->file,&st))return -*ladybird_errno_location();
    stat_ex_fill(q->out,&st,q->file->identity,fs_get_times(path,&times)?NULL:&times);return 0;
}
int vex_stat_ex_object(int fd,vex_stat_ex_t *out){
    VEX_FD_BORROW(borrow);ladybird_fd_object_t *o=vex_fd_borrow_acquire(&borrow,fd);
    if(!o || !o->ctx)return -EBADF;
    if(o->ops==&file_ops){
        ladybird_file_ctx_t *f=o->ctx;KERNEL_GUARD(f->data_gate);
        if(!f->identity)return -ENOTSUP;object_information q={f,out};
        if(f->root){char relative[512];if(fs_root_relative(f->root,f->path,relative,sizeof(relative)))return -EACCES;
            return fs_root_invoke(f->root,f->root_rights,f->granted_uid,f->granted_caps,relative,object_information_call,&q);}
        return object_information_call(f->path,&q);
    }
    if(o->ops==&iso_stream_ops){ladybird_iso_stream_ctx_t *stream=o->ctx;struct stat st;
        if(iso_stream_fstat_op(stream,&st))return -*ladybird_errno_location();
        stat_ex_fill(out,&st,(1ull<<63)|stream->lba,&stream->times);return 0;
    }return -ENOTSUP;
}

/* The VexFS path behind a regular file descriptor, after writing out
   buffered data so that later metadata changes are not overtaken by it. */
static int fd_flushed_path(int fd, char path[LADYBIRD_MAX_PATH]) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);
    int result = 0;

    if (object == NULL) return -EBADF;
    if (object->ctx == NULL || object->ops != &file_ops) {
        result = object->ops == &iso_stream_ops ? -EROFS : -EINVAL;
    } else {
        ladybird_file_ctx_t *file = object->ctx;
        kernel_guard_t memory __attribute__((cleanup(process_memory_end_update))) =
            process_memory_begin_update();
        KERNEL_GUARD(file->data_gate);
        if (file->unlinked) {
            result = -ENOENT;
        } else if (flush_file(file) != 0) {
            result = -EIO;
        } else {
            memcpy(path, file->path, LADYBIRD_MAX_PATH);
        }
    }
    ladybird_fd_release(object);
    return result;
}

/* A failed metadata change: a file the caller does not own, a missing
   file, or one VexFS does not own (read-only ISO contents). */
static int metadata_error(const char *path, int result) {
    struct stat st;
    if (result == FS_NOT_OWNER) return -EPERM;
    if (lstat(path, &st) != 0) return -ENOENT;
    return -EROFS;
}

int vex_set_times(const vex_set_times_req_t *req) {
    char path[LADYBIRD_MAX_PATH];
    vex_file_times_t times = {0};
    int follow = 1;

    if (req == NULL || req->reserved != 0u || (req->flags & ~VEX_SET_TIMES_NOFOLLOW) != 0u ||
        (req->valid & ~(VEX_STAT_EX_HAS_ATIME | VEX_STAT_EX_HAS_MTIME)) != 0u)
        return -EINVAL;
    for (uint32_t i = 0u; i < 2u; ++i) {
        if ((req->valid & (1u << i)) && req->nanoseconds[i] >= 1000000000u) return -EINVAL;
        times.seconds[i] = req->seconds[i];
        times.nanoseconds[i] = req->nanoseconds[i];
    }
    times.valid = req->valid;
    if (req->path == NULL) {
        const int result = fd_flushed_path(req->fd, path);
        if (result != 0) return result;
    } else {
        ladybird_posix_init();
        if (req->path[0] == '\0') return -ENOENT;
        normalize_path(req->path, path);
        rewrite_multiarch_path(path);
        follow = (req->flags & VEX_SET_TIMES_NOFOLLOW) == 0u;
    }
    const int result = fs_set_times(path, &times, follow);
    return result == 0 ? 0 : metadata_error(path, result);
}

int vex_fchmod(int fd, uint32_t mode) {
    char path[LADYBIRD_MAX_PATH];
    int result = fd_flushed_path(fd, path);
    if (result != 0) return result;
    result = fs_set_mode(path, mode & 07777u);
    return result == 0 ? 0 : metadata_error(path, result);
}

int access(const char *path, int mode) {
    char normalized[LADYBIRD_MAX_PATH];

    ladybird_posix_init();
    normalize_path(path, normalized);
    rewrite_multiarch_path(normalized);

    if (!path_exists_any(normalized)) {
        return set_errno(ENOENT);
    }
    if (!path_access_allowed(normalized, (mode & R_OK) != 0, (mode & W_OK) != 0, (mode & X_OK) != 0, 1u)) {
        return set_errno(EACCES);
    }

    return 0;
}

int unlink(const char *path) {
    kernel_guard_t namespace __attribute__((cleanup(fs_namespace_end)))=fs_namespace_begin();
    char normalized[LADYBIRD_MAX_PATH];

    ladybird_posix_init();
    normalize_path(path, normalized);

    if(!fs_namespace_mutation_allowed(normalized))return set_errno(EACCES);

    if (wk2_shared_path(normalized)) {
        int found = 0;
        uint64_t inventory_flags=fd_inventory_lock();
        for(ladybird_process_fd_table_t *table=ladybird_fd_tables;table;table=table->next)
        for (int i = 0; i < LADYBIRD_MAX_FDS; ++i) {
            ladybird_fd_object_t *object = table->slots[i].object;
            if (object != NULL && object->ops == &file_ops && object->ctx != NULL) {
                ladybird_file_ctx_t *file = (ladybird_file_ctx_t *)object->ctx;
                if (file->volatile_shm != 0u && string_equals(file->path, normalized)) {
                    file->unlinked = 1u;
                    found = 1;
                }
            }
        }
        fd_inventory_unlock(inventory_flags);
        if (found) {
            wk2_shared_trace("unlink.end", normalized, 0u, 0u, 0u);
            return 0;
        }
        /* Remove stale entries left by earlier VSL versions which persisted
         * /dev/shm objects as ordinary VexFS files. */
        if (fs_exists(normalized)) {
            if (fs_delete(normalized) != 0) {
                wk2_shared_trace("unlink.fail", normalized, 0u, 0u,
                                 (uint64_t)(int64_t)-EIO);
                return set_errno(EIO);
            }
            wk2_shared_trace("unlink.end", normalized, 0u, 0u, 0u);
            return 0;
        }
        wk2_shared_trace("unlink.fail", normalized, 0u, 0u,
                         (uint64_t)(int64_t)-ENOENT);
        return set_errno(ENOENT);
    }

    if (!fs_exists(normalized)) {
        return set_errno(path_is_directory(normalized) ? EISDIR : ENOENT);
    }

    if (fs_delete(normalized) != 0) {
        return set_errno(EIO);
    }

    return 0;
}

unsigned vex_fd_usage_snapshot(vex_fd_usage_t *out, unsigned capacity) {
    unsigned count = 0u;
    uint64_t inventory_flags;

    if (out == NULL) {
        return 0u;
    }
    inventory_flags = fd_inventory_lock();
    for (ladybird_process_fd_table_t *table = ladybird_fd_tables;
         table != NULL && count < capacity; table = table->next) {
        unsigned open_descriptors = 0u;

        for (int i = 0; i < LADYBIRD_MAX_FDS; ++i) {
            if (table->slots[i].object != NULL) {
                ++open_descriptors;
            }
        }
        out[count].pid = table->owner_pid;
        out[count].open_descriptors = open_descriptors;
        ++count;
    }
    fd_inventory_unlock(inventory_flags);
    return count;
}

int rename(const char *old_path, const char *new_path) {
    kernel_guard_t namespace __attribute__((cleanup(fs_namespace_end)))=fs_namespace_begin();
    char old_normalized[LADYBIRD_MAX_PATH];
    char new_normalized[LADYBIRD_MAX_PATH];

    ladybird_posix_init();
    normalize_path(old_path, old_normalized);
    normalize_path(new_path, new_normalized);
    if(!fs_namespace_mutation_allowed(old_normalized) || !fs_namespace_mutation_allowed(new_normalized))return set_errno(EACCES);

    if (path_is_directory(old_normalized)) {
        const size_t old_length = string_length(old_normalized);
        if (string_equals(old_normalized, "/")) {
            return set_errno(EBUSY);
        }
        if (string_equals(old_normalized, new_normalized)) {
            return 0;
        }
        if (strncmp(new_normalized, old_normalized, old_length) == 0 &&
            new_normalized[old_length] == '/') {
            return set_errno(EINVAL); /* into itself */
        }
        if (path_is_directory(new_normalized)) {
            /* An empty directory at the new name is replaced. */
            if (rmdir(new_normalized) != 0) {
                return -1; /* ENOTEMPTY, EROFS... */
            }
        } else if (path_exists_any(new_normalized)) {
            return set_errno(ENOTDIR);
        }
        if (fs_move(old_normalized, new_normalized) != 0) {
            return set_errno(path_exists_any(old_normalized) ? EACCES : ENOENT);
        }
        return 0;
    }
    if (path_is_directory(new_normalized)) {
        return set_errno(EISDIR);
    }

    if (!path_exists_any(old_normalized)) {
        return set_errno(ENOENT);
    }
    /* POSIX: an existing regular file at the new name is replaced. Programs
     * such as GIO rely on it to publish a finished temporary file. */
    if (fs_move_replace(old_normalized, new_normalized) != 0) {
        return set_errno(EIO);
    }

    return 0;
}

int mkdir(const char *path, uint32_t mode) {
    char normalized[LADYBIRD_MAX_PATH];
    char marker[LADYBIRD_MAX_PATH];
    char parent[LADYBIRD_MAX_PATH];
    uint8_t marker_byte = 1u;

    ladybird_posix_init();
    normalize_path(path, normalized);

    if (path_exists_any(normalized)) {
        return set_errno(EEXIST);
    }

    if (!path_parent(parent, normalized) || !path_is_directory(parent)) {
        return set_errno(ENOENT);
    }

    directory_marker_path(normalized, marker);
    if (fs_write(marker, &marker_byte, sizeof(marker_byte)) != 0) {
        return set_errno(EIO);
    }
    if (fs_set_mode(marker, (mode & 0777u) != 0u
                                ? (mode & 0777u) : FS_MODE_DEFAULT_DIR) != 0) {
        (void)fs_delete(marker);
        return set_errno(EIO);
    }

    return 0;
}

static int directory_child_visitor(const char *name, size_t size, uint8_t type, void *context) {
    (void)size;
    (void)type;
    if (string_equals(name, LADYBIRD_DIR_MARKER)) return 0;
    *(int *)context = 1;
    return 1; /* one child is enough */
}

/* POSIX rmdir: only an empty directory is removed. */
int rmdir(const char *path) {
    kernel_guard_t namespace __attribute__((cleanup(fs_namespace_end)))=fs_namespace_begin();
    char normalized[LADYBIRD_MAX_PATH];
    char marker[LADYBIRD_MAX_PATH];
    int has_child = 0;

    ladybird_posix_init();
    normalize_path(path, normalized);
    if(!fs_namespace_mutation_allowed(normalized))return set_errno(EACCES);

    if (string_equals(normalized, "/")) {
        return set_errno(EBUSY);
    }
    if (!path_is_directory(normalized)) {
        return set_errno(path_exists_any(normalized) ? ENOTDIR : ENOENT);
    }
    (void)fs_list_directory(normalized, directory_child_visitor, &has_child);
    if (has_child) {
        return set_errno(ENOTEMPTY);
    }
    directory_marker_path(normalized, marker);
    if (fs_exists(marker) && fs_delete(marker) != 0) {
        return set_errno(EIO);
    }
    /* A directory that only exists on the boot medium cannot be removed. */
    if (path_is_directory(normalized)) {
        return set_errno(EROFS);
    }
    return 0;
}

int chdir(const char *path) {
    char normalized[LADYBIRD_MAX_PATH];

    ladybird_posix_init();
    normalize_path(path, normalized);

    if (!path_is_directory(normalized)) {
        return set_errno(ENOENT);
    }

    copy_string(ladybird_cwd, sizeof(ladybird_cwd), normalized);
    return 0;
}

char *getcwd(char *buffer, size_t size) {
    ladybird_posix_init();
    if (buffer == NULL || size == 0u) {
        ladybird_errno_value = EINVAL;
        return NULL;
    }

    if (string_length(ladybird_cwd) + 1u > size) {
        ladybird_errno_value = ENOMEM;
        return NULL;
    }

    copy_string(buffer, size, ladybird_cwd);
    return buffer;
}

int getpid(void) {
    return (int)scheduler_current_pid();
}

int getppid(void) {
    return (int)scheduler_current_ppid();
}

int getuid(void) {
    return (int)user_current_uid();
}

int geteuid(void) {
    return scheduler_current_capabilities() != 0u ? 0 : (int)user_current_uid();
}

int isatty(int fd) {
    VEX_FD_BORROW(ref);ladybird_fd_object_t *object=vex_fd_borrow_acquire(&ref,fd);
    if(!object)return set_errno(EBADF)==0;
    if(object->ops==&console_ops)return 1;
    *ladybird_errno_location()=ENOTTY;return 0;
}

int ioctl(int fd, unsigned long request, void *arg) {
    (void)fd;
    (void)request;
    (void)arg;
    return set_errno(ENOTSUP);
}

#define LADYBIRD_F_GETLK   5
#define LADYBIRD_F_SETLK   6
#define LADYBIRD_F_SETLKW  7
#define LADYBIRD_F_RDLCK   0
#define LADYBIRD_F_WRLCK   1
#define LADYBIRD_F_UNLCK   2

typedef struct ladybird_fcntl_range {
    uint64_t start;
    uint64_t end;
    uint8_t end_infinite;
} ladybird_fcntl_range_t;

static uint64_t ladybird_fcntl_lock_acquire(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    while (__atomic_exchange_n(&ladybird_fcntl_lock_word, 1u,
                               __ATOMIC_ACQUIRE) != 0u) {
        smp_relax();
    }
    return flags;
}

static void ladybird_fcntl_lock_release(uint64_t flags) {
    __atomic_store_n(&ladybird_fcntl_lock_word, 0u, __ATOMIC_RELEASE);
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}

static int ladybird_fcntl_ranges_overlap(uint64_t a_start, uint64_t a_end,
                                         uint8_t a_infinite,
                                         uint64_t b_start, uint64_t b_end,
                                         uint8_t b_infinite) {
    const int a_before_b_end = b_infinite || a_start < b_end;
    const int b_before_a_end = a_infinite || b_start < a_end;
    return a_before_b_end && b_before_a_end;
}

static int ladybird_fcntl_lock_conflicts(const ladybird_fcntl_lock_t *held,
                                         uint32_t pid, uint8_t type,
                                         const ladybird_fcntl_range_t *range) {
    if (!held->used || held->mandatory || held->pid == pid ||
        !ladybird_fcntl_ranges_overlap(held->start, held->end,
                                       held->end_infinite, range->start,
                                       range->end, range->end_infinite)) {
        return 0;
    }
    return type == LADYBIRD_F_WRLCK || held->type == LADYBIRD_F_WRLCK;
}

static int ladybird_fcntl_find_conflict_locked(const char *path, uint32_t pid,
                                                uint8_t type,
                                                const ladybird_fcntl_range_t *range) {
    for (uint32_t i = 0u; i < LADYBIRD_FCNTL_LOCK_MAX; ++i) {
        ladybird_fcntl_lock_t *held = &ladybird_fcntl_locks[i];
        if (held->used && string_equals(held->path, path) &&
            ladybird_fcntl_lock_conflicts(held, pid, type, range)) {
            return (int)i;
        }
    }
    return -1;
}

static void ladybird_fcntl_wake_path_locked(const char *path) {
    for (uint32_t i = 0u; i < LADYBIRD_FCNTL_WAITER_MAX; ++i) {
        ladybird_fcntl_waiter_t *waiter = &ladybird_fcntl_waiters[i];
        if (waiter->used && (waiter->mandatory || string_equals(waiter->path, path))) {
            __atomic_store_n(&waiter->woken,1u,__ATOMIC_RELEASE);
            process_wake(waiter->tid);
        }
    }
}

static void ladybird_fcntl_prune_waiters_locked(void) {
    for (uint32_t i = 0u; i < LADYBIRD_FCNTL_WAITER_MAX; ++i) {
        ladybird_fcntl_waiter_t *waiter = &ladybird_fcntl_waiters[i];
        if (waiter->used && !scheduler_process_alive(waiter->tid)) {
            memset(waiter, 0, sizeof(*waiter));
        }
    }
}

static int ladybird_fcntl_set_lock_locked(const char *path, uint32_t pid,
                                          uint8_t type,
                                          const ladybird_fcntl_range_t *range) {
    int split_slot = -1;
    memcpy(ladybird_fcntl_lock_backup, ladybird_fcntl_locks,
           sizeof(ladybird_fcntl_locks));

    /* Reserve a slot before changing anything if the new range splits an
     * existing process lock into a left and right remainder. */
    if (type != LADYBIRD_F_UNLCK && !range->end_infinite) {
        for (uint32_t i = 0u; i < LADYBIRD_FCNTL_LOCK_MAX; ++i) {
            ladybird_fcntl_lock_t *held = &ladybird_fcntl_locks[i];
            if (held->used && !held->mandatory && held->pid == pid && string_equals(held->path, path) &&
                held->start < range->start &&
                (held->end_infinite || held->end > range->end) &&
                ladybird_fcntl_ranges_overlap(held->start, held->end,
                                             held->end_infinite, range->start,
                                             range->end, range->end_infinite)) {
                for (uint32_t j = 0u; j < LADYBIRD_FCNTL_LOCK_MAX; ++j) {
                    if (!ladybird_fcntl_locks[j].used) { split_slot = (int)j; break; }
                }
                if (split_slot < 0) return -ENOLCK;
                break;
            }
        }
    }

    for (uint32_t i = 0u; i < LADYBIRD_FCNTL_LOCK_MAX; ++i) {
        ladybird_fcntl_lock_t *held = &ladybird_fcntl_locks[i];
        if (!held->used || held->mandatory || held->pid != pid || !string_equals(held->path, path) ||
            !ladybird_fcntl_ranges_overlap(held->start, held->end,
                                           held->end_infinite, range->start,
                                           range->end, range->end_infinite)) {
            continue;
        }

        const int has_left = held->start < range->start;
        const int has_right = !range->end_infinite &&
            (held->end_infinite || held->end > range->end);
        if (has_left && has_right) {
            if (split_slot < 0) {
                for (uint32_t j = 0u; j < LADYBIRD_FCNTL_LOCK_MAX; ++j) {
                    if (!ladybird_fcntl_locks[j].used) { split_slot = (int)j; break; }
                }
            }
            if (split_slot < 0) {
                memcpy(ladybird_fcntl_locks, ladybird_fcntl_lock_backup,
                       sizeof(ladybird_fcntl_locks));
                return -ENOLCK;
            }
            ladybird_fcntl_locks[split_slot] = *held;
            ladybird_fcntl_locks[split_slot].start = range->end;
            held->end = range->start;
            held->end_infinite = 0u;
            split_slot = -1;
        } else if (has_left) {
            held->end = range->start;
            held->end_infinite = 0u;
        } else if (has_right) {
            held->start = range->end;
        } else {
            memset(held, 0, sizeof(*held));
        }
    }

    if (type != LADYBIRD_F_UNLCK) {
        int free_slot = -1;
        for (uint32_t i = 0u; i < LADYBIRD_FCNTL_LOCK_MAX; ++i) {
            if (!ladybird_fcntl_locks[i].used) { free_slot = (int)i; break; }
        }
        if (free_slot < 0) {
            memcpy(ladybird_fcntl_locks, ladybird_fcntl_lock_backup,
                   sizeof(ladybird_fcntl_locks));
            return -ENOLCK;
        }
        ladybird_fcntl_lock_t *created = &ladybird_fcntl_locks[free_slot];
        memset(created, 0, sizeof(*created));
        created->used = 1u;
        created->pid = pid;
        created->type = type;
        created->start = range->start;
        created->end = range->end;
        created->end_infinite = range->end_infinite;
        copy_string(created->path, sizeof(created->path), path);
    }

    ladybird_fcntl_wake_path_locked(path);
    return 0;
}

static int ladybird_fcntl_make_range(const ladybird_file_ctx_t *file,
                                      const vex_linux_flock_t *request,
                                      ladybird_fcntl_range_t *range) {
    int64_t base = 0;
    int64_t absolute;
    if (request->whence == SEEK_CUR) {
        if (file->offset > (size_t)INT64_MAX) return -EINVAL;
        base = (int64_t)file->offset;
    } else if (request->whence == SEEK_END) {
        if (file->size > (size_t)INT64_MAX) return -EINVAL;
        base = (int64_t)file->size;
    } else if (request->whence != SEEK_SET) {
        return -EINVAL;
    }
    if ((request->start > 0 && base > INT64_MAX - request->start) ||
        (request->start < 0 && base < INT64_MIN - request->start)) return -EINVAL;
    absolute = base + request->start;
    if (request->length < 0) {
        if (request->length == INT64_MIN || absolute < -request->length || absolute < 0)
            return -EINVAL;
        range->start = (uint64_t)(absolute + request->length);
        range->end = (uint64_t)absolute;
        range->end_infinite = 0u;
    } else {
        if (absolute < 0) return -EINVAL;
        range->start = (uint64_t)absolute;
        if (request->length == 0) {
            range->end = UINT64_MAX;
            range->end_infinite = 1u;
        } else {
            if (absolute > INT64_MAX - request->length) return -EINVAL;
            range->end = (uint64_t)(absolute + request->length);
            range->end_infinite = 0u;
        }
    }
    return 0;
}

static int fcntl_lock_impl(int fd,int command,vex_linux_flock_t request,vex_linux_flock_t *user_lock) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fcntl_range_t range;
    ladybird_fd_object_t *object;
    ladybird_file_ctx_t *file;
    uint32_t pid = scheduler_current_process_id();
    uint32_t tid = scheduler_current_tid();
    ladybird_fcntl_waiter_t *waiter = NULL;
    int result = 0;

    if (command != LADYBIRD_F_GETLK && command != LADYBIRD_F_SETLK &&
        command != LADYBIRD_F_SETLKW) return set_errno(ENOTSUP);
    if (request.type != LADYBIRD_F_RDLCK && request.type != LADYBIRD_F_WRLCK &&
        (command == LADYBIRD_F_GETLK || request.type != LADYBIRD_F_UNLCK))
        return set_errno(EINVAL);

    object = vex_fd_borrow_acquire(&operation_ref,fd);
    if (object == NULL) return set_errno(EBADF);
    if (object->ops != &file_ops || object->ctx == NULL) {
        vex_fd_borrow_release(&operation_ref);
        return set_errno(EBADF);
    }
    file = (ladybird_file_ctx_t *)object->ctx;
    hsts_file_trace("fcntl.begin", file->path, (uint32_t)command,
                    ((uint64_t)(uint32_t)request.type << 32u) |
                        (uint32_t)request.whence,
                    request.start);
    if ((request.type == LADYBIRD_F_RDLCK &&
         (file->flags & O_ACCMODE) == O_WRONLY) ||
        (request.type == LADYBIRD_F_WRLCK &&
         ((file->flags & O_ACCMODE) == O_RDONLY || file->write_allowed == 0u))) {
        vex_fd_borrow_release(&operation_ref);
        return set_errno(EBADF);
    }
    if (ladybird_fcntl_make_range(file, &request, &range) != 0) {
        vex_fd_borrow_release(&operation_ref);
        return set_errno(EINVAL);
    }

    if (command == LADYBIRD_F_GETLK) {
        const uint64_t flags = ladybird_fcntl_lock_acquire();
        const int conflict = ladybird_fcntl_find_conflict_locked(
            file->path, pid, (uint8_t)request.type, &range);
        if (conflict < 0) {
            request.type = LADYBIRD_F_UNLCK;
            request.pid = 0;
        } else {
            const ladybird_fcntl_lock_t *held = &ladybird_fcntl_locks[conflict];
            request.type = held->type;
            request.whence = SEEK_SET;
            request.start = (int64_t)held->start;
            request.length = held->end_infinite ? 0 : (int64_t)(held->end - held->start);
            request.pid = (int32_t)held->pid;
        }
        ladybird_fcntl_lock_release(flags);
        if(vex_copy_to_user((uintptr_t)user_lock,&request,sizeof(request)))return set_errno(EFAULT);
        hsts_file_trace("fcntl.getlk", file->path, (uint32_t)command,
                        ((uint64_t)(uint32_t)request.type << 32u) |
                            (uint32_t)request.whence,
                        request.start);
        vex_fd_borrow_release(&operation_ref);
        return 0;
    }

    for (;;) {
        const uint64_t flags = ladybird_fcntl_lock_acquire();
        ladybird_fcntl_prune_waiters_locked();
        const int conflict = request.type == LADYBIRD_F_UNLCK ? -1 :
            ladybird_fcntl_find_conflict_locked(file->path, pid,
                                                (uint8_t)request.type, &range);
        if (conflict < 0) {
            if (waiter != NULL) memset(waiter, 0, sizeof(*waiter));
            result = ladybird_fcntl_set_lock_locked(file->path, pid,
                                                    (uint8_t)request.type, &range);
            ladybird_fcntl_lock_release(flags);
            break;
        }
        if (command != LADYBIRD_F_SETLKW) {
            ladybird_fcntl_lock_release(flags);
            result = -EAGAIN;
            break;
        }
        if (waiter == NULL) {
            for (uint32_t i = 0u; i < LADYBIRD_FCNTL_WAITER_MAX; ++i) {
                if (!ladybird_fcntl_waiters[i].used) {
                    waiter = &ladybird_fcntl_waiters[i];
                    memset(waiter, 0, sizeof(*waiter));
                    waiter->used = 1u;
                    waiter->pid = pid;
                    waiter->tid = tid;
                    copy_string(waiter->path, sizeof(waiter->path), file->path);
                    break;
                }
            }
            if (waiter == NULL) {
                ladybird_fcntl_lock_release(flags);
                result = -ENOLCK;
                break;
            }
        } else {
            waiter->woken = 0u;
        }
        ladybird_fcntl_lock_release(flags);
        process_wait_event(&waiter->woken);
        if (!scheduler_process_alive(tid)) {
            const uint64_t cleanup_flags = ladybird_fcntl_lock_acquire();
            memset(waiter, 0, sizeof(*waiter));
            ladybird_fcntl_lock_release(cleanup_flags);
            result = -EINTR;
            break;
        }
    }
    hsts_file_trace("fcntl.end", file->path, (uint32_t)command,
                    ((uint64_t)(uint32_t)request.type << 32u) |
                        (uint32_t)request.whence,
                    result);
    vex_fd_borrow_release(&operation_ref);
    if (result == -EAGAIN) return set_errno(EAGAIN);
    if (result == -ENOLCK) return set_errno(ENOLCK);
    if (result == -EINTR) return set_errno(EINTR);
    if (result != 0) return set_errno(EINVAL);
    return 0;
}

int vex_linux_fcntl_lock(int fd,int command,vex_linux_flock_t *user_lock) {
    vex_linux_flock_t request;
    if(!user_lock || vex_copy_from_user(&request,(uintptr_t)user_lock,sizeof(request)))return set_errno(EFAULT);
    return fcntl_lock_impl(fd,command,request,user_lock);
}
int vex_fd_group_range(int fd,unsigned mode,uint64_t offset,uint64_t length,int wait) {
    if(mode>2 || offset>INT64_MAX || length>INT64_MAX)return set_errno(EINVAL);
    vex_linux_flock_t request={0};request.type=mode==0?LADYBIRD_F_UNLCK:(mode==1?LADYBIRD_F_RDLCK:LADYBIRD_F_WRLCK);
    request.whence=SEEK_SET;request.start=(int64_t)offset;request.length=(int64_t)length;
    return fcntl_lock_impl(fd,wait?LADYBIRD_F_SETLKW:LADYBIRD_F_SETLK,request,NULL);
}

static void ladybird_fcntl_drop_path(uint32_t pid, const char *path) {
    if (pid == 0u || path == NULL) return;
    const uint64_t flags = ladybird_fcntl_lock_acquire();
    for (uint32_t i = 0u; i < LADYBIRD_FCNTL_LOCK_MAX; ++i) {
        ladybird_fcntl_lock_t *held = &ladybird_fcntl_locks[i];
        if (held->used && !held->mandatory && held->pid == pid && string_equals(held->path, path))
            memset(held, 0, sizeof(*held));
    }
    ladybird_fcntl_wake_path_locked(path);
    ladybird_fcntl_lock_release(flags);
}

static void ladybird_fcntl_drop_process(uint32_t pid) {
    if (pid == 0u) return;
    const uint64_t flags = ladybird_fcntl_lock_acquire();
    for (uint32_t i = 0u; i < LADYBIRD_FCNTL_LOCK_MAX; ++i) {
        ladybird_fcntl_lock_t *held = &ladybird_fcntl_locks[i];
        if (held->used && held->pid == pid) {
            char path[LADYBIRD_MAX_PATH];
            copy_string(path, sizeof(path), held->path);
            if(held->mandatory)--mandatory_lock_count;
            memset(held, 0, sizeof(*held));
            ladybird_fcntl_wake_path_locked(path);
        }
    }
    for (uint32_t i = 0u; i < LADYBIRD_FCNTL_WAITER_MAX; ++i) {
        if (ladybird_fcntl_waiters[i].used &&
            ladybird_fcntl_waiters[i].pid == pid)
            memset(&ladybird_fcntl_waiters[i], 0,
                   sizeof(ladybird_fcntl_waiters[i]));
    }
    ladybird_fcntl_lock_release(flags);
}

#include "vex_description_ranges.inc"

int fcntl(int fd, int command, int arg) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);
    ladybird_file_ctx_t *file;
    ladybird_pipe_ctx_t *pipe_ctx;
    ladybird_fd_slot_t *fd_slot;

    if (object == NULL) {
        return set_errno(EBADF);
    }

    /* glibc's posix_spawn file-descriptor setup uses F_DUPFD_CLOEXEC to
       obtain a private copy before the child is created.  This is a
       descriptor operation, so keep it in the VexOS FD table and share the
       underlying refcounted object; do not route it through a Unix kernel
       fd table or silently turn it into F_DUPFD. */
    if (command == F_DUPFD || command == F_DUPFD_CLOEXEC) {
        const uint8_t close_on_exec = command == F_DUPFD_CLOEXEC ? 1u : 0u;
        if (arg < 0 || arg >= LADYBIRD_MAX_FDS) {
            ladybird_fd_release(object);
            return set_errno(EINVAL);
        }
        /* Select AND publish under one inventory lock. A concurrent open
           must not have its new FD replaced by F_DUPFD's minimum search. */
        return ladybird_fd_install_from(object, -1, arg, close_on_exec);
    }

    if (command == F_GETFD || command == F_SETFD) {
        int result;
        uint64_t inventory_flags=fd_inventory_lock();
        fd_slot=ladybird_fd_find_slot(ladybird_fd_owner_pid(),fd);
        if (fd_slot == NULL || fd_slot->object!=object) {
            fd_inventory_unlock(inventory_flags);
            ladybird_fd_release(object);
            return set_errno(EBADF);
        }
        if (command == F_GETFD) {
            result = fd_slot->close_on_exec != 0u ? FD_CLOEXEC : 0;
        } else {
            fd_slot->close_on_exec = (arg & FD_CLOEXEC) != 0 ? 1u : 0u;
            result = 0;
        }
        fd_inventory_unlock(inventory_flags);
        ladybird_fd_release(object);
        return result;
    }

    file = object->ops == &file_ops ? (ladybird_file_ctx_t *)object->ctx : NULL;
    pipe_ctx = object->ops == &pipe_ops ? (ladybird_pipe_ctx_t *)object->ctx : NULL;
    {
        const int socket_result = vex_socketpair_fcntl(object, command, arg);
        if (socket_result != -2) {
            ladybird_fd_release(object);
            return socket_result;
        }
    }
    switch (command) {
        case F_GETFL:
            if (file == NULL && pipe_ctx == NULL) {
                ladybird_fd_release(object);
                return set_errno(EBADF);
            }
            arg = file != NULL ? file->flags : pipe_ctx->flags;
            ladybird_fd_release(object);
            return arg;
        case F_SETFL:
            if (file != NULL) {
                file->flags = (file->flags & ~(O_APPEND | O_NONBLOCK)) | (arg & (O_APPEND | O_NONBLOCK));
            } else if (pipe_ctx != NULL) {
                pipe_ctx->flags = (pipe_ctx->flags & ~O_NONBLOCK) | (arg & O_NONBLOCK);
            } else {
                ladybird_fd_release(object);
                return set_errno(EBADF);
            }
            ladybird_fd_release(object);
            return 0;
        default:
            ladybird_fd_release(object);
            return set_errno(ENOTSUP);
    }
}

int dup(int fd) {
    return ladybird_fd_dup_object(fd, -1);
}

int dup2(int oldfd, int newfd) {
    ladybird_fd_object_t *object;

    if (oldfd == newfd) {
        object = ladybird_fd_acquire(oldfd);
        if (object == NULL) {
            return set_errno(EBADF);
        }
        ladybird_fd_release(object);
        return oldfd;
    }

    return ladybird_fd_dup_object(oldfd, newfd);
}

int pipe(int pipefd[2]) {
    int pipe_id;
    ladybird_pipe_ctx_t *read_ctx;
    ladybird_pipe_ctx_t *write_ctx;
    ladybird_fd_object_t *read_object;
    ladybird_fd_object_t *write_object;

    ladybird_posix_init();
    if (pipefd == NULL) {
        return set_errno(EINVAL);
    }

    pipe_id = pipe_create();
    if (pipe_id < 0) {
        return set_errno(ENFILE);
    }

    read_ctx = kmalloc(sizeof(*read_ctx));
    write_ctx = kmalloc(sizeof(*write_ctx));
    if (read_ctx == NULL || write_ctx == NULL) {
        pipe_close(pipe_id);
        kfree(read_ctx);
        kfree(write_ctx);
        return set_errno(ENOMEM);
    }

    memset(read_ctx, 0, sizeof(*read_ctx));
    memset(write_ctx, 0, sizeof(*write_ctx));
    read_ctx->pipe_id = pipe_id;
    read_ctx->readable = 1u;
    write_ctx->pipe_id = pipe_id;
    write_ctx->writable = 1u;

    read_object = ladybird_fd_object_create(read_ctx, &pipe_ops);
    write_object = ladybird_fd_object_create(write_ctx, &pipe_ops);
    if (read_object == NULL || write_object == NULL) {
        if (read_object != NULL) {
            ladybird_fd_release(read_object);
        }
        if (write_object != NULL) {
            ladybird_fd_release(write_object);
        }
        pipe_close(pipe_id);
        return set_errno(ENOMEM);
    }

    pipefd[0] = ladybird_fd_install(read_object, -1);
    if (pipefd[0] < 0) {
        ladybird_fd_release(write_object);
        pipe_close(pipe_id);
        return -1;
    }

    pipefd[1] = ladybird_fd_install(write_object, -1);
    if (pipefd[1] < 0) {
        close(pipefd[0]);
        return -1;
    }

    return 0;
}

int pipe2(int pipefd[2], int flags) {
    if ((flags & ~(O_NONBLOCK | O_CLOEXEC)) != 0) {
        return set_errno(EINVAL);
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

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    uint8_t *mapping;

    (void)addr;
    ladybird_posix_init();

    debug_log_size("mmap: len=", length);

    if (length == 0u) {
        ladybird_errno_value = EINVAL;
        debug_log_puts("mmap: EINVAL(len=0)\n");
        return MAP_FAILED;
    }

    mapping = kmalloc(length);
    if (mapping == NULL) {
        ladybird_errno_value = ENOMEM;
        debug_log_size("mmap: ENOMEM len=", length);
        return MAP_FAILED;
    }

    memset(mapping, 0, length);
    if ((flags & MAP_ANONYMOUS) == 0 && fd >= 0) {
        debug_log_size("mmap: pread fd=", (size_t)fd);
        if (pread(fd, mapping, length, offset) < 0) {
            debug_log_puts("mmap: pread failed\n");
            kfree(mapping);
            return MAP_FAILED;
        }
        debug_log_puts("mmap: pread ok\n");
    }

    for (size_t i = 0; i < LADYBIRD_MAX_MAPPINGS; ++i) {
        if (!ladybird_mappings[i].used) {
            ladybird_mappings[i].addr = mapping;
            ladybird_mappings[i].length = length;
            ladybird_mappings[i].prot = prot;
            ladybird_mappings[i].flags = flags;
            ladybird_mappings[i].used = 1;
            debug_log_puts("mmap: ok\n");
            return mapping;
        }
    }

    kfree(mapping);
    ladybird_errno_value = ENOMEM;
    debug_log_puts("mmap: ENOMEM(table full)\n");
    return MAP_FAILED;
}

int munmap(void *addr, size_t length) {
    (void)length;

    ladybird_posix_init();
    for (size_t i = 0; i < LADYBIRD_MAX_MAPPINGS; ++i) {
        if (ladybird_mappings[i].used && ladybird_mappings[i].addr == addr) {
            kfree(ladybird_mappings[i].addr);
            memset(&ladybird_mappings[i], 0, sizeof(ladybird_mappings[i]));
            return 0;
        }
    }

    return set_errno(EINVAL);
}

int mprotect(void *addr, size_t length, int prot) {
    (void)length;

    ladybird_posix_init();
    for (size_t i = 0; i < LADYBIRD_MAX_MAPPINGS; ++i) {
        if (ladybird_mappings[i].used && ladybird_mappings[i].addr == addr) {
            ladybird_mappings[i].prot = prot;
            return 0;
        }
    }

    return set_errno(EINVAL);
}

int madvise(void *addr, size_t length, int advice) {
    (void)length;
    (void)advice;

    ladybird_posix_init();
    for (size_t i = 0; i < LADYBIRD_MAX_MAPPINGS; ++i) {
        if (ladybird_mappings[i].used && ladybird_mappings[i].addr == addr) {
            return 0;
        }
    }

    return set_errno(EINVAL);
}

static int poll_once(struct pollfd *fds, nfds_t count) {
    int ready = 0;

    for (nfds_t i = 0; i < count; ++i) {
        fds[i].revents = 0;
        /* Negative entries are deliberately disabled by poll callers (GLib
           included), and must never manufacture perpetual readiness. */
        if (fds[i].fd < 0) continue;
        ladybird_fd_object_t *object = ladybird_fd_acquire(fds[i].fd);
        if (object == NULL) {
            fds[i].revents = POLLNVAL;
            ++ready;
            continue;
        }
        if (object->ops == NULL || object->ops->poll == NULL) {
            fds[i].revents = POLLERR;
            ++ready;
            ladybird_fd_release(object);
            continue;
        }

        if (object->ops->poll(object->ctx, fds[i].events, &fds[i].revents) == 0 && fds[i].revents != 0) {
            ++ready;
        }
        ladybird_fd_release(object);
    }

    return ready;
}

static int poll_internal(struct pollfd *fds,nfds_t count,int timeout,int user_array) {
    uint64_t deadline = 0u;

    ladybird_posix_init();
    if (fds == NULL && count != 0) {
        return set_errno(EINVAL);
    }
    if ((uint64_t)count > SIZE_MAX / sizeof(*fds) ||
        (user_array && count != 0u && !vex_syscall_user_range_valid(
            (uint64_t)(uintptr_t)fds, count * sizeof(*fds), 1))) {
        return set_errno(EFAULT);
    }

    if (timeout > 0) {
        deadline = monotonic_ms() + (uint64_t)timeout;
    }

    for (;;) {
        /* The caller may keep this array on a lazily materialized stack while
           another thread or a spawn/exec transition changes mappings.  Re-
           validate immediately before every dereference. */
        if (user_array && count != 0u && !vex_syscall_user_range_valid(
                (uint64_t)(uintptr_t)fds, count * sizeof(*fds), 1)) {
            return set_errno(EFAULT);
        }
        const int ready = poll_once(fds, count);
        if (ready != 0 || timeout == 0) {
            return ready;
        }
        if (timeout > 0 && monotonic_ms() >= deadline) {
            return 0;
        }

        process_sleep_ticks(1u);
    }
}

int poll(struct pollfd *fds,nfds_t count,int timeout){return poll_internal(fds,count,timeout,1);}
int vex_poll_kernel(struct pollfd *fds,nfds_t count,int timeout){return poll_internal(fds,count,timeout,0);}

void exit(int status) {
    process_exit_status(status);
}

void _exit(int status) {
    process_exit_status(status);
}

int wait(int *status) {
    return waitpid(-1, status, 0);
}

int waitpid(int pid, int *status, int options) {
    int result;

    if ((options & ~WNOHANG) != 0) {
        return set_errno(EINVAL);
    }

    result = process_wait_pid(pid, status, (uint32_t)options, NULL);
    if (result < 0) {
        return set_errno(ECHILD);
    }
    return result;
}

int fork(void) {
    const uint32_t child_pid = process_fork_current();
    if (child_pid == 0u) {
        return set_errno(EAGAIN);
    }
    return (int)child_pid;
}

int exec(const char *path) {
    return execve(path, (char *const *)0, (char *const *)0);
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    (void)argv;
    (void)envp;
    if (path == NULL) {
        return set_errno(EINVAL);
    }
    if (elf_exec_current(path) != 0) {
        return set_errno(EINVAL);
    }
    return 0;
}

int kill(int pid, int sig) {
    if (sig != 0 && sig != SIGKILL && sig != SIGTERM) {
        return set_errno(ENOSYS);
    }
    if (pid <= 0) {
        return set_errno(EINVAL);
    }
    if (sig == 0) {
        if (!scheduler_process_alive((uint32_t)pid)) {
            return set_errno(ESRCH);
        }
        return scheduler_can_control_process((uint32_t)pid) ? 0 : set_errno(EPERM);
    }
    if (process_kill((uint32_t)pid) != 0) {
        if (!scheduler_process_alive((uint32_t)pid)) {
            return set_errno(ESRCH);
        }
        return set_errno(EPERM);
    }
    return 0;
}

int sigaction(int sig, const struct sigaction *action, struct sigaction *old_action) {
    (void)sig;
    (void)action;
    (void)old_action;
    return set_errno(ENOSYS);
}

/* CLOCK_REALTIME: the RTC date read once, advanced by the same nanosecond
   clock as CLOCK_MONOTONIC, so both clocks measure intervals identically. */
int vex_realtime_ns(uint64_t *out) {
    static uint64_t epoch_base_ns, clock_base_ns;static uint8_t ready;
    static kernel_gate_t realtime_gate=KERNEL_GATE_INIT;
    if(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE)){
        KERNEL_GUARD(realtime_gate);
        if(!ready){uint64_t seconds;if(rtc_epoch_seconds(&seconds) || seconds>UINT64_MAX/1000000000ull)return -1;
            epoch_base_ns=seconds*1000000000ull;clock_base_ns=scheduler_time_ns();__atomic_store_n(&ready,1,__ATOMIC_RELEASE);}
    }
    uint64_t now=scheduler_time_ns();
    *out=epoch_base_ns+(now-clock_base_ns);return 0;
}
int gettimeofday(struct timeval *tv,void *tz) {
    uint64_t ns;if(!tv || vex_realtime_ns(&ns))return set_errno(EINVAL);
    tv->tv_sec=ns/1000000000ull;tv->tv_usec=(ns%1000000000ull)/1000u;
    if(tz)memset(tz,0,2*sizeof(int32_t));return 0;
}
int clock_gettime(int clock_id,struct timespec *ts) {
    uint64_t ns;if(!ts)return set_errno(EINVAL);
    if(clock_id==0 || clock_id==5){if(vex_realtime_ns(&ns))return set_errno(EINVAL);}
    else if(clock_id==1 || clock_id==4 || clock_id==6 || clock_id==7)ns=scheduler_time_ns();
    else if(clock_id==2 || clock_id==3)ns=scheduler_cpu_time_ns(clock_id==2);
    else return set_errno(EINVAL);
    ts->tv_sec=ns/1000000000ull;ts->tv_nsec=ns%1000000000ull;
    return 0;
}

/* Linux timerfd compatibility is backed by the scheduler's real monotonic
 * ticks.  Reads block on scheduler wait queues, and poll observes expiration
 * state; no timer thread or busy-loop is created for each descriptor. */
#define LINUX_TFD_CLOEXEC       0x80000u
#define LINUX_TFD_NONBLOCK      0x00800u
#define LINUX_TFD_TIMER_ABSTIME 0x00001u
#define LINUX_TFD_TIMER_CANCEL_ON_SET 0x00002u
#define LADYBIRD_TICKS_PER_SEC  (100ull)
#define LADYBIRD_NSEC_PER_TICK  (10000000ull)

static int timerfd_timespec_to_ticks(int64_t seconds, int64_t nanoseconds,
                                     uint64_t *ticks_out) {
    uint64_t ticks, fractional;
    if (ticks_out == NULL || seconds < 0 || nanoseconds < 0 ||
        nanoseconds >= 1000000000ll ||
        (uint64_t)seconds > UINT64_MAX / LADYBIRD_TICKS_PER_SEC) {
        return -1;
    }
    ticks = (uint64_t)seconds * LADYBIRD_TICKS_PER_SEC;
    fractional = ((uint64_t)nanoseconds + LADYBIRD_NSEC_PER_TICK - 1u) /
                 LADYBIRD_NSEC_PER_TICK;
    if (ticks > UINT64_MAX - fractional) return -1;
    *ticks_out = ticks + fractional;
    return 0;
}

static void timerfd_wake_waiters(ladybird_timerfd_ctx_t *timer) {
    if (timer == NULL) return;
    for (ladybird_timerfd_waiter_t *waiter = timer->waiters;
         waiter != NULL; waiter = waiter->next) {
        __atomic_store_n(&waiter->woken,1u,__ATOMIC_RELEASE);
        process_wake(waiter->tid);
    }
}

static void timerfd_update(ladybird_timerfd_ctx_t *timer, uint64_t now) {
    uint64_t elapsed, count;
    if (timer == NULL || timer->armed == 0u || now < timer->deadline_tick) return;
    if (timer->interval_ticks == 0u) {
        if (timer->expirations != UINT64_MAX) ++timer->expirations;
        timer->armed = 0u;
        timer->deadline_tick = 0u;
        return;
    }
    elapsed = now - timer->deadline_tick;
    count = 1u + elapsed / timer->interval_ticks;
    timer->expirations = count > UINT64_MAX - timer->expirations
        ? UINT64_MAX : timer->expirations + count;
    if (count > (UINT64_MAX - timer->deadline_tick) / timer->interval_ticks) {
        timer->armed = 0u;
        timer->deadline_tick = 0u;
    } else {
        timer->deadline_tick += count * timer->interval_ticks;
    }
}

static void timerfd_ticks_to_timespec(uint64_t ticks, int64_t *seconds,
                                      int64_t *nanoseconds) {
    *seconds = (int64_t)(ticks / LADYBIRD_TICKS_PER_SEC);
    *nanoseconds = (int64_t)((ticks % LADYBIRD_TICKS_PER_SEC) *
                             LADYBIRD_NSEC_PER_TICK);
}

static void timerfd_export_spec(ladybird_timerfd_ctx_t *timer,
                                vex_linux_itimerspec_t *spec) {
    const uint64_t now = scheduler_ticks_now();
    uint64_t remaining = timer->armed != 0u && timer->deadline_tick > now
        ? timer->deadline_tick - now : 0u;
    memset(spec, 0, sizeof(*spec));
    timerfd_ticks_to_timespec(timer->interval_ticks,
                              &spec->interval_sec, &spec->interval_nsec);
    timerfd_ticks_to_timespec(remaining, &spec->value_sec, &spec->value_nsec);
}

static kernel_gate_t timerfd_gate=KERNEL_GATE_INIT;
static ssize_t timerfd_read_op(void *ctx,void *buffer,size_t count) {
    ladybird_timerfd_ctx_t *timer=ctx;
    if(!timer || !buffer || count!=sizeof(uint64_t))return set_errno_ssize(EINVAL);
    for(;;) {
        ladybird_timerfd_waiter_t waiter={0};uint64_t ticks=0;
        {
            KERNEL_GUARD(timerfd_gate);
            uint64_t now=scheduler_ticks_now();timerfd_update(timer,now);
            if(timer->expirations) {
                uint64_t value=timer->expirations;timer->expirations=0u;
                memcpy(buffer,&value,sizeof(value));return sizeof(value);
            }
            if(timer->nonblock)return set_errno_ssize(EAGAIN);
            waiter.tid=scheduler_current_tid();waiter.next=timer->waiters;timer->waiters=&waiter;
            if(timer->armed && timer->deadline_tick>now)ticks=timer->deadline_tick-now;
        }
        if(ticks)process_wait_event_timeout(&waiter.woken,ticks);
        else process_wait_event(&waiter.woken);
        {
            KERNEL_GUARD(timerfd_gate);
            ladybird_timerfd_waiter_t **link=&timer->waiters;
            while(*link && *link!=&waiter)link=&(*link)->next;
            if(*link)*link=waiter.next;
        }
    }
}

static ssize_t timerfd_write_op(void *ctx, const void *buffer, size_t count) {
    (void)ctx; (void)buffer; (void)count;
    return set_errno_ssize(EINVAL);
}

static int timerfd_close_op(void *ctx) {
    KERNEL_GUARD(timerfd_gate);
    ladybird_timerfd_ctx_t *timer = ctx;
    if (timer != NULL) {
        timerfd_wake_waiters(timer);
        kfree(timer);
    }
    return 0;
}

static int timerfd_fstat_op(void *ctx, struct stat *st) {
    (void)ctx;
    if (st == NULL) return set_errno(EINVAL);
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0600u;
    return 0;
}

static int timerfd_seek_op(void *ctx, off_t offset, int whence, off_t *result) {
    (void)ctx; (void)offset; (void)whence; (void)result;
    return set_errno(ESPIPE);
}

static int timerfd_fsync_op(void *ctx) { (void)ctx; return set_errno(EINVAL); }

static int timerfd_poll_op(void *ctx, short events, short *revents) {
    KERNEL_GUARD(timerfd_gate);
    ladybird_timerfd_ctx_t *timer = ctx;
    if (timer == NULL || revents == NULL) return set_errno(EINVAL);
    timerfd_update(timer, scheduler_ticks_now());
    *revents = timer->expirations != 0u && (events & POLLIN) != 0
        ? POLLIN : 0;
    return 0;
}

static const ladybird_fd_ops_t timerfd_ops = {
    timerfd_read_op, timerfd_write_op, timerfd_close_op,
    timerfd_fstat_op, timerfd_seek_op, timerfd_fsync_op, timerfd_poll_op,
    NULL, NULL
};

static void fd_cancel_stopped_object(ladybird_fd_object_t *object,uint32_t tid) {
    if(object && object->ops==&file_ops) {
        vex_file_range_thread_stopped(tid);
    }
    if(!object || object->ops!=&timerfd_ops || !object->ctx)return;
    KERNEL_GUARD(timerfd_gate);
    ladybird_timerfd_ctx_t *timer=object->ctx;
    ladybird_timerfd_waiter_t **link=&timer->waiters;
    while(*link) {
        ladybird_timerfd_waiter_t *w=*link;
        if(w->tid!=tid){link=&w->next;continue;}
        *link=w->next;w->next=NULL;
    }
}

int vex_web_timerfd_create(int clock_id, uint32_t flags) {
    ladybird_timerfd_ctx_t *timer;
    ladybird_fd_object_t *object;
    if ((clock_id != 0 && clock_id != 1) ||
        (flags & ~(LINUX_TFD_CLOEXEC | LINUX_TFD_NONBLOCK)) != 0u) {
        return set_errno(EINVAL);
    }
    timer = kmalloc(sizeof(*timer));
    if (timer == NULL) return set_errno(ENOMEM);
    memset(timer, 0, sizeof(*timer));
    timer->clock_id = (uint32_t)clock_id;
    timer->nonblock = (flags & LINUX_TFD_NONBLOCK) != 0u;
    object = ladybird_fd_object_create(timer, &timerfd_ops);
    if (object == NULL) {
        kfree(timer);
        return set_errno(ENOMEM);
    }
    return ladybird_fd_install_flags(object, -1,
        (flags & LINUX_TFD_CLOEXEC) != 0u ? 1u : 0u);
}

int vex_web_timerfd_settime(int fd, uint32_t flags,
                            const vex_linux_itimerspec_t *new_value,
                            vex_linux_itimerspec_t *old_value) {
    ladybird_fd_object_t *object;
    ladybird_timerfd_ctx_t *timer;
    uint64_t value_ticks, interval_ticks, deadline, now;
    if (new_value == NULL || (flags & ~(LINUX_TFD_TIMER_ABSTIME |
                                         LINUX_TFD_TIMER_CANCEL_ON_SET)) != 0u) {
        return set_errno(EINVAL);
    }
    if ((flags & LINUX_TFD_TIMER_CANCEL_ON_SET) != 0u) {
        /* Vex has no settimeofday operation, so cancel-on-clock-set cannot
         * occur. Refuse that opt-in contract instead of claiming to support it. */
        return set_errno(ENOTSUP);
    }
    if (timerfd_timespec_to_ticks(new_value->value_sec,
                                  new_value->value_nsec, &value_ticks) != 0 ||
        timerfd_timespec_to_ticks(new_value->interval_sec,
                                  new_value->interval_nsec, &interval_ticks) != 0) {
        return set_errno(EINVAL);
    }
    object = ladybird_fd_acquire(fd);
    if (object == NULL || object->ops != &timerfd_ops || object->ctx == NULL) {
        if (object != NULL) ladybird_fd_release(object);
        return set_errno(EBADF);
    }
    KERNEL_GUARD(timerfd_gate);
    timer = object->ctx;
    timerfd_update(timer, scheduler_ticks_now());
    if (old_value != NULL) timerfd_export_spec(timer, old_value);
    now = scheduler_ticks_now();
    deadline = 0u;
    if (value_ticks != 0u) {
        if ((flags & LINUX_TFD_TIMER_ABSTIME) != 0u && timer->clock_id == 1u) {
            deadline = value_ticks;
        } else if ((flags & LINUX_TFD_TIMER_ABSTIME) != 0u) {
            struct timespec current;
            uint64_t target_ns, current_ns, delay_ticks = 0u;
            if ((uint64_t)new_value->value_sec > UINT64_MAX / 1000000000ull ||
                clock_gettime(0, &current) != 0) {
                ladybird_fd_release(object);
                return set_errno(EINVAL);
            }
            target_ns = (uint64_t)new_value->value_sec * 1000000000ull +
                        (uint64_t)new_value->value_nsec;
            current_ns = (uint64_t)current.tv_sec * 1000000000ull +
                         (uint64_t)current.tv_nsec;
            if (target_ns > current_ns) {
                const uint64_t delta = target_ns - current_ns;
                delay_ticks = delta / LADYBIRD_NSEC_PER_TICK +
                    (delta % LADYBIRD_NSEC_PER_TICK != 0u);
            }
            deadline = delay_ticks > UINT64_MAX - now ? UINT64_MAX
                                                       : now + delay_ticks;
        } else {
            deadline = value_ticks > UINT64_MAX - now ? UINT64_MAX
                                                       : now + value_ticks;
        }
    }
    timer->interval_ticks = interval_ticks;
    timer->expirations = 0u;
    timer->deadline_tick = deadline;
    timer->armed = value_ticks != 0u;
    timerfd_wake_waiters(timer);
    ladybird_fd_release(object);
    return 0;
}

int vex_web_timerfd_gettime(int fd, vex_linux_itimerspec_t *current_value) {
    ladybird_fd_object_t *object;
    ladybird_timerfd_ctx_t *timer;
    if (current_value == NULL) return set_errno(EINVAL);
    object = ladybird_fd_acquire(fd);
    if (object == NULL || object->ops != &timerfd_ops || object->ctx == NULL) {
        if (object != NULL) ladybird_fd_release(object);
        return set_errno(EBADF);
    }
    KERNEL_GUARD(timerfd_gate);
    timer = object->ctx;
    timerfd_update(timer, scheduler_ticks_now());
    timerfd_export_spec(timer, current_value);
    ladybird_fd_release(object);
    return 0;
}

int nanosleep(const struct timespec *requested, struct timespec *remaining) {
    uint64_t ms;

    if (requested == NULL || requested->tv_sec < 0 || requested->tv_nsec < 0 || requested->tv_nsec >= 1000000000L) {
        return set_errno(EINVAL);
    }

    /* Capture before touching remaining: callers may alias both pointers. */
    const struct timespec request=*requested;
    if((uint64_t)request.tv_sec>(UINT64_MAX-999ull)/1000ull)return set_errno(EINVAL);
    ms = (uint64_t)request.tv_sec * 1000ull + ((uint64_t)request.tv_nsec + 999999ull) / 1000000ull;
    if (remaining != NULL) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }

    process_sleep_ticks(ms_to_ticks(ms));
    return 0;
}

unsigned int sleep(unsigned int seconds) {
    process_sleep_ticks((uint64_t)seconds * (1000ull / LADYBIRD_TICK_MS));
    return 0;
}

int usleep(unsigned int usec) {
    process_sleep_ticks(ms_to_ticks(((uint64_t)usec + 999ull) / 1000ull));
    return 0;
}

typedef struct {uint8_t *bytes;size_t size;} rooted_truncate_write;
static int root_truncate_write(const char *path,void *opaque) {
    rooted_truncate_write *request=opaque;return fs_write(path,request->bytes,request->size);
}
int ftruncate(int fd, off_t length) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object;
    ladybird_file_ctx_t *file;
    uint8_t *replacement = NULL;
    size_t new_size;
    size_t copy_size;

    if (length < 0) {
        return set_errno(EINVAL);
    }

    object = vex_fd_borrow_acquire(&operation_ref,fd);
    if (object == NULL) {
        return set_errno(EBADF);
    }
    if (object->ops != &file_ops || object->ctx == NULL) {

        return set_errno(EINVAL);
    }

    file = (ladybird_file_ctx_t *)object->ctx;
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    KERNEL_GUARD(file->data_gate);
    wk2_shared_trace("ftruncate.begin", file->path, (uint64_t)length,
                     (uint32_t)fd, 0u);
    hsts_file_trace("ftruncate", file->path, (uint64_t)length,
                    (uint32_t)fd, 0);
    if ((file->flags & O_ACCMODE) == O_RDONLY || file->write_allowed == 0u) {

        return set_errno(EBADF);
    }
    new_size = (size_t)length;
    if ((off_t)new_size != length) {

        return set_errno(EINVAL);
    }

    /* Build the new contents separately.  ftruncate must update the file's
     * visible length (and zero-fill an extension); seeking and syncing alone
     * only changes the descriptor offset.  Keeping the old buffer untouched
     * until fs_write commits also makes allocation/disk errors leave this
     * descriptor's contents and size unchanged. */
    if (new_size != 0u) {
        replacement = kmalloc(new_size);
        if (replacement == NULL) {
            wk2_shared_trace("ftruncate.fail", file->path,
                             (uint64_t)length, (uint32_t)fd,
                             (uint64_t)(int64_t)-ENOMEM);

            return set_errno(ENOMEM);
        }
        copy_size = new_size < file->size ? new_size : file->size;
        if (copy_size != 0u) {
            if (file->buffer == NULL) {
                kfree(replacement);

                return set_errno(EIO);
            }
            memcpy(replacement, file->buffer, copy_size);
            process_memory_file_object_cache_copy(object, 0, replacement, copy_size, 0);
        }
        if (new_size > copy_size) {
            memset(replacement + copy_size, 0, new_size - copy_size);
        }
    }

    int write_result=0;
    if(file->volatile_shm==0u) {
        if(file->root) {
            char relative[512];rooted_truncate_write request={replacement,new_size};
            if(fs_root_relative(file->root,file->path,relative,sizeof(relative)))write_result=-1;
            else write_result=fs_root_invoke(file->root,file->root_rights,file->granted_uid,file->granted_caps,
                relative,root_truncate_write,&request);
        }else write_result=fs_write(file->path,replacement,new_size);
    }
    if (write_result != 0) {
        wk2_shared_trace("ftruncate.fail", file->path,
                         (uint64_t)length, (uint32_t)fd,
                         (uint64_t)(int64_t)-EIO);
        kfree(replacement);

        return set_errno(EIO);
    }

    if (file->buffer_owned != 0u) {
        kfree(file->buffer);
    }
    ora_view_release(&file->content);
    process_memory_file_object_truncate(object, new_size);
    file->buffer = replacement;
    file->size = new_size;
    file->capacity = new_size;
    file->buffer_owned = 1u;
    file->dirty = 0u;
    wk2_shared_trace("ftruncate.end", file->path, file->size,
                     (uint32_t)fd, 0u);
    hsts_file_trace("ftruncate.end", file->path, file->size,
                    (uint32_t)fd, 0);

    return 0;
}

int chmod(const char *path, uint32_t mode) {
    char normalized[LADYBIRD_MAX_PATH];

    if (path == NULL) {
        return set_errno(EINVAL);
    }

    ladybird_posix_init();
    normalize_path(path, normalized);
    if (fs_set_mode(normalized, mode) != 0) {
        return set_errno(EACCES);
    }
    return 0;
}

ssize_t readlink(const char *path, char *buffer, size_t size) {
    char normalized[LADYBIRD_MAX_PATH];
    int count;

    if (path == NULL || buffer == NULL || size == 0u) {
        return set_errno(EINVAL);
    }

    ladybird_posix_init();
    normalize_path(path, normalized);
    count = fs_readlink(normalized, buffer, size);
    if (count < 0) {
        return set_errno_ssize(EINVAL);
    }
    return (ssize_t)count;
}

int symlink(const char *target, const char *link_path) {
    char normalized[LADYBIRD_MAX_PATH];

    if (target == NULL || link_path == NULL) {
        return set_errno(EINVAL);
    }

    ladybird_posix_init();
    normalize_path(link_path, normalized);
    if (fs_symlink(target, normalized) != 0) {
        return set_errno(EIO);
    }
    return 0;
}

char *realpath(const char *path, char *resolved_path) {
    char normalized[LADYBIRD_MAX_PATH];

    if (path == NULL || resolved_path == NULL) {
        ladybird_errno_value = EINVAL;
        return NULL;
    }

    normalize_path(path, normalized);
    if (fs_resolve_path(normalized, resolved_path, LADYBIRD_MAX_PATH) != 0) {
        copy_string(resolved_path, LADYBIRD_MAX_PATH, normalized);
    }
    return resolved_path;
}

char *getenv(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return NULL;
    }

    ladybird_posix_init();
    for (size_t i = 0; i < LADYBIRD_MAX_ENV; ++i) {
        if (ladybird_env[i].used && string_equals(ladybird_env[i].name, name)) {
            return ladybird_env[i].value;
        }
    }

    return NULL;
}

int setenv(const char *name, const char *value, int overwrite) {
    size_t free_slot = LADYBIRD_MAX_ENV;

    if (name == NULL || name[0] == '\0' || value == NULL) {
        return set_errno(EINVAL);
    }

    ladybird_posix_init();
    for (size_t i = 0; i < LADYBIRD_MAX_ENV; ++i) {
        if (ladybird_env[i].used && string_equals(ladybird_env[i].name, name)) {
            if (!overwrite) {
                return 0;
            }
            copy_string(ladybird_env[i].value, sizeof(ladybird_env[i].value), value);
            return 0;
        }
        if (!ladybird_env[i].used && free_slot == LADYBIRD_MAX_ENV) {
            free_slot = i;
        }
    }

    if (free_slot == LADYBIRD_MAX_ENV) {
        return set_errno(ENOMEM);
    }

    ladybird_env[free_slot].used = 1u;
    copy_string(ladybird_env[free_slot].name, sizeof(ladybird_env[free_slot].name), name);
    copy_string(ladybird_env[free_slot].value, sizeof(ladybird_env[free_slot].value), value);
    return 0;
}

int unsetenv(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return set_errno(EINVAL);
    }

    ladybird_posix_init();
    for (size_t i = 0; i < LADYBIRD_MAX_ENV; ++i) {
        if (ladybird_env[i].used && string_equals(ladybird_env[i].name, name)) {
            memset(&ladybird_env[i], 0, sizeof(ladybird_env[i]));
            return 0;
        }
    }

    return 0;
}

long sysconf(int name) {
    if (name == _SC_PAGESIZE || name == _SC_PAGE_SIZE) {
        return 4096;
    }
    if (name == _SC_OPEN_MAX) {
        return LADYBIRD_MAX_FDS;
    }
    if (name == _SC_NPROCESSORS_ONLN) {
        return (long)smp_cpu_count();
    }

    return -1;
}

int uname(void *buffer) {
    struct utsname *name = buffer;

    if (name == NULL) {
        return set_errno(EINVAL);
    }

    copy_string(name->sysname, sizeof(name->sysname), "KyIlOS");
    copy_string(name->nodename, sizeof(name->nodename), "vex");
    copy_string(name->release, sizeof(name->release), "0.1");
    copy_string(name->version, sizeof(name->version), "KyIlOS kernel");
    copy_string(name->machine, sizeof(name->machine), "x86_64");
    return 0;
}

int getrandom(void *buffer, size_t length, unsigned int flags) {
    (void)flags;
    if (buffer == NULL) {
        return set_errno(EINVAL);
    }

    if (kernel_random_try_fill(buffer, length) != 0) return set_errno(EAGAIN);
    return (int)length;
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    void *ptr;

    if (memptr == NULL || alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
        return EINVAL;
    }

    ptr = kmalloc(size + alignment);
    if (ptr == NULL) {
        return ENOMEM;
    }

    *memptr = ptr;
    return 0;
}

size_t malloc_good_size(size_t size) {
    const size_t alignment = 16u;

    return (size + alignment - 1u) & ~(alignment - 1u);
}
