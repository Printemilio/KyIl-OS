#ifndef UNOS_STDLIB_H
#define UNOS_STDLIB_H

#include <stddef.h>

void *calloc(size_t count, size_t size);
void free(void *ptr);
void abort(void);

#endif
