#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>
#include <unos/vex_abi.h>

#include "vex_runtime.h"

#include "vex_syscall.h"

#define EOF (-1)

void* memset(void* dest, int value, size_t count) {
    unsigned char* bytes = (unsigned char*)dest;
    for (size_t i = 0; i < count; ++i) {
        bytes[i] = (unsigned char)value;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t count) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < count; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void* memmove(void* dest, const void* src, size_t count) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        for (size_t i = 0; i < count; ++i) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = count; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int memcmp(const void* left, const void* right, size_t count) {
    const unsigned char* l = (const unsigned char*)left;
    const unsigned char* r = (const unsigned char*)right;
    for (size_t i = 0; i < count; ++i) {
        if (l[i] != r[i]) {
            return (int)l[i] - (int)r[i];
        }
    }
    return 0;
}

size_t strlen(const char* text) {
    size_t length = 0;
    if (text == 0) {
        return 0;
    }
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

char* strcpy(char* dest, const char* src) {
    size_t i = 0;
    while (src[i] != '\0') {
        dest[i] = src[i];
        ++i;
    }
    dest[i] = '\0';
    return dest;
}

char* strncpy(char* dest, const char* src, size_t count) {
    size_t i = 0;
    for (; i < count && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
    for (; i < count; ++i) {
        dest[i] = '\0';
    }
    return dest;
}

int strcmp(const char* left, const char* right) {
    size_t i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return (int)(unsigned char)left[i] - (int)(unsigned char)right[i];
        }
        ++i;
    }
    return (int)(unsigned char)left[i] - (int)(unsigned char)right[i];
}

int strncmp(const char* left, const char* right, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (left[i] != right[i] || left[i] == '\0' || right[i] == '\0') {
            return (int)(unsigned char)left[i] - (int)(unsigned char)right[i];
        }
    }
    return 0;
}

char* strchr(const char* text, int ch) {
    const char target = (char)ch;
    if (text == 0) {
        return 0;
    }
    for (size_t i = 0;; ++i) {
        if (text[i] == target) {
            return (char*)(text + i);
        }
        if (text[i] == '\0') {
            return 0;
        }
    }
}

char* strrchr(const char* text, int ch) {
    char* last = 0;
    const char target = (char)ch;
    if (text == 0) {
        return 0;
    }
    for (size_t i = 0;; ++i) {
        if (text[i] == target) {
            last = (char*)(text + i);
        }
        if (text[i] == '\0') {
            return last;
        }
    }
}

char* strerror(int errnum) {
    (void)errnum;
    return (char*)"vex errno";
}

int puts(const char* text) {
    size_t length = strlen(text);
    if (write(STDOUT_FILENO, text, length) < 0) {
        return EOF;
    }
    if (write(STDOUT_FILENO, "\n", 1) < 0) {
        return EOF;
    }
    return 0;
}

int putchar(int ch) {
    char c = (char)ch;
    return write(STDOUT_FILENO, &c, 1) < 0 ? EOF : ch;
}

void abort(void) {
    exit(1);
}

int vex_mrac_put(uint64_t key_hash, const void* data, size_t size, uint32_t* version_out) {
    vex_mrac_put_req_t req;
    int result;

    req.key_hash = key_hash;
    req.data = data;
    req.size = size;
    req.version_out = 0u;
    result = (int)vex_syscall1(VEX_SYS_MRAC_PUT, (uint64_t)&req);
    if (result == 0 && version_out != 0) {
        *version_out = req.version_out;
    }
    return result;
}

int vex_mrac_get(uint64_t key_hash, uint32_t version, void* buffer, size_t buffer_size, size_t* size_out, uint32_t* resolved_version) {
    vex_mrac_get_req_t req;
    int result;

    req.key_hash = key_hash;
    req.version = version;
    req.buffer = buffer;
    req.buffer_size = buffer_size;
    req.size_out = 0u;
    req.resolved_version = 0u;
    result = (int)vex_syscall1(VEX_SYS_MRAC_GET, (uint64_t)&req);
    if (size_out != 0) {
        *size_out = req.size_out;
    }
    if (resolved_version != 0) {
        *resolved_version = req.resolved_version;
    }
    return result;
}

int vex_mrac_remove(uint64_t key_hash, uint32_t version, uint32_t flags) {
    vex_mrac_remove_req_t req;

    req.key_hash = key_hash;
    req.version = version;
    req.flags = flags;
    return (int)vex_syscall1(VEX_SYS_MRAC_REMOVE, (uint64_t)&req);
}

uint32_t vex_mrac_current_version(uint64_t key_hash) {
    return (uint32_t)vex_syscall1(VEX_SYS_MRAC_CURRENT_VERSION, key_hash);
}

size_t vex_mrac_version_count(uint64_t key_hash) {
    return (size_t)vex_syscall1(VEX_SYS_MRAC_VERSION_COUNT, key_hash);
}

int vex_mrac_version_info(uint64_t key_hash, size_t index, uint32_t* version_out, size_t* size_out, uint8_t* is_current_out) {
    vex_mrac_version_info_t info;
    vex_mrac_version_info_req_t req;
    int result;

    req.key_hash = key_hash;
    req.index = index;
    req.info = &info;
    result = (int)vex_syscall1(VEX_SYS_MRAC_VERSION_INFO, (uint64_t)&req);
    if (result != 0) {
        return result;
    }
    if (version_out != 0) {
        *version_out = info.version;
    }
    if (size_out != 0) {
        *size_out = info.size;
    }
    if (is_current_out != 0) {
        *is_current_out = info.is_current;
    }
    return 0;
}

int vex_directory_list(const char *path, vex_directory_entry_t *entries,
                       size_t capacity, size_t *count_out) {
    vex_directory_list_req_t req;
    int result;

    if (path == 0 || (capacity != 0u && entries == 0)) {
        return -1;
    }
    req.path = path;
    req.entries = entries;
    req.capacity = capacity;
    req.count = 0u;
    result = (int)vex_syscall1(VEX_SYS_DIRECTORY_LIST, (uint64_t)&req);
    if (count_out != 0) {
        *count_out = result == 0 ? req.count : 0u;
    }
    return result;
}

int vex_open_path(const char *path, uint8_t type) {
    vex_open_path_req_t req;

    if (path == 0 || path[0] == '\0') {
        return -1;
    }
    req.path = path;
    req.type = type;
    memset(req.reserved, 0, sizeof(req.reserved));
    return (int)vex_syscall1(VEX_SYS_OPEN_PATH, (uint64_t)&req);
}

int vex_open_context(uint32_t application, char *buffer, size_t size) {
    vex_open_context_req_t req;

    if (buffer == 0 || size == 0u) {
        return -1;
    }
    buffer[0] = '\0';
    req.application = application;
    req.reserved = 0u;
    req.buffer = buffer;
    req.size = size;
    return (int)vex_syscall1(VEX_SYS_OPEN_CONTEXT, (uint64_t)&req);
}

int vex_jit_arena_create(size_t size, uint32_t flags, uint64_t quota_bytes, void** rw_base_out, void** rx_base_out, uint32_t* arena_id_out, uint64_t* source_namespace_id_out) {
    vex_jit_arena_create_req_t req;
    int result;

    req.preferred_rw_addr = 0;
    req.preferred_rx_addr = 0;
    req.size = size;
    req.quota_bytes = quota_bytes;
    req.flags = flags;
    req.arena_id = 0u;
    req.rw_base = 0;
    req.rx_base = 0;
    req.source_namespace_id = 0u;
    result = (int)vex_syscall1(VEX_SYS_JIT_ARENA_CREATE, (uint64_t)&req);
    if (result != 0) {
        return result;
    }
    if (rw_base_out != 0) {
        *rw_base_out = req.rw_base;
    }
    if (rx_base_out != 0) {
        *rx_base_out = req.rx_base;
    }
    if (arena_id_out != 0) {
        *arena_id_out = req.arena_id;
    }
    if (source_namespace_id_out != 0) {
        *source_namespace_id_out = req.source_namespace_id;
    }
    return 0;
}

int vex_jit_arena_destroy(void* addr, uint32_t arena_id, uint32_t flags) {
    vex_jit_arena_destroy_req_t req;

    req.addr = addr;
    req.arena_id = arena_id;
    req.flags = flags;
    return (int)vex_syscall1(VEX_SYS_JIT_ARENA_DESTROY, (uint64_t)&req);
}

ssize_t vex_debug_log_read(char *buf, size_t len, uint32_t *seq) {
    if (buf == 0 || seq == 0) {
        return -1;
    }
    return (ssize_t)vex_syscall3(VEX_SYS_DEBUG_LOG_READ, (uint64_t)buf, (uint64_t)len, (uint64_t)seq);
}

int vex_mouse_poll(vex_mouse_packet_t *packet) {
    if (packet == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_MOUSE_POLL, (uint64_t)packet);
}

int vex_gpu_query_info(vex_gpu_info_t *info) {
    if (info == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_GPU_QUERY_INFO, (uint64_t)info);
}

int vex_gpu_query_caps(vex_gpu_caps_t *caps) {
    if (caps == 0) {
        return -1;
    }
    caps->struct_size = sizeof(*caps);
    return (int)vex_syscall1(VEX_SYS_GPU_QUERY_CAPS, (uint64_t)caps);
}

int vex_gpu_buffer_create(vex_gpu_buffer_create_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_GPU_BUFFER_CREATE, (uint64_t)req);
}

int vex_gpu_buffer_destroy(uint32_t handle) {
    return (int)vex_syscall1(VEX_SYS_GPU_BUFFER_DESTROY, (uint64_t)handle);
}

int vex_gpu_queue_submit(vex_gpu_submit_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_GPU_QUEUE_SUBMIT, (uint64_t)req);
}

uint64_t vex_gpu_queue_create(uint32_t family) {
    return vex_syscall1(VEX_SYS_GPU_QUEUE_CREATE, (uint64_t)family);
}

int vex_gpu_queue_destroy(uint64_t queue) {
    return (int)vex_syscall1(VEX_SYS_GPU_QUEUE_DESTROY, queue);
}

int vex_gpu_submit_begin(uint64_t queue, uint64_t *serial_out) {
    uint64_t serial;
    if (serial_out == NULL) return -1;
    serial = vex_syscall1(VEX_SYS_GPU_SUBMIT_BEGIN, queue);
    if ((int64_t)serial < 0) return -1;
    *serial_out = serial;
    return 0;
}

int vex_gpu_submit_complete(uint64_t queue, uint64_t serial, int32_t status) {
    return (int)vex_syscall3(VEX_SYS_GPU_SUBMIT_COMPLETE, queue, serial,
                             (uint64_t)(int64_t)status);
}

int vex_gpu_fence_query(uint64_t queue, uint64_t serial) {
    return (int)vex_syscall2(VEX_SYS_GPU_FENCE_QUERY, queue, serial);
}

int vex_gpu_fence_wait(uint64_t queue, uint64_t serial,
                       uint64_t timeout_ticks) {
    return (int)vex_syscall3(VEX_SYS_GPU_FENCE_WAIT, queue, serial,
                             timeout_ticks);
}

uint64_t vex_event_counter_create(uint64_t initial, uint32_t flags) {
    return vex_syscall2(VEX_SYS_EVENT_COUNTER_CREATE, initial, flags);
}

int vex_event_counter_signal(uint64_t capability, uint64_t value) {
    return (int)vex_syscall2(VEX_SYS_EVENT_COUNTER_SIGNAL, capability, value);
}

int vex_event_counter_read(uint64_t capability, uint64_t *value_out,
                           uint32_t flags, uint64_t timeout_ticks) {
    vex_event_counter_read_req_t req = {0};
    req.capability = capability;
    req.value_out = value_out;
    req.timeout_ticks = timeout_ticks;
    req.flags = flags;
    return (int)vex_syscall1(VEX_SYS_EVENT_COUNTER_READ,
                             (uint64_t)(uintptr_t)&req);
}

int vex_event_counter_release(uint64_t capability) {
    return (int)vex_syscall1(VEX_SYS_EVENT_COUNTER_RELEASE, capability);
}

typedef struct vex_thread_start_context {
    vex_thread_entry_t entry;
    void *arg;
} vex_thread_start_context_t;

static void vex_thread_context_free(vex_thread_start_context_t *context) {
    vex_heap_req_t request;
    if (context == NULL) return;
    request.ptr = context;
    request.size = sizeof(*context);
    (void)vex_syscall1(VEX_SYS_HEAP_FREE, (uint64_t)(uintptr_t)&request);
}

static void vex_thread_entry_bootstrap(void *opaque) {
    vex_thread_start_context_t *context =
        (vex_thread_start_context_t *)opaque;
    vex_thread_entry_t entry;
    void *arg;
    if (context == NULL || context->entry == NULL) vex_thread_exit(-1);
    entry = context->entry;
    arg = context->arg;
    vex_thread_context_free(context);
    entry(arg);
    vex_thread_exit(0);
}

int vex_thread_create(vex_thread_entry_t entry, void *arg, uint64_t tls_base,
                      uint32_t *tid_out) {
    vex_thread_start_context_t *context;
    vex_thread_create_req_t request;
    uint64_t result;
    if (entry == NULL || tid_out == NULL) return -1;
    *tid_out = 0u;
    context = (vex_thread_start_context_t *)(uintptr_t)
        vex_syscall1(VEX_SYS_HEAP_ALLOC, sizeof(*context));
    if (context == NULL || (int64_t)(uintptr_t)context < 0) return -1;
    context->entry = entry;
    context->arg = arg;
    request.entry = vex_thread_entry_bootstrap;
    request.arg = context;
    request.flags = 0u;
    request.tid = 0u;
    result = vex_syscall2(VEX_SYS_THREAD_CREATE,
        (uint64_t)(uintptr_t)&request, tls_base);
    if ((int64_t)result < 0 || request.tid == 0u) {
        vex_thread_context_free(context);
        return -1;
    }
    *tid_out = request.tid;
    return 0;
}

int vex_thread_join(uint32_t tid, int *status_out) {
    if (tid == 0u) return -1;
    return (int)vex_syscall2(VEX_SYS_THREAD_JOIN, tid,
        (uint64_t)(uintptr_t)status_out);
}

void vex_thread_exit(int status) {
    (void)vex_syscall1(VEX_SYS_THREAD_EXIT, (uint64_t)(int64_t)status);
    for (;;) (void)vex_syscall0(VEX_SYS_THREAD_YIELD);
}

int vex_gpu_present(vex_gpu_present_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_GPU_PRESENT, (uint64_t)req);
}

int vex_gpu_surface_create(vex_gpu_surface_create_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_GPU_SURFACE_CREATE, (uint64_t)req);
}

int vex_gpu_surface_destroy(uint64_t surface_handle) {
    return (int)vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,
                             (uint64_t)surface_handle);
}

int vex_gpu_surface_present(vex_gpu_surface_present_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_GPU_SURFACE_PRESENT, (uint64_t)req);
}

int vex_gpu_surface_wait(vex_gpu_surface_wait_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_GPU_SURFACE_WAIT, (uint64_t)req);
}

int vex_surface_map(vex_surface_map_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_SURFACE_MAP, (uint64_t)req);
}

int vex_surface_retain(uint64_t surface_handle) {
    return (int)vex_syscall1(VEX_SYS_SURFACE_RETAIN, surface_handle);
}

int vex_surface_query(vex_surface_query_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_SURFACE_QUERY, (uint64_t)req);
}

int vex_surface_unmap(vex_surface_unmap_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_SURFACE_UNMAP, (uint64_t)req);
}

int vex_message_pair(vex_message_pair_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_MESSAGE_PAIR, (uint64_t)req);
}

ssize_t vex_message_send_objects(vex_message_io_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (ssize_t)vex_syscall1(VEX_SYS_MESSAGE_SEND, (uint64_t)req);
}

ssize_t vex_message_receive_objects(vex_message_io_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (ssize_t)vex_syscall1(VEX_SYS_MESSAGE_RECV, (uint64_t)req);
}

void __stack_chk_fail(void) {
    abort();
}

long lround(double value) {
    return (long)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

long long llround(double value) {
    return (long long)(value >= 0.0 ? value + 0.5 : value - 0.5);
}
