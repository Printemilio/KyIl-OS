#ifndef VEX_USERSPACE_SHM_ABI_H
#define VEX_USERSPACE_SHM_ABI_H

#include <stddef.h>
#include <stdint.h>

/* Stable native VexOS syscall wire values.  This header intentionally has no
 * dependency on vex_legacy_compat.h, so a glibc/Linux WebKit process can use
 * the SHM bridge without redefining hosted POSIX types. */
/* Logical native numbers, selected explicitly by vex_syscall.h's wire domain.
 * They never enter the untagged Linux number space. Existing binaries using
 * the old 0x4001..0x4003 Linux bridge remain supported by compat_linux. */
#define VEX_SYS_SHM_OPEN  161ull
#define VEX_SYS_SHM_CLOSE 162ull
#define VEX_SYS_SHM_MAP   163ull

typedef struct vex_shm_open_req {
    const char *name;
    uint64_t handle;
} vex_shm_open_req_t;

typedef struct vex_shm_map_req {
    uint64_t handle;
    void *preferred_addr;
    size_t length;
    size_t offset;
    int prot;
    uint32_t flags;
    void *mapped_addr;
} vex_shm_map_req_t;

#endif
