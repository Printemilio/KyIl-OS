#ifndef KYIL_WINDOWS_HANDLE_VALUE_H
#define KYIL_WINDOWS_HANDLE_VALUE_H
#include <stdint.h>
/* Windows kernel-object handles must survive signed LONG serialization even
   in an AMD64 PE. Native capabilities remain separate, full native objects.
   Positive 31-bit values: type[30:24], generation[23:10], slot[9:2], zero[1:0].
   A slot is retired after its last generation, never silently wrapped (ABA).
   Heap/module pointers and pseudo/predefined handles are separate contracts. */
#define WR_HANDLE_GENERATION_MAX 0x3fffu
static inline uintptr_t wr_handle_pack(unsigned type,uint32_t generation,unsigned slot){
    return type<0x80 && generation && generation<=WR_HANDLE_GENERATION_MAX && slot<256?
        ((uintptr_t)type<<24)|((uintptr_t)generation<<10)|((uintptr_t)slot<<2):0;
}
static inline unsigned wr_handle_tag(uintptr_t h){
    return h<=INT32_MAX && !(h&3) && ((h>>10)&WR_HANDLE_GENERATION_MAX)?(unsigned)(h>>24):0;
}
static inline unsigned wr_handle_slot(uintptr_t h){return (unsigned)((h>>2)&255);}
static inline uint32_t wr_handle_generation(uintptr_t h){return (uint32_t)((h>>10)&WR_HANDLE_GENERATION_MAX);}
#endif
