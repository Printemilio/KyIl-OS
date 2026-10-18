#ifndef UNOS_HEAP_H
#define UNOS_HEAP_H

#include <stddef.h>
#include <stdint.h>

void heap_init(void);
void *kmalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t alignment);
void kfree(void *ptr);
void kfree_aligned(void *ptr);
void *krealloc(void *ptr, size_t size);

uint64_t heap_bootstrap_bytes(void);
#endif
