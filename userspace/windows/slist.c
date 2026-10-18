#include "windows.h"
typedef struct {uint64_t low,high;} __attribute__((aligned(16))) slist_head;
typedef struct slist_entry {struct slist_entry *next;} slist_entry;
static unsigned cx16;
static void supported(slist_head *head) {
    if((uintptr_t)head&15)wr_exit(0xc000000du);
    unsigned state=__atomic_load_n(&cx16,__ATOMIC_ACQUIRE);
    if(!state){uint32_t a=1,b,c,d;__asm__ volatile("cpuid":"+a"(a),"=b"(b),"=c"(c),"=d"(d));state=(c&(1u<<13))?1:2;__atomic_store_n(&cx16,state,__ATOMIC_RELEASE);}
    /* No instruction unsupported by the CPU is ever executed. Software
     * locks cannot emulate an interoperable, lock-free 128-bit public header. */
    if(state!=1)wr_exit(0xc00000bbu);
}
static int exchange(slist_head *h,uint64_t *low,uint64_t *high,uint64_t replacement_low,uint64_t replacement_high) {
    unsigned char result;__asm__ volatile("lock cmpxchg16b %1;setz %0":"=q"(result),"+m"(*h),"+a"(*low),"+d"(*high):"b"(replacement_low),"c"(replacement_high):"memory","cc");return result;
}
static slist_head snapshot(slist_head *h){slist_head s={0};exchange(h,&s.low,&s.high,0,0);return s;}
static void WR_WIN InitializeSListHead(slist_head *h){if((uintptr_t)h&15)wr_exit(0xc000000du);h->low=h->high=0;}
static slist_entry *WR_WIN InterlockedFlushSList(slist_head *h) {
    supported(h);slist_head old=snapshot(h);
    while(!exchange(h,&old.low,&old.high,old.low&~65535ull,old.high&15)){}
    return (void *)(uintptr_t)(old.high&~15ull);
}
static slist_entry *WR_WIN InterlockedPushEntrySList(slist_head *h,slist_entry *entry) {
    supported(h);if((uintptr_t)entry&15)wr_exit(0xc000000du);slist_head old=snapshot(h);
    for(;;){entry->next=(void *)(uintptr_t)(old.high&~15ull);
        uint64_t low=((old.low+0x20000ull)&~65535ull)|((old.low+1)&65535);
        if(exchange(h,&old.low,&old.high,low,(uintptr_t)entry|(old.high&15)))return (void *)(uintptr_t)(old.high&~15ull);}
}
static slist_entry *WR_WIN InterlockedPopEntrySList(slist_head *h) {
    supported(h);slist_head old=snapshot(h);
    for(;;){slist_entry *entry=(void *)(uintptr_t)(old.high&~15ull);if(!entry)return 0;slist_entry *next;
        /* Another pop may free/unmap this node before we inspect Next. A safe
         * failed read retries only if the public head really changed. */
        if(wr_memory_copy(&next,entry,sizeof(next))){slist_head fresh=snapshot(h);
            if(fresh.low==old.low && fresh.high==old.high)wr_exit(0xc0000005u);old=fresh;continue;}
        if((uintptr_t)next&15)wr_exit(0xc000000du);
        uint64_t low=(old.low&~65535ull)|((old.low-1)&65535);
        if(exchange(h,&old.low,&old.high,low,(uintptr_t)next|(old.high&15)))return entry;}
}
static unsigned short WR_WIN QueryDepthSList(slist_head *h){if((uintptr_t)h&15)wr_exit(0xc000000du);return __atomic_load_n((uint16_t *)&h->low,__ATOMIC_ACQUIRE);}
void *wr_slist_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(InitializeSListHead);E(InterlockedFlushSList);E(InterlockedPushEntrySList);E(InterlockedPopEntrySList);E(QueryDepthSList);
#undef E
    return 0;
}
