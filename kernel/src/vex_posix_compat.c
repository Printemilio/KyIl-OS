#include <unos/vex_legacy_compat.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/elf.h>
#include <unos/fs.h>
#include <unos/heap.h>
#include <unos/ipc.h>
#include <unos/irq.h>
#include <unos/ethernet.h>
#include <unos/keyboard.h>
#include <unos/scheduler.h>
#include <unos/tty.h>
#include <unos/users.h>

#define LADYBIRD_MAX_FDS 128
#define LADYBIRD_MAX_PATH 256
#define LADYBIRD_MAX_MAPPINGS 64
#define LADYBIRD_MAX_ENV 32
#define LADYBIRD_ENV_NAME_MAX 48
#define LADYBIRD_ENV_VALUE_MAX 160
#define LADYBIRD_TICK_MS 10ull
#define LADYBIRD_DIR_MARKER ".ladybird_dir"

typedef struct ladybird_fd_slot {
    ladybird_fd_object_t *object;
} ladybird_fd_slot_t;

typedef struct ladybird_console_ctx {
    int readable;
    int writable;
    char pending_char;
} ladybird_console_ctx_t;

typedef struct ladybird_file_ctx {
    char path[LADYBIRD_MAX_PATH];
    uint8_t *buffer;
    size_t size;
    size_t capacity;
    size_t offset;
    int flags;
    uint8_t dirty;
} ladybird_file_ctx_t;

typedef struct ladybird_pipe_ctx {
    int pipe_id;
    uint8_t readable;
    uint8_t writable;
    uint32_t *shared_refs;
} ladybird_pipe_ctx_t;

typedef struct ladybird_mmap_entry {
    void *addr;
    size_t length;
    int prot;
    int flags;
    int used;
} ladybird_mmap_entry_t;

typedef struct ladybird_env_entry {
    uint8_t used;
    char name[LADYBIRD_ENV_NAME_MAX];
    char value[LADYBIRD_ENV_VALUE_MAX];
} ladybird_env_entry_t;

static int ladybird_errno_value;
static uint8_t ladybird_posix_ready;
static ladybird_fd_slot_t ladybird_fd_table[LADYBIRD_MAX_FDS];
static char ladybird_cwd[LADYBIRD_MAX_PATH];
static ladybird_mmap_entry_t ladybird_mappings[LADYBIRD_MAX_MAPPINGS];
static ladybird_env_entry_t ladybird_env[LADYBIRD_MAX_ENV];

static ssize_t console_read(void *ctx, void *buffer, size_t count);
static ssize_t console_write(void *ctx, const void *buffer, size_t count);
static int console_close(void *ctx);
static int console_fstat(void *ctx, struct stat *st);
static int console_seek(void *ctx, off_t offset, int whence, off_t *result);
static int console_fsync(void *ctx);
static int console_poll(void *ctx, short events, short *revents);

static ssize_t file_read_op(void *ctx, void *buffer, size_t count);
static ssize_t file_write_op(void *ctx, const void *buffer, size_t count);
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

static const ladybird_fd_ops_t console_ops = {
    console_read,
    console_write,
    console_close,
    console_fstat,
    console_seek,
    console_fsync,
    console_poll
};

static const ladybird_fd_ops_t file_ops = {
    file_read_op,
    file_write_op,
    file_close_op,
    file_fstat_op,
    file_seek_op,
    file_fsync_op,
    file_poll_op
};

static const ladybird_fd_ops_t pipe_ops = {
    pipe_read_op,
    pipe_write_op,
    pipe_close_op,
    pipe_fstat_op,
    pipe_seek_op,
    pipe_fsync_op,
    pipe_poll_op
};

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

static int set_errno(int value) {
    ladybird_errno_value = value;
    return -1;
}

static uint64_t monotonic_ms(void) {
    return irq_timer_ticks() * LADYBIRD_TICK_MS;
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

static void directory_marker_path(const char *path, char out[LADYBIRD_MAX_PATH]) {
    path_join(out, LADYBIRD_MAX_PATH, path, LADYBIRD_DIR_MARKER);
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

static int directory_exists_scan_match;
static const char *directory_exists_scan_prefix;
static size_t directory_exists_scan_prefix_length;

static void directory_exists_scan_visitor(const char *path, size_t size) {
    (void)size;

    if (directory_exists_scan_match || path == NULL) {
        return;
    }

    if (starts_with(path, directory_exists_scan_prefix) &&
        path[directory_exists_scan_prefix_length] == '/') {
        directory_exists_scan_match = 1;
    }
}

static int path_is_directory(const char *path) {
    char marker[LADYBIRD_MAX_PATH];

    if (path == NULL) {
        return 0;
    }

    if (string_equals(path, "/")) {
        return 1;
    }

    directory_marker_path(path, marker);
    if (fs_exists(marker)) {
        return 1;
    }

    directory_exists_scan_match = 0;
    directory_exists_scan_prefix = path;
    directory_exists_scan_prefix_length = string_length(path);
    fs_list(directory_exists_scan_visitor);
    return directory_exists_scan_match;
}

static int path_exists_any(const char *path) {
    return fs_exists(path) || path_is_directory(path);
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

static int flush_file(ladybird_file_ctx_t *file) {
    if (file == NULL || !file->dirty) {
        return 0;
    }

    if (file->size == 0u) {
        if (fs_exists(file->path)) {
            (void)fs_delete(file->path);
        }
        file->dirty = 0;
        return 0;
    }

    if (fs_write(file->path, file->buffer, file->size) != 0) {
        return -1;
    }

    file->dirty = 0;
    return 0;
}

static int fill_stat_for_path(const char *path, struct stat *st) {
    void *data = NULL;
    size_t size = 0;
    char marker[LADYBIRD_MAX_PATH];

    if (path == NULL || st == NULL) {
        return set_errno(EINVAL);
    }

    memset(st, 0, sizeof(*st));
    st->st_uid = user_current_uid();
    st->st_gid = user_current_uid();
    st->st_blksize = 4096u;

    if (string_equals(path, "/") || path_is_directory(path)) {
        directory_marker_path(path, marker);
        st->st_mode = S_IFDIR;
        st->st_size = 0u;
        st->st_blocks = fs_exists(marker) ? 1u : 0u;
        return 0;
    }

    if (fs_read(path, &data, &size) != 0) {
        return set_errno(ENOENT);
    }

    st->st_mode = S_IFREG;
    st->st_size = size;
    st->st_blocks = (size + 511u) / 512u;
    return 0;
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
    ctx->pending_char = 0;
    object = ladybird_fd_object_create(ctx, &console_ops);
    if (object == NULL) {
        kfree(ctx);
        return -1;
    }

    return ladybird_fd_install(object, fd);
}

int *ladybird_errno_location(void) {
    return &ladybird_errno_value;
}

ladybird_fd_object_t *ladybird_fd_object_create(void *ctx, const ladybird_fd_ops_t *ops) {
    ladybird_fd_object_t *object = kmalloc(sizeof(*object));

    if (object == NULL) {
        return NULL;
    }

    object->refcount = 1u;
    object->ctx = ctx;
    object->ops = ops;
    return object;
}

int ladybird_fd_install(ladybird_fd_object_t *object, int preferred_fd) {
    int index = preferred_fd;

    if (object == NULL) {
        return set_errno(EBADF);
    }

    ladybird_posix_init();

    if (preferred_fd >= 0) {
        if (preferred_fd >= LADYBIRD_MAX_FDS) {
            ladybird_fd_release(object);
            return set_errno(EMFILE);
        }

        if (ladybird_fd_table[preferred_fd].object != NULL) {
            close(preferred_fd);
        }

        ladybird_fd_table[preferred_fd].object = object;
        return preferred_fd;
    }

    for (index = 0; index < LADYBIRD_MAX_FDS; ++index) {
        if (ladybird_fd_table[index].object == NULL) {
            ladybird_fd_table[index].object = object;
            return index;
        }
    }

    ladybird_fd_release(object);
    return set_errno(EMFILE);
}

ladybird_fd_object_t *ladybird_fd_acquire(int fd) {
    ladybird_fd_object_t *object;

    ladybird_posix_init();
    if (fd < 0 || fd >= LADYBIRD_MAX_FDS || ladybird_fd_table[fd].object == NULL) {
        return NULL;
    }

    object = ladybird_fd_table[fd].object;
    ++object->refcount;
    return object;
}

void ladybird_fd_release(ladybird_fd_object_t *object) {
    if (object == NULL) {
        return;
    }

    if (object->refcount > 0u) {
        --object->refcount;
    }

    if (object->refcount == 0u) {
        if (object->ops != NULL && object->ops->close != NULL) {
            (void)object->ops->close(object->ctx);
        }
        kfree(object);
    }
}

int ladybird_fd_dup_object(int fd, int preferred_fd) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);

    if (object == NULL) {
        return set_errno(EBADF);
    }

    return ladybird_fd_install(object, preferred_fd);
}

void ladybird_posix_init(void) {
    if (ladybird_posix_ready) {
        return;
    }

    memset(ladybird_fd_table, 0, sizeof(ladybird_fd_table));
    memset(ladybird_mappings, 0, sizeof(ladybird_mappings));
    memset(ladybird_env, 0, sizeof(ladybird_env));
    copy_string(ladybird_cwd, sizeof(ladybird_cwd), "/");
    ladybird_errno_value = 0;
    ladybird_posix_ready = 1;

    /* Default environment for all Linux-ABI processes.
     * These mirror what a real Linux distro would have in /etc/environment.
     * Processes can still override via setenv(name, value, 1). */

    /* TLS / HTTPS — point libsoup + GnuTLS at the CA bundle on the CD */
    setenv("SSL_CERT_FILE",    "/etc/ssl/certs/ca-certificates.crt", 0);
    setenv("SSL_CERT_DIR",     "/etc/ssl/certs",                     0);
    setenv("CURL_CA_BUNDLE",   "/etc/ssl/certs/ca-certificates.crt", 0);

    /* Fonts — tell fontconfig where its config and cache live */
    setenv("FONTCONFIG_PATH",  "/etc/fonts",   0);
    setenv("FONTCONFIG_CACHE", "/tmp/fc-cache", 0);
    setenv("FC_CACHEDIR",      "/tmp/fc-cache", 0);

    /* GLib / GIO data dirs — required for gio TLS module discovery */
    setenv("XDG_DATA_DIRS",    "/usr/share:/usr/local/share", 0);
    setenv("XDG_CACHE_HOME",   "/tmp/.cache",                 0);

    (void)create_console_fd(STDIN_FILENO, 1, 0);
    (void)create_console_fd(STDOUT_FILENO, 0, 1);
    (void)create_console_fd(STDERR_FILENO, 0, 1);
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
    const uint8_t *bytes = buffer;
    ladybird_console_ctx_t *console = ctx;

    if (console == NULL || !console->writable || buffer == NULL) {
        return set_errno_ssize(EBADF);
    }

    for (size_t i = 0; i < count; ++i) {
        tty_putchar((char)bytes[i]);
    }

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

    if (file == NULL || buffer == NULL) {
        return set_errno_ssize(EINVAL);
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

static ssize_t file_write_op(void *ctx, const void *buffer, size_t count) {
    ladybird_file_ctx_t *file = ctx;
    size_t target_end;

    if (file == NULL || buffer == NULL) {
        return set_errno_ssize(EINVAL);
    }

    if ((file->flags & O_ACCMODE) == O_RDONLY) {
        return set_errno_ssize(EBADF);
    }

    if ((file->flags & O_APPEND) != 0) {
        file->offset = file->size;
    }

    target_end = file->offset + count;
    if (ensure_file_capacity(file, target_end) != 0) {
        return set_errno_ssize(ENOMEM);
    }

    memcpy(file->buffer + file->offset, buffer, count);
    file->offset = target_end;
    if (target_end > file->size) {
        file->size = target_end;
    }

    file->dirty = 1;
    return (ssize_t)count;
}

static int file_close_op(void *ctx) {
    ladybird_file_ctx_t *file = ctx;

    if (file == NULL) {
        return 0;
    }

    (void)flush_file(file);
    kfree(file->buffer);
    kfree(file);
    return 0;
}

static int file_fstat_op(void *ctx, struct stat *st) {
    ladybird_file_ctx_t *file = ctx;

    if (file == NULL || st == NULL) {
        return set_errno(EINVAL);
    }

    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    st->st_uid = user_current_uid();
    st->st_gid = user_current_uid();
    st->st_size = file->size;
    st->st_blksize = 4096u;
    st->st_blocks = (file->size + 511u) / 512u;
    return 0;
}

static int file_seek_op(void *ctx, off_t offset, int whence, off_t *result) {
    ladybird_file_ctx_t *file = ctx;
    off_t target = 0;

    if (file == NULL) {
        return set_errno(EBADF);
    }

    switch (whence) {
        case SEEK_SET:
            target = offset;
            break;
        case SEEK_CUR:
            target = (off_t)file->offset + offset;
            break;
        case SEEK_END:
            target = (off_t)file->size + offset;
            break;
        default:
            return set_errno(EINVAL);
    }

    if (target < 0) {
        return set_errno(EINVAL);
    }

    file->offset = (size_t)target;
    if (result != NULL) {
        *result = target;
    }
    return 0;
}

static int file_fsync_op(void *ctx) {
    if (flush_file((ladybird_file_ctx_t *)ctx) != 0) {
        return set_errno(EIO);
    }
    return 0;
}

static int file_poll_op(void *ctx, short events, short *revents) {
    ladybird_file_ctx_t *file = ctx;

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

static ssize_t pipe_read_op(void *ctx, void *buffer, size_t count) {
    ladybird_pipe_ctx_t *pipe = ctx;
    int read_result;

    if (pipe == NULL || !pipe->readable || buffer == NULL) {
        return set_errno_ssize(EBADF);
    }

    read_result = pipe_read(pipe->pipe_id, buffer, count);
    if (read_result < 0) {
        return set_errno_ssize(EIO);
    }

    return (ssize_t)read_result;
}

static ssize_t pipe_write_op(void *ctx, const void *buffer, size_t count) {
    ladybird_pipe_ctx_t *pipe = ctx;
    int write_result;

    if (pipe == NULL || !pipe->writable || buffer == NULL) {
        return set_errno_ssize(EBADF);
    }

    write_result = pipe_write(pipe->pipe_id, buffer, count);
    if (write_result < 0) {
        return set_errno_ssize(EIO);
    }

    return (ssize_t)write_result;
}

static int pipe_close_op(void *ctx) {
    ladybird_pipe_ctx_t *pipe = ctx;

    if (pipe == NULL) {
        return 0;
    }

    if (pipe->shared_refs != NULL && *pipe->shared_refs > 0u) {
        --(*pipe->shared_refs);
        if (*pipe->shared_refs == 0u) {
            pipe_close(pipe->pipe_id);
            kfree(pipe->shared_refs);
        }
    }

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
        if (pipe_readable(pipe->pipe_id) != 0u) {
            *revents |= POLLIN;
        }
    }
    if ((events & POLLOUT) != 0 && pipe->writable) {
        if (pipe_writable(pipe->pipe_id) != 0u) {
            *revents |= POLLOUT;
        }
    }
    return 0;
}

int open(const char *path, int flags, ...) {
    char normalized[LADYBIRD_MAX_PATH];
    ladybird_file_ctx_t *file;
    ladybird_fd_object_t *object;
    void *data = NULL;
    size_t size = 0;

    ladybird_posix_init();
    normalize_path(path, normalized);

    if (path_is_directory(normalized)) {
        return set_errno(EISDIR);
    }

    if (!fs_exists(normalized)) {
        if ((flags & O_CREAT) == 0) {
            return set_errno(ENOENT);
        }
    } else if (fs_read(normalized, &data, &size) != 0) {
        return set_errno(EIO);
    }

    file = kmalloc(sizeof(*file));
    if (file == NULL) {
        return set_errno(ENOMEM);
    }

    memset(file, 0, sizeof(*file));
    copy_string(file->path, sizeof(file->path), normalized);
    file->flags = flags;

    if ((flags & O_TRUNC) != 0) {
        size = 0u;
    }

    if (size > 0u) {
        file->buffer = kmalloc(size);
        if (file->buffer == NULL) {
            kfree(file);
            return set_errno(ENOMEM);
        }
        memcpy(file->buffer, data, size);
        file->size = size;
        file->capacity = size;
    }

    if ((flags & O_APPEND) != 0) {
        file->offset = file->size;
    }

    object = ladybird_fd_object_create(file, &file_ops);
    if (object == NULL) {
        kfree(file->buffer);
        kfree(file);
        return set_errno(ENOMEM);
    }

    return ladybird_fd_install(object, -1);
}

int close(int fd) {
    ladybird_fd_object_t *object;

    ladybird_posix_init();
    if (fd < 0 || fd >= LADYBIRD_MAX_FDS || ladybird_fd_table[fd].object == NULL) {
        return set_errno(EBADF);
    }

    object = ladybird_fd_table[fd].object;
    ladybird_fd_table[fd].object = NULL;
    ladybird_fd_release(object);
    return 0;
}

ssize_t read(int fd, void *buffer, size_t count) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);
    ssize_t result;

    if (object == NULL || object->ops == NULL || object->ops->read == NULL) {
        return set_errno_ssize(EBADF);
    }

    result = object->ops->read(object->ctx, buffer, count);
    ladybird_fd_release(object);
    return result;
}

ssize_t write(int fd, const void *buffer, size_t count) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);
    ssize_t result;

    if (object == NULL || object->ops == NULL || object->ops->write == NULL) {
        return set_errno_ssize(EBADF);
    }

    result = object->ops->write(object->ctx, buffer, count);
    ladybird_fd_release(object);
    return result;
}

ssize_t pread(int fd, void *buffer, size_t count, off_t offset) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);
    off_t original = 0;
    ssize_t result;

    if (object == NULL || object->ops == NULL || object->ops->seek == NULL || object->ops->read == NULL) {
        return set_errno_ssize(EBADF);
    }

    if (object->ops->seek(object->ctx, 0, SEEK_CUR, &original) != 0 ||
        object->ops->seek(object->ctx, offset, SEEK_SET, NULL) != 0) {
        ladybird_fd_release(object);
        return set_errno_ssize(EINVAL);
    }

    result = object->ops->read(object->ctx, buffer, count);
    (void)object->ops->seek(object->ctx, original, SEEK_SET, NULL);
    ladybird_fd_release(object);
    return result;
}

ssize_t pwrite(int fd, const void *buffer, size_t count, off_t offset) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);
    off_t original = 0;
    ssize_t result;

    if (object == NULL || object->ops == NULL || object->ops->seek == NULL || object->ops->write == NULL) {
        return set_errno_ssize(EBADF);
    }

    if (object->ops->seek(object->ctx, 0, SEEK_CUR, &original) != 0 ||
        object->ops->seek(object->ctx, offset, SEEK_SET, NULL) != 0) {
        ladybird_fd_release(object);
        return set_errno_ssize(EINVAL);
    }

    result = object->ops->write(object->ctx, buffer, count);
    (void)object->ops->seek(object->ctx, original, SEEK_SET, NULL);
    ladybird_fd_release(object);
    return result;
}

off_t lseek(int fd, off_t offset, int whence) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);
    off_t result = -1;

    if (object == NULL || object->ops == NULL || object->ops->seek == NULL) {
        return (off_t)set_errno(EBADF);
    }

    if (object->ops->seek(object->ctx, offset, whence, &result) != 0) {
        ladybird_fd_release(object);
        return -1;
    }

    ladybird_fd_release(object);
    return result;
}

int fsync(int fd) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);
    int result;

    if (object == NULL || object->ops == NULL || object->ops->fsync == NULL) {
        return set_errno(EBADF);
    }

    result = object->ops->fsync(object->ctx);
    ladybird_fd_release(object);
    return result;
}

int stat(const char *path, struct stat *st) {
    char normalized[LADYBIRD_MAX_PATH];

    ladybird_posix_init();
    normalize_path(path, normalized);
    return fill_stat_for_path(normalized, st);
}

int lstat(const char *path, struct stat *st) {
    return stat(path, st);
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

int access(const char *path, int mode) {
    char normalized[LADYBIRD_MAX_PATH];

    ladybird_posix_init();
    normalize_path(path, normalized);

    if (!path_exists_any(normalized)) {
        return set_errno(ENOENT);
    }

    if ((mode & W_OK) != 0 && fs_is_protected(normalized) && !user_is_admin()) {
        return set_errno(EACCES);
    }

    return 0;
}

int unlink(const char *path) {
    char normalized[LADYBIRD_MAX_PATH];

    ladybird_posix_init();
    normalize_path(path, normalized);

    if (!fs_exists(normalized)) {
        return set_errno(ENOENT);
    }

    if (fs_delete(normalized) != 0) {
        return set_errno(EIO);
    }

    return 0;
}

int rename(const char *old_path, const char *new_path) {
    char old_normalized[LADYBIRD_MAX_PATH];
    char new_normalized[LADYBIRD_MAX_PATH];
    void *data = NULL;
    size_t size = 0;

    ladybird_posix_init();
    normalize_path(old_path, old_normalized);
    normalize_path(new_path, new_normalized);

    if (path_is_directory(old_normalized) || path_is_directory(new_normalized)) {
        return set_errno(ENOTSUP);
    }

    if (fs_read(old_normalized, &data, &size) != 0) {
        return set_errno(ENOENT);
    }

    if (size > 0u && fs_write(new_normalized, data, size) != 0) {
        return set_errno(EIO);
    }

    if (fs_delete(old_normalized) != 0) {
        return set_errno(EIO);
    }

    return 0;
}

int mkdir(const char *path, uint32_t mode) {
    char normalized[LADYBIRD_MAX_PATH];
    char marker[LADYBIRD_MAX_PATH];
    char parent[LADYBIRD_MAX_PATH];
    uint8_t marker_byte = 1u;

    (void)mode;
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

    return 0;
}

int rmdir(const char *path) {
    char normalized[LADYBIRD_MAX_PATH];
    char marker[LADYBIRD_MAX_PATH];

    ladybird_posix_init();
    normalize_path(path, normalized);

    if (!path_is_directory(normalized) || string_equals(normalized, "/")) {
        return set_errno(ENOENT);
    }

    directory_marker_path(normalized, marker);
    (void)fs_delete(marker);
    (void)fs_delete_prefix(normalized);
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
    return (int)user_current_uid();
}

int isatty(int fd) {
    return (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO) ? 1 : 0;
}

int ioctl(int fd, unsigned long request, void *arg) {
    (void)fd;
    (void)request;
    (void)arg;
    return set_errno(ENOTSUP);
}

int fcntl(int fd, int command, int arg) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(fd);
    ladybird_file_ctx_t *file;

    if (object == NULL) {
        return set_errno(EBADF);
    }

    file = (ladybird_file_ctx_t *)object->ctx;
    switch (command) {
        case F_GETFL:
            if (file == NULL) {
                ladybird_fd_release(object);
                return set_errno(EBADF);
            }
            arg = file->flags;
            ladybird_fd_release(object);
            return arg;
        case F_SETFL:
            if (file != NULL) {
                file->flags = (file->flags & ~(O_APPEND | O_NONBLOCK)) | (arg & (O_APPEND | O_NONBLOCK));
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
    uint32_t *shared_refs;
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

    shared_refs = kmalloc(sizeof(*shared_refs));
    read_ctx = kmalloc(sizeof(*read_ctx));
    write_ctx = kmalloc(sizeof(*write_ctx));
    if (shared_refs == NULL || read_ctx == NULL || write_ctx == NULL) {
        pipe_close(pipe_id);
        kfree(shared_refs);
        kfree(read_ctx);
        kfree(write_ctx);
        return set_errno(ENOMEM);
    }

    *shared_refs = 2u;
    memset(read_ctx, 0, sizeof(*read_ctx));
    memset(write_ctx, 0, sizeof(*write_ctx));
    read_ctx->pipe_id = pipe_id;
    read_ctx->readable = 1u;
    read_ctx->shared_refs = shared_refs;
    write_ctx->pipe_id = pipe_id;
    write_ctx->writable = 1u;
    write_ctx->shared_refs = shared_refs;

    read_object = ladybird_fd_object_create(read_ctx, &pipe_ops);
    write_object = ladybird_fd_object_create(write_ctx, &pipe_ops);
    if (read_object == NULL || write_object == NULL) {
        if (read_object != NULL) {
            ladybird_fd_release(read_object);
        }
        if (write_object != NULL) {
            ladybird_fd_release(write_object);
        }
        return set_errno(ENOMEM);
    }

    pipefd[0] = ladybird_fd_install(read_object, -1);
    if (pipefd[0] < 0) {
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
    return 0;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    uint8_t *mapping;

    (void)addr;
    ladybird_posix_init();

    if (length == 0u) {
        ladybird_errno_value = EINVAL;
        return MAP_FAILED;
    }

    mapping = kmalloc(length);
    if (mapping == NULL) {
        ladybird_errno_value = ENOMEM;
        return MAP_FAILED;
    }

    memset(mapping, 0, length);
    if ((flags & MAP_ANONYMOUS) == 0 && fd >= 0) {
        if (pread(fd, mapping, length, offset) < 0) {
            kfree(mapping);
            return MAP_FAILED;
        }
    }

    for (size_t i = 0; i < LADYBIRD_MAX_MAPPINGS; ++i) {
        if (!ladybird_mappings[i].used) {
            ladybird_mappings[i].addr = mapping;
            ladybird_mappings[i].length = length;
            ladybird_mappings[i].prot = prot;
            ladybird_mappings[i].flags = flags;
            ladybird_mappings[i].used = 1;
            return mapping;
        }
    }

    kfree(mapping);
    ladybird_errno_value = ENOMEM;
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

    (void)ethernet_poll();

    for (nfds_t i = 0; i < count; ++i) {
        ladybird_fd_object_t *object = ladybird_fd_acquire(fds[i].fd);
        fds[i].revents = 0;
        if (object == NULL || object->ops == NULL || object->ops->poll == NULL) {
            fds[i].revents = POLLERR;
            ++ready;
            continue;
        }

        if (object->ops->poll(object->ctx, fds[i].events, &fds[i].revents) == 0 && fds[i].revents != 0) {
            ++ready;
        }
        ladybird_fd_release(object);
    }

    return ready;
}

int poll(struct pollfd *fds, nfds_t count, int timeout) {
    uint64_t deadline = 0u;

    ladybird_posix_init();
    if (fds == NULL && count != 0) {
        return set_errno(EINVAL);
    }

    if (timeout > 0) {
        deadline = monotonic_ms() + (uint64_t)timeout;
    }

    for (;;) {
        const int ready = poll_once(fds, count);
        if (ready != 0 || timeout == 0) {
            return ready;
        }
        if (timeout > 0 && monotonic_ms() >= deadline) {
            return 0;
        }

        (void)ethernet_poll();
        process_sleep_ticks(1u);
    }
}

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
        return 0;
    }
    if (process_kill((uint32_t)pid) != 0) {
        return set_errno(ESRCH);
    }
    return 0;
}

int sigaction(int sig, const struct sigaction *action, struct sigaction *old_action) {
    (void)sig;
    (void)action;
    (void)old_action;
    return set_errno(ENOSYS);
}

int gettimeofday(struct timeval *tv, void *tz) {
    const uint64_t ms = monotonic_ms();

    (void)tz;
    if (tv == NULL) {
        return set_errno(EINVAL);
    }

    tv->tv_sec = (time_t)(ms / 1000ull);
    tv->tv_usec = (long)((ms % 1000ull) * 1000ull);
    return 0;
}

int clock_gettime(int clock_id, struct timespec *ts) {
    const uint64_t ms = monotonic_ms();

    (void)clock_id;
    if (ts == NULL) {
        return set_errno(EINVAL);
    }

    ts->tv_sec = (time_t)(ms / 1000ull);
    ts->tv_nsec = (long)((ms % 1000ull) * 1000000ull);
    return 0;
}

int nanosleep(const struct timespec *requested, struct timespec *remaining) {
    uint64_t ms;

    if (requested == NULL || requested->tv_sec < 0 || requested->tv_nsec < 0 || requested->tv_nsec >= 1000000000L) {
        return set_errno(EINVAL);
    }

    if (remaining != NULL) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }

    ms = (uint64_t)requested->tv_sec * 1000ull + ((uint64_t)requested->tv_nsec + 999999ull) / 1000000ull;
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

int ftruncate(int fd, off_t length) {
    if (length < 0) {
        return set_errno(EINVAL);
    }

    if (lseek(fd, length, SEEK_SET) < 0) {
        return -1;
    }

    return fsync(fd);
}

int chmod(const char *path, uint32_t mode) {
    (void)path;
    (void)mode;
    return set_errno(ENOSYS);
}

ssize_t readlink(const char *path, char *buffer, size_t size) {
    (void)path;
    (void)buffer;
    (void)size;
    return set_errno(ENOSYS);
}

int symlink(const char *target, const char *link_path) {
    (void)target;
    (void)link_path;
    return set_errno(ENOSYS);
}

char *realpath(const char *path, char *resolved_path) {
    char normalized[LADYBIRD_MAX_PATH];

    if (path == NULL || resolved_path == NULL) {
        ladybird_errno_value = EINVAL;
        return NULL;
    }

    normalize_path(path, normalized);
    copy_string(resolved_path, LADYBIRD_MAX_PATH, normalized);
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
        return 1;
    }

    return -1;
}

int uname(void *buffer) {
    struct utsname *name = buffer;

    if (name == NULL) {
        return set_errno(EINVAL);
    }

    copy_string(name->sysname, sizeof(name->sysname), "VexOS");
    copy_string(name->nodename, sizeof(name->nodename), "vex");
    copy_string(name->release, sizeof(name->release), "0.1");
    copy_string(name->version, sizeof(name->version), "VexOS kernel");
    copy_string(name->machine, sizeof(name->machine), "x86_64");
    return 0;
}

int getrandom(void *buffer, size_t length, unsigned int flags) {
    uint8_t *bytes = buffer;

    (void)flags;
    if (buffer == NULL) {
        return set_errno(EINVAL);
    }

    uint64_t state = monotonic_ms() ^ ((uint64_t)(uintptr_t)buffer << 17u) ^ 0x9e3779b97f4a7c15ull;
    for (size_t i = 0; i < length; ++i) {
        state ^= state << 13u;
        state ^= state >> 7u;
        state ^= state << 17u;
        bytes[i] = (uint8_t)(state >> 24u);
    }

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
