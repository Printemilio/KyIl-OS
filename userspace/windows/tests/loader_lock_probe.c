#include <stdint.h>
__declspec(dllimport) void *GetModuleHandleW(const uint16_t *);
typedef struct {void *debug;int32_t count,recursion;uintptr_t owner;void *semaphore;uintptr_t spin;} critical;
static unsigned attached,detached,failed;
static critical *loader(uintptr_t *tid){uintptr_t teb;__asm__ volatile("movq %%gs:0x30,%0":"=r"(teb));*tid=*(uintptr_t *)(teb+0x48);
    uintptr_t peb=*(uintptr_t *)(teb+0x60);return *(critical **)(peb+0x110);}
__declspec(dllexport) void ProbeCounts(unsigned *a,unsigned *d,unsigned *f){*a=__atomic_load_n(&attached,__ATOMIC_ACQUIRE);*d=__atomic_load_n(&detached,__ATOMIC_ACQUIRE);*f=__atomic_load_n(&failed,__ATOMIC_ACQUIRE);}
int DllMain(void *module,unsigned reason,void *reserved){(void)module;(void)reserved;uintptr_t tid;critical *c=loader(&tid);
    if(!c || __atomic_load_n(&c->owner,__ATOMIC_ACQUIRE)!=tid || c->recursion<=0){__atomic_store_n(&failed,1,__ATOMIC_RELEASE);return 0;}
    int before=c->recursion;if(!GetModuleHandleW(0) || c->recursion!=before || c->owner!=tid){__atomic_store_n(&failed,1,__ATOMIC_RELEASE);return 0;}
    if(reason==2)__atomic_fetch_add(&attached,1,__ATOMIC_RELEASE);if(reason==3)__atomic_fetch_add(&detached,1,__ATOMIC_RELEASE);return 1;
}
