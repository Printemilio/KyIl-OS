#ifndef UNOS_CVEX_H
#define UNOS_CVEX_H

#include <stdint.h>
#include <unos/vex_abi.h>

static inline uint64_t cvex_syscall3(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    __asm__ volatile (
        "syscall"
        : "+a"(number)
        : "D"(arg1), "S"(arg2), "d"(arg3)
        : "rcx", "r11", "memory"
    );

    return number;
}

static inline int cvex_bulk(uint64_t mask, vex_bulk_result_t *result) {
    vex_bulk_req_t req;

    req.mask = mask;
    req.result = result;
    return (int)cvex_syscall3(VEX_SYS_BULK, (uint64_t)(uintptr_t)&req, 0, 0);
}

#endif
