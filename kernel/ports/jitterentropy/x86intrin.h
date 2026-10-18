#ifndef KYIL_JENT_X86INTRIN_H
#define KYIL_JENT_X86INTRIN_H
#include <stdint.h>
/* Freestanding equivalent of the single compiler intrinsic upstream uses. */
static inline uint64_t __rdtsc(void) {
    uint32_t low,high;__asm__ volatile("rdtsc":"=a"(low),"=d"(high));
    return ((uint64_t)high<<32u)|low;
}
static inline void _mm_pause(void) {__asm__ volatile("pause");}
#endif
