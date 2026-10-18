#ifndef KYIL_MEMORY_COPY_H
#define KYIL_MEMORY_COPY_H
#include <stddef.h>
#include <stdint.h>
/* Exact byte range, unaligned allowed on x86-64. No SIMD/FPU state or AVX/ERMS
 * requirement: this also runs on Core 2. memcpy still forbids overlap. */
static inline void *kyil_memory_copy(void *destination,const void *source,size_t size) {
    void *result=destination;
    if(size<16){uint8_t *d=destination;const uint8_t *s=source;for(size_t i=0;i<size;i++)d[i]=s[i];return result;}
    size_t words=size/8,tail=size%8;
    __asm__ volatile("cld;rep movsq":"+D"(destination),"+S"(source),"+c"(words)::"memory");
    __asm__ volatile("rep movsb":"+D"(destination),"+S"(source),"+c"(tail)::"memory");
    return result;
}
#endif
