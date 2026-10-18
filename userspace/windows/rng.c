#include "windows.h"
/* ProcessPrng's valid-buffer contract is filled by KyIlOS's already seeded
 * native cryptographic RNG. No private generator, weak fallback or fake bytes.
 * https://learn.microsoft.com/en-us/windows/win32/seccng/processprng */
static int WR_WIN ProcessPrng(void *out,size_t size){
    if(size && (!out || (uintptr_t)out>UINTPTR_MAX-size)){wr_current_teb()->last_error=87;return 0;}
    uint8_t *bytes=out;
    while(size){size_t count=size>65536?65536:size;
        if(wr_random(bytes,count)){wr_current_teb()->last_error=1117;return 0;}
        bytes+=count;size-=count;
    }return 1;
}
void *wr_rng_exports(const char *name){
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(ProcessPrng);
#undef E
    return 0;
}
