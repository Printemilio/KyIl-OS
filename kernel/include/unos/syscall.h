#ifndef UNOS_SYSCALL_H
#define UNOS_SYSCALL_H

#include <stdint.h>
#include <unos/vex_abi.h>

typedef struct syscall_entry {
    uint64_t hash;
    uint8_t priority;
    void *handler;
} syscall_entry_t;

#define SYSCALL_PRIORITY_CRITICAL 0
#define SYSCALL_PRIORITY_SYSTEM   1
#define SYSCALL_PRIORITY_USER     2
#define SYSCALL_PRIORITY_LAMBDA   3

#define SYS_WRITE 1ull
#define SYS_EXIT  2ull
#define SYS_READ  3ull

void syscall_init(void);
void syscall_register(const char *name, void *handler, uint8_t priority);
void *syscall_lookup(uint64_t hash);
void syscall_dispatch(uint64_t hash, void *args);
uint64_t syscall_dispatch_raw(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7);

#endif
