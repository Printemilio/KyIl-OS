#ifndef UNOS_USERSPACE_SYSCALL_H
#define UNOS_USERSPACE_SYSCALL_H

#include <stddef.h>
#include <stdint.h>
#include <unos/vex_abi.h>

#define SYS_WRITE 1ull
#define SYS_EXIT  2ull
#define SYS_READ  3ull

static inline uint64_t unos_syscall3(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int unos_write(const char *str) {
    return (int)unos_syscall3(SYS_WRITE, (uint64_t)str, 0, 0);
}

static inline void unos_exit(void) {
    (void)unos_syscall3(SYS_EXIT, 0, 0, 0);
    for (;;) {
    }
}

static inline int unos_read(char *buffer, size_t capacity) {
    return (int)unos_syscall3(SYS_READ, (uint64_t)buffer, (uint64_t)capacity, 0);
}

#endif
