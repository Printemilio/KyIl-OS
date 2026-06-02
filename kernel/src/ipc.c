#include <unos/ipc.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/mrac.h>
#include <unos/heap.h>

#define IPC_PID 0xffffffe0u
#define IPC_PIPE_COUNT 16u
#define IPC_PIPE_SIZE 4096u
#define FNV1A_OFFSET_BASIS 14695981039346656037ull
#define FNV1A_PRIME 1099511628211ull

typedef struct {
    int used;
    uint8_t *buffer;
    size_t read_pos;
    size_t write_pos;
    size_t size;
} ipc_pipe_t;

static ipc_pipe_t pipes[IPC_PIPE_COUNT];
static int ipc_ready;

static uint64_t fnv1a64_string(const char *str) {
    uint64_t hash = FNV1A_OFFSET_BASIS;

    for (size_t i = 0; str[i] != '\0'; ++i) {
        hash ^= (uint8_t)str[i];
        hash *= FNV1A_PRIME;
    }

    return hash;
}

void ipc_init(void) {
    for (size_t i = 0; i < IPC_PIPE_COUNT; ++i) {
        pipes[i].used = 0;
        pipes[i].buffer = NULL;
        pipes[i].read_pos = 0;
        pipes[i].write_pos = 0;
        pipes[i].size = 0;
    }

    ipc_ready = 1;
}

void *ipc_share(const char *key, void *data, size_t size) {
    if (!ipc_ready || key == NULL || key[0] == '\0' || data == NULL || size == 0) {
        return NULL;
    }

    return MRAC_store_with_hash(IPC_PID, fnv1a64_string(key), data, size);
}

void *ipc_get(const char *key) {
    if (!ipc_ready || key == NULL || key[0] == '\0') {
        return NULL;
    }

    return MRAC_lookup(IPC_PID, fnv1a64_string(key));
}

void *ipc_update(const char *key, void *new_data, size_t size) {
    if (!ipc_ready || key == NULL || key[0] == '\0' || new_data == NULL || size == 0) {
        return NULL;
    }

    return MRAC_modify_with_hash(IPC_PID, fnv1a64_string(key), new_data, size);
}

void ipc_release(const char *key) {
    if (!ipc_ready || key == NULL || key[0] == '\0') {
        return;
    }

    MRAC_release(IPC_PID, fnv1a64_string(key));
}

int pipe_create(void) {
    if (!ipc_ready) {
        return -1;
    }

    for (size_t i = 0; i < IPC_PIPE_COUNT; ++i) {
        if (pipes[i].used) {
            continue;
        }

        pipes[i].buffer = kmalloc(IPC_PIPE_SIZE);
        if (pipes[i].buffer == NULL) {
            return -1;
        }

        pipes[i].used = 1;
        pipes[i].read_pos = 0;
        pipes[i].write_pos = 0;
        pipes[i].size = 0;
        return (int)i;
    }

    return -1;
}

int pipe_write(int fd, const void *data, size_t size) {
    if (fd < 0 || (size_t)fd >= IPC_PIPE_COUNT || !pipes[fd].used || data == NULL) {
        return -1;
    }

    ipc_pipe_t *pipe = &pipes[fd];
    const uint8_t *bytes = data;
    size_t written = 0;

    while (written < size && pipe->size < IPC_PIPE_SIZE) {
        pipe->buffer[pipe->write_pos] = bytes[written];
        pipe->write_pos = (pipe->write_pos + 1u) % IPC_PIPE_SIZE;
        ++pipe->size;
        ++written;
    }

    return (int)written;
}

int pipe_read(int fd, void *buffer, size_t size) {
    if (fd < 0 || (size_t)fd >= IPC_PIPE_COUNT || !pipes[fd].used || buffer == NULL) {
        return -1;
    }

    ipc_pipe_t *pipe = &pipes[fd];
    uint8_t *bytes = buffer;
    size_t read = 0;

    while (read < size && pipe->size > 0) {
        bytes[read] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1u) % IPC_PIPE_SIZE;
        --pipe->size;
        ++read;
    }

    return (int)read;
}

size_t pipe_readable(int fd) {
    if (fd < 0 || (size_t)fd >= IPC_PIPE_COUNT || !pipes[fd].used) {
        return 0u;
    }

    return pipes[fd].size;
}

size_t pipe_writable(int fd) {
    if (fd < 0 || (size_t)fd >= IPC_PIPE_COUNT || !pipes[fd].used) {
        return 0u;
    }

    return IPC_PIPE_SIZE - pipes[fd].size;
}

void pipe_close(int fd) {
    if (fd < 0 || (size_t)fd >= IPC_PIPE_COUNT || !pipes[fd].used) {
        return;
    }

    kfree(pipes[fd].buffer);
    pipes[fd].buffer = NULL;
    pipes[fd].read_pos = 0;
    pipes[fd].write_pos = 0;
    pipes[fd].size = 0;
    pipes[fd].used = 0;
}
