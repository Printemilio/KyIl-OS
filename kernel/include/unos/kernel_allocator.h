#ifndef UNOS_KERNEL_ALLOCATOR_H
#define UNOS_KERNEL_ALLOCATOR_H
#include <stddef.h>
#include <stdint.h>
/* Raw mutable kernel storage. Owner is accounting/locality, never lifetime. */
void kernel_allocator_init(void);
int kernel_allocator_active(void);
void *kernel_allocator_alloc(uint32_t owner, size_t size);
void kernel_allocator_free(void *ptr);
void *kernel_allocator_realloc(uint32_t owner, void *ptr, size_t size);
void kernel_allocator_reap(uint32_t owner);
int kernel_allocator_can_project(const void *ptr, size_t size);
int kernel_allocator_physical_page(const void *ptr, size_t offset, uint64_t *physical);
void kernel_allocator_memory_diagnostic(void);
void kernel_allocator_usage(uint32_t owner,uint64_t *total,uint64_t *owned,uint64_t *peak_owned);
#endif
