#ifndef VEX_JIT_ABI_H
#define VEX_JIT_ABI_H
#include <stddef.h>
#include <stdint.h>

/* Existing wire layout, shared with hosted engines without importing the
 * native libc compatibility declarations. Numbers require the native tag. */
#define VEX_SYS_JIT_ARENA_CREATE 126ull
#define VEX_SYS_JIT_ARENA_DESTROY 127ull
#define VEX_JIT_ARENA_FLAG_DISABLE_QUOTA (1u << 0)
#define VEX_JIT_ARENA_FLAG_DISABLE_LOCKS (1u << 1)
#define VEX_JIT_ARENA_FLAG_FULL_OPEN (1u << 2)

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

#endif
