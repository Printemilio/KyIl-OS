#ifndef UNOS_COMPAT_H
#define UNOS_COMPAT_H

#include <stdint.h>

#define VEX_ABI_NATIVE 0u
#define VEX_ABI_LINUX  1u
#define VEX_ABI_PE     2u

#define VEX_PE_SYSCALL_CALL 0x766578504543414cull
#define VEX_PE_THUNK_BASE 0x0000007000000000ull
#define VEX_PE_THUNK_SIZE 64ull

#define VEX_PE_GET_STD_HANDLE 1ull
#define VEX_PE_WRITE_FILE     2ull
#define VEX_PE_EXIT_PROCESS   3ull
#define VEX_PE_READ_FILE      4ull
#define VEX_PE_GET_LAST_ERROR 5ull
#define VEX_PE_SET_LAST_ERROR 6ull
#define VEX_PE_CREATE_FILE    7ull
#define VEX_PE_CLOSE_HANDLE   8ull
#define VEX_PE_VIRTUAL_ALLOC  9ull
#define VEX_PE_VIRTUAL_FREE   10ull
#define VEX_PE_CREATE_PROCESS 11ull
#define VEX_PE_WAIT_SINGLE    12ull
#define VEX_PE_CREATE_THREAD  13ull
#define VEX_PE_HEAP_ALLOC     14ull
#define VEX_PE_HEAP_FREE      15ull
#define VEX_PE_GET_MODULE     16ull
#define VEX_PE_LOAD_LIBRARY   17ull
#define VEX_PE_GET_PROC       18ull

#define VEX_PE_STATE_KEY 0x7665785045535441ull

uint64_t compat_linux_syscall(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);
uint64_t compat_pe_syscall(uint64_t id, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);
uint64_t compat_pe_call(const char *symbol, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);

#endif
