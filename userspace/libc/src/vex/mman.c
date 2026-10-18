/* Memory mappings on native Vex calls. */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include "syscall.h"

static void dummy(void) { }
weak_alias(dummy, __vm_wait);

void *__vex_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset)
{
	vex_mmap_req_t req = {
		.addr = addr,
		.length = length,
		.prot = prot,
		.flags = flags,
		.fd = fd,
		.offset = offset,
	};
	uint64_t r = __vex_call1(VEX_SYS_MMAP, __vex_ptr(&req));
	if (r == (uint64_t)-1) {
		errno = __vex_last_error();
		return MAP_FAILED;
	}
	return (void *)(uintptr_t)r;
}

void *__mmap(void *start, size_t len, int prot, int flags, int fd, off_t off)
{
	if (off & (PAGE_SIZE - 1) || off < 0) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (!len) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (len >= PTRDIFF_MAX) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	if (flags & MAP_FIXED) __vm_wait();
	return __vex_mmap(start, len, prot, flags, fd, off);
}
weak_alias(__mmap, mmap);

static int memory_op(uint64_t n, void *addr, size_t len, int value)
{
	vex_memory_req_t req = { .addr = addr, .length = len, .prot_or_advice = value };
	return (int)__vex_legacy_ret(__vex_call1(n, __vex_ptr(&req)));
}

int __vex_munmap(void *addr, size_t len)
{
	return memory_op(VEX_SYS_MUNMAP, addr, len, 0);
}

int __munmap(void *addr, size_t len)
{
	__vm_wait();
	return __vex_munmap(addr, len);
}
weak_alias(__munmap, munmap);

int __mprotect(void *addr, size_t len, int prot)
{
	uintptr_t start = (uintptr_t)addr & -PAGE_SIZE;
	uintptr_t end = (uintptr_t)((char *)addr + len + PAGE_SIZE - 1) & -PAGE_SIZE;
	return memory_op(VEX_SYS_MPROTECT, (void *)start, end - start, prot);
}
weak_alias(__mprotect, mprotect);

int __madvise(void *addr, size_t len, int advice)
{
	return memory_op(VEX_SYS_MADVISE, addr, len, advice);
}
weak_alias(__madvise, madvise);

int posix_madvise(void *addr, size_t len, int advice)
{
	if (advice == MADV_DONTNEED) return 0;
	return __madvise(addr, len, advice) ? errno : 0;
}

/* Moving a mapping is not a native operation; callers (mallocng, realloc)
 * fall back to allocate-and-copy. */
void *__mremap(void *old, size_t old_len, size_t new_len, int flags, ...)
{
	(void)old; (void)old_len; (void)new_len; (void)flags;
	errno = ENOSYS;
	return MAP_FAILED;
}
weak_alias(__mremap, mremap);

/* Every mapping is memory resident on Vex (there is no paging to disk), so
 * the locking calls only validate their arguments. */
int mlock(const void *addr, size_t len) { (void)addr; (void)len; return 0; }
int munlock(const void *addr, size_t len) { (void)addr; (void)len; return 0; }
int mlockall(int flags)
{
	if (!flags || flags & ~(MCL_CURRENT | MCL_FUTURE | MCL_ONFAULT)) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}
int munlockall(void) { return 0; }
int mlock2(const void *addr, size_t len, unsigned flags)
{
	if (flags & ~MLOCK_ONFAULT) {
		errno = EINVAL;
		return -1;
	}
	return mlock(addr, len);
}

/* MAP_SHARED file pages are written back when the descriptor is flushed;
 * there is no separate writeback to request. */
int msync(void *addr, size_t len, int flags)
{
	(void)len;
	if (((uintptr_t)addr & (PAGE_SIZE - 1)) ||
	    (flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE)) ||
	    ((flags & MS_ASYNC) && (flags & MS_SYNC))) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}
