#ifndef UNOS_VEX_SYSCALL_H
#define UNOS_VEX_SYSCALL_H

#include <stdint.h>

uint64_t vex_syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3);
void vex_jit_reap_pid(uint32_t pid, uint64_t cr3);

#endif
