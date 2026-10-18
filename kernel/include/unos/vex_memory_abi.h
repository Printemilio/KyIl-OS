#ifndef VEX_MEMORY_ABI_H
#define VEX_MEMORY_ABI_H
#include <stddef.h>

#define VEX_SYS_MMAP 30ull
#define VEX_SYS_MUNMAP 31ull
#define VEX_SYS_MPROTECT 32ull
#define VEX_SYS_MADVISE 33ull

typedef struct vex_memory_req {
    void *addr;
    size_t length;
    int prot_or_advice;
} vex_memory_req_t;
#endif
