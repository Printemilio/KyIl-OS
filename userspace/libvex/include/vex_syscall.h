#ifndef VEX_USERSPACE_SYSCALL_H
#define VEX_USERSPACE_SYSCALL_H

#include <stdint.h>
#include <unos/syscall_wire.h>

static inline uint64_t vex_legacy_syscall0(uint64_t number) {
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(number)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline uint64_t vex_legacy_syscall1(uint64_t number, uint64_t arg1) {
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline uint64_t vex_legacy_syscall2(uint64_t number, uint64_t arg1, uint64_t arg2) {
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg1), "S"(arg2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline uint64_t vex_legacy_syscall3(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* Do not mask a malformed number into a valid native call. */
static inline uint64_t vex_syscall0(uint64_t number) {
    if (number > VEX_SYSCALL_NUMBER_MASK) return (uint64_t)-1;
    return vex_legacy_syscall0(VEX_SYSCALL_NATIVE_TAG | number);
}
static inline uint64_t vex_syscall1(uint64_t number, uint64_t arg1) {
    if (number > VEX_SYSCALL_NUMBER_MASK) return (uint64_t)-1;
    return vex_legacy_syscall1(VEX_SYSCALL_NATIVE_TAG | number, arg1);
}
static inline uint64_t vex_syscall2(uint64_t number, uint64_t arg1, uint64_t arg2) {
    if (number > VEX_SYSCALL_NUMBER_MASK) return (uint64_t)-1;
    return vex_legacy_syscall2(VEX_SYSCALL_NATIVE_TAG | number, arg1, arg2);
}
static inline uint64_t vex_syscall3(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    if (number > VEX_SYSCALL_NUMBER_MASK) return (uint64_t)-1;
    return vex_legacy_syscall3(VEX_SYSCALL_NATIVE_TAG | number, arg1, arg2, arg3);
}

#endif
