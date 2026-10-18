/* Native Vex system interface used inside the Vex C library.
 *
 * Every OS service of the library goes through these calls, in the tagged
 * native domain (unos/syscall_wire.h). There is no Linux system call layer:
 * a musl source file that still uses __syscall() does not build.
 *
 * Error conventions of the native ABI:
 *  - historical calls return (uint64_t)-1 on failure and record the errno in
 *    the thread's error slot, read back with VEX_SYS_LAST_ERROR;
 *  - calls numbered after the frozen interval (vex_thread_abi.h,
 *    vex_file_abi.h, ...) return a negated errno directly. */
#ifndef VEX_LIBC_NATIVE_H
#define VEX_LIBC_NATIVE_H

#include <stdint.h>
#include <stddef.h>
#include <errno.h>

#ifndef VEX_HOSTED_BUILD
#define VEX_HOSTED_BUILD 1
#endif
#include <unos/syscall_wire.h>
#include <unos/vex_abi.h>

#ifndef hidden
#define hidden __attribute__((__visibility__("hidden")))
#endif

static inline uint64_t __vex_call3(uint64_t n, uint64_t a, uint64_t b, uint64_t c)
{
	uint64_t r;
	__asm__ __volatile__ ("syscall"
		: "=a"(r)
		: "a"(VEX_SYSCALL_NATIVE_TAG | n), "D"(a), "S"(b), "d"(c)
		: "rcx", "r11", "memory");
	return r;
}

static inline uint64_t __vex_call0(uint64_t n) { return __vex_call3(n, 0, 0, 0); }
static inline uint64_t __vex_call1(uint64_t n, uint64_t a) { return __vex_call3(n, a, 0, 0); }
static inline uint64_t __vex_call2(uint64_t n, uint64_t a, uint64_t b) { return __vex_call3(n, a, b, 0); }

#define __vex_ptr(p) ((uint64_t)(uintptr_t)(p))

/* The errno of the last failing historical call of this thread. */
hidden int __vex_last_error(void);

/* Historical call result -> POSIX result: on failure, errno is set and -1
 * returned. */
static inline long __vex_legacy_ret(uint64_t r)
{
	if (r == (uint64_t)-1) {
		errno = __vex_last_error();
		return -1;
	}
	return (long)r;
}

/* Negated-errno result -> POSIX result. */
static inline long __vex_ret(uint64_t r)
{
	if (r > (uint64_t)-4096) {
		errno = (int)-(int64_t)r;
		return -1;
	}
	return (long)r;
}

/* Thread and synchronisation primitives (src/vex/thread.c). */
hidden int __vex_futex_wait(volatile void *addr, int value, uint64_t timeout_ns);
hidden int __vex_futex_wake(volatile void *addr, int count);
hidden int __vex_set_tls(void *base);
hidden int __vex_thread_self(void);
hidden int __vex_thread_start(void (*entry)(void *), void *arg, void *stack_top,
                              void *tls, volatile int *tid_out,
                              volatile int *clear_on_exit);
_Noreturn hidden void __vex_thread_exit(int status);
_Noreturn hidden void __vex_thread_exit_unmap(void *base, size_t length);

/* Memory (src/vex/mman.c). */
hidden void *__vex_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
hidden int __vex_munmap(void *addr, size_t length);

/* Native events behind POSIX signals (src/vex/signal.c). */
hidden void __vex_signal_block_all(unsigned long *old);
hidden void __vex_signal_restore(const unsigned long *old);

#endif
