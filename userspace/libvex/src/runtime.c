#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>
#include <unos/vex_abi.h>

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

int abort(void) {
    exit(1);
    return 0;
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

int vex_gpu_present(vex_gpu_present_req_t *req) {
    if (req == 0) {
        return -1;
    }
    return (int)vex_syscall1(VEX_SYS_GPU_PRESENT, (uint64_t)req);
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
