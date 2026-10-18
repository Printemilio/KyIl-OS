#include "windows.h"
/* The WinMM clock is the low DWORD of the genuine monotonic millisecond
 * counter. It does not change scheduler resolution or impersonate a device. */
static unsigned WR_WIN timeGetTime(void){return (unsigned)wr_ticks();}
void *wr_multimedia_exports(const char *name){
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(timeGetTime);
#undef E
    return 0;
}
