#ifndef UNOS_RANDOM_H
#define UNOS_RANDOM_H

#include <stddef.h>
#include <stdint.h>

uint64_t kernel_random_u64(void);
void kernel_random_fill(void *buffer, size_t length);
void kernel_random_cpu_init(void);
int kernel_random_init(void);
int kernel_random_try_fill(void *buffer, size_t length);
const char *kernel_random_provider(void);

#endif
