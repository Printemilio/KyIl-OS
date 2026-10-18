__declspec(dllimport) void OutputDebugStringA(const char *);
__declspec(dllimport) unsigned TlsAlloc(void);
__declspec(dllimport) int TlsSetValue(unsigned,void *),TlsFree(unsigned);
__declspec(dllimport) void *TlsGetValue(unsigned);
#ifndef TLS_VALUE
#define TLS_VALUE 101
#endif
__declspec(thread) static unsigned value=TLS_VALUE;
__declspec(thread) static unsigned callback_stage;
static volatile unsigned attached,detached;
#include "tls_support.h"
static void tls_callback(void *base,unsigned reason,void *reserved) {
    (void)base;(void)reserved;
    if(reason==1 || reason==2) {
        if(value!=TLS_VALUE) {OutputDebugStringA("windows-tls-dll: template FAIL\n");__builtin_trap();}
        /* Real re-entry while the Windows loader owns its serialization gate. */
        unsigned slot=TlsAlloc();
        if(slot==0xffffffffu || !TlsSetValue(slot,(void *)base) || TlsGetValue(slot)!=base || !TlsFree(slot)) {
            OutputDebugStringA("windows-tls-dll: callback loader re-entry FAIL\n");__builtin_trap();
        }
        callback_stage=reason;
    }
    if(reason==3)__atomic_fetch_add(&detached,1,__ATOMIC_SEQ_CST);
}
int DllMain(void *base,unsigned reason,void *reserved) {
    (void)base;(void)reserved;
    if(reason==1 || reason==2) {
        if(callback_stage!=reason)return 0;
        if(reason==2)__atomic_fetch_add(&attached,1,__ATOMIC_SEQ_CST);
    }
    return 1;
}
#ifdef TLS_SECOND
__declspec(dllexport) unsigned DllTwo(unsigned id) {unsigned old=value;value+=id;return old;}
__declspec(dllexport) unsigned DllTwoCounts(void) {return attached*65536+detached;}
#else
__declspec(dllexport) unsigned DllOne(unsigned id) {unsigned old=value;value+=id;return old;}
__declspec(dllexport) unsigned DllOneCounts(void) {return attached*65536+detached;}
#endif
