#include "windows.h"
/* The WinMM clock is the low DWORD of the genuine monotonic millisecond
 * counter. It does not change scheduler resolution or impersonate a device. */
static unsigned WR_WIN timeGetTime(void){return (unsigned)wr_ticks();}
/* No WinMM MIDI/wave endpoint adapter is installed. This is the empty
 * Windows-device catalog, not a pretend audio device or completed buffer.
 * Native HDA access is not exposed through a fabricated WinMM handle. */
static unsigned WR_WIN midiInGetNumDevs(void){return 0;}
static unsigned WR_WIN midiOutGetNumDevs(void){return 0;}
static unsigned WR_WIN waveInGetNumDevs(void){return 0;}
static unsigned WR_WIN waveOutGetNumDevs(void){return 0;}
static unsigned WR_WIN midiInOpen(void **out,unsigned id,uintptr_t callback,uintptr_t instance,unsigned flags){
    (void)id;(void)callback;(void)instance;(void)flags;void *empty=0;
    if(!out || wr_memory_copy(out,&empty,sizeof(empty)))return 11;return 2;
}
static unsigned WR_WIN midiOutOpen(void **out,unsigned id,uintptr_t cb,uintptr_t instance,unsigned flags){return midiInOpen(out,id,cb,instance,flags);}
static unsigned WR_WIN waveOutOpen(void **out,unsigned id,const void *format,uintptr_t cb,uintptr_t instance,unsigned flags){
    (void)format;if(flags&1u)return 2;return midiInOpen(out,id,cb,instance,flags);
}
static unsigned WR_WIN midiInGetDevCapsW(uintptr_t id,void *caps,unsigned bytes){(void)id;(void)caps;(void)bytes;return 2;}
static unsigned WR_WIN midiOutGetDevCapsW(uintptr_t id,void *caps,unsigned bytes){return midiInGetDevCapsW(id,caps,bytes);}
#define HANDLE_ONLY(f) static unsigned WR_WIN f(void *h){(void)h;return 5;}
#define HEADER_OP(f) static unsigned WR_WIN f(void *h,void *header,unsigned bytes){(void)h;(void)header;(void)bytes;return 5;}
HANDLE_ONLY(midiInClose) HANDLE_ONLY(midiInReset) HANDLE_ONLY(midiInStart)
HANDLE_ONLY(midiOutClose) HANDLE_ONLY(midiOutReset)
HANDLE_ONLY(waveOutClose) HANDLE_ONLY(waveOutPause) HANDLE_ONLY(waveOutReset) HANDLE_ONLY(waveOutRestart)
HEADER_OP(midiInAddBuffer) HEADER_OP(midiInPrepareHeader) HEADER_OP(midiInUnprepareHeader)
HEADER_OP(midiOutLongMsg) HEADER_OP(midiOutPrepareHeader) HEADER_OP(midiOutUnprepareHeader)
HEADER_OP(waveOutPrepareHeader) HEADER_OP(waveOutUnprepareHeader) HEADER_OP(waveOutWrite)
#undef HANDLE_ONLY
#undef HEADER_OP
static unsigned WR_WIN midiOutShortMsg(void *h,unsigned message){(void)h;(void)message;return 5;}
/* Changing scheduler/timer resolution has no native contract yet. Never
 * claim a 1ms resolution merely because timeGetTime can report milliseconds. */
static unsigned WR_WIN timeBeginPeriod(unsigned period){(void)period;return 97;}
static unsigned WR_WIN timeEndPeriod(unsigned period){(void)period;return 97;}
void *wr_multimedia_exports(const char *name){
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(timeGetTime);
    E(midiInGetNumDevs);E(midiOutGetNumDevs);E(waveInGetNumDevs);E(waveOutGetNumDevs);
    E(midiInOpen);E(midiOutOpen);E(waveOutOpen);E(midiInGetDevCapsW);E(midiOutGetDevCapsW);
    E(midiInClose);E(midiInReset);E(midiInStart);E(midiOutClose);E(midiOutReset);
    E(waveOutClose);E(waveOutPause);E(waveOutReset);E(waveOutRestart);
    E(midiInAddBuffer);E(midiInPrepareHeader);E(midiInUnprepareHeader);
    E(midiOutLongMsg);E(midiOutPrepareHeader);E(midiOutUnprepareHeader);E(midiOutShortMsg);
    E(waveOutPrepareHeader);E(waveOutUnprepareHeader);E(waveOutWrite);E(timeBeginPeriod);E(timeEndPeriod);
#undef E
    return 0;
}
