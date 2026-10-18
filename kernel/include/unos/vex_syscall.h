#ifndef UNOS_VEX_SYSCALL_H
#define UNOS_VEX_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

uint64_t vex_syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3);
/* Entry used only by the ring-3 SYSCALL path.  Kernel compatibility layers
   continue to use vex_syscall_dispatch with trusted kernel-side request
   records, while this entry applies the user-range gate before dispatch. */
uint64_t vex_syscall_dispatch_user(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3);
int vex_syscall_user_range_valid(uint64_t address, size_t length, int writable);
int vex_syscall_user_string_valid(uint64_t address, size_t max_length);
void vex_jit_reap_pid(uint32_t pid, uint64_t cr3);
/* Return 1 when a JIT fault was resolved, 0 when the address is not a JIT
   mapping, and -1 when it is a JIT mapping but the access/quota is invalid. */
int vex_jit_materialize_page(uint32_t pid, uint64_t cr3, uint64_t address,
                             uint64_t page_fault_error);
int vex_jit_clone_pid(uint32_t source_pid, uint32_t destination_pid);
void vex_jit_report_execution_samples(void);
void vex_shm_reap_pid(uint32_t pid);

#endif
