/* Private userspace arenas; out-of-band metadata; recursive locks and registry
 * pins. A stale/destroyed heap can never refer to a subsequently created heap.
 * Executable heaps are refused: Windows policy cannot weaken native W^X. */
#include "windows.h"
#include "exception_abi.h"
#include "wer.h"
typedef struct piece {struct piece *next;uint8_t *base;size_t length,committed,used;} piece;
typedef struct block {struct block *next;piece *arena;size_t offset,span,size;unsigned busy;} block;
typedef struct {wr_mutex lock;uint32_t owner,depth;piece *pieces;block *blocks;uint64_t secret;unsigned initialized,fixed;} heap_state;
static heap_state heap;
static struct {heap_state *value;uint32_t generation,pins;} slots[256];
static wr_mutex registry;
static unsigned terminate_on_corruption;
static int fail(unsigned e){wr_current_teb()->last_error=e;return 0;}
static void enter(heap_state *h){
    unsigned self=(unsigned)wr_current_teb()->tid;
    if(__atomic_load_n(&h->owner,__ATOMIC_ACQUIRE)==self){if(h->depth==UINT32_MAX)wr_exit(0xc000000du);h->depth++;return;}
    wr_lock(&h->lock);h->depth=1;__atomic_store_n(&h->owner,self,__ATOMIC_RELEASE);
}
static void leave(heap_state *h){
    if(__atomic_load_n(&h->owner,__ATOMIC_ACQUIRE)!=(unsigned)wr_current_teb()->tid || !h->depth)wr_exit(0xc000000du);
    if(!--h->depth){__atomic_store_n(&h->owner,0,__ATOMIC_RELEASE);wr_unlock(&h->lock);}
}
static int index(void *handle){
    uintptr_t v=(uintptr_t)handle;unsigned i=v&255;
    return v>>48==0x5f && !(v&0xff0000000000ull) && (uint32_t)(v>>8)==slots[i].generation && slots[i].value?(int)i:-1;
}
static heap_state *pin(void *handle){
    if(handle==&heap)return &heap;
    wr_lock(&registry);int i=index(handle);heap_state *h=i>=0?slots[i].value:0;
    if(h){if(slots[i].pins==UINT32_MAX)h=0;else slots[i].pins++;}wr_unlock(&registry);
    if(!h)fail(6);return h;
}
static void put(void *handle){
    if(handle==&heap)return;
    wr_lock(&registry);int i=index(handle);if(i<0 || !slots[i].pins)wr_exit(0xc000000du);slots[i].pins--;wr_unlock(&registry);
}
static int rounded(size_t n,size_t unit,size_t *out){if(n>SIZE_MAX-(unit-1))return 0;*out=(n+unit-1)&~(unit-1);return 1;}
static int add_piece(heap_state *h,size_t length,size_t committed){
    piece *p=wr_native_alloc(sizeof(*p));if(!p)return fail(8);
    memset(p,0,sizeof(*p));p->length=length;p->base=wr_map(0,length,0);
    if(!p->base){wr_native_free(p);return fail(8);}
    if(committed && wr_protect(p->base,committed,PE_R|PE_W)){
        wr_unmap(p->base,length);wr_native_free(p);return fail(8);
    }
    p->committed=committed;p->next=h->pieces;h->pieces=p;return 1;
}
static int initialize(heap_state *h,size_t initial,size_t maximum){
    if(wr_random(&h->secret,sizeof(h->secret)))return fail(8);
    size_t committed,reserved;
    if(!rounded(initial?initial:1,4096,&committed) || !rounded(maximum?maximum:committed,maximum?4096:65536,&reserved) || committed>reserved)return fail(87);
    if(!add_piece(h,reserved,committed))return 0;h->fixed=maximum!=0;h->initialized=1;return 1;
}
static void *payload(block *b){return b->arena->base+b->offset+16;}
static uint64_t cookie(heap_state *h,block *b){return h->secret^(uintptr_t)payload(b)^b->size;}
static void seal(heap_state *h,block *b){
    uint64_t value[2]={cookie(h,b),~cookie(h,b)};
    memcpy((uint8_t *)payload(b)-16,value,16);memcpy((uint8_t *)payload(b)+b->size,value,16);
}
static int intact(heap_state *h,block *b){
    uint64_t prefix[2],suffix[2],value=cookie(h,b);
    if(!wr_memory_copy(prefix,(uint8_t *)payload(b)-16,16) && !wr_memory_copy(suffix,(uint8_t *)payload(b)+b->size,16) &&
       prefix[0]==value && prefix[1]==~value && suffix[0]==value && suffix[1]==~value)return 1;
    if(__atomic_load_n(&terminate_on_corruption,__ATOMIC_ACQUIRE)){
        win_context context;void (WR_WIN *capture)(win_context *)=wr_unwind_exports("RtlCaptureContext");
        if(capture){capture(&context);exception_record record={.code=0xc0000374u,.flags=1,.address=(void *)(uintptr_t)context.rip};wr_wer_report(&record,&context);}
        wr_exit(0xc0000374u);
    }
    return fail(13);
}
static block *find(heap_state *h,const void *p){for(block *b=h->blocks;b;b=b->next)if(b->busy && payload(b)==p)return b;return 0;}
static void coalesce(heap_state *h,block *b){
    for(;;){block **link=&h->blocks,*other=0;
        while(*link){block *q=*link;
            if(q!=b && !q->busy && q->arena==b->arena &&
               (q->offset+q->span==b->offset || b->offset+b->span==q->offset)){other=q;break;}link=&q->next;}
        if(!other)break;
        if(other->offset<b->offset)b->offset=other->offset;b->span+=other->span;*link=other->next;wr_native_free(other);
    }
}
static void *allocate(heap_state *h,unsigned flags,size_t n){
    size_t span;if(n>SIZE_MAX-32 || !rounded(n+32,16,&span)){fail(8);return 0;}
    if(!h->initialized && !initialize(h,0,0))return 0;
    block *b=h->blocks;while(b && (b->busy || b->span<span))b=b->next;
    if(b){
        if(b->span-span>=48){block *rest=wr_native_alloc(sizeof(*rest));if(!rest){fail(8);return 0;}
            *rest=(block){.next=h->blocks,.arena=b->arena,.offset=b->offset+span,.span=b->span-span};h->blocks=rest;b->span=span;}
    }else{
        b=wr_native_alloc(sizeof(*b));if(!b){fail(8);return 0;}
        piece *p=h->pieces;while(p && span>p->length-p->used)p=p->next;
        if(!p){
            if(h->fixed){wr_native_free(b);fail(8);return 0;}
            size_t length=h->pieces->length;
            if(length<=SIZE_MAX/2)length*=2;if(length<span && !rounded(span,65536,&length)){wr_native_free(b);fail(8);return 0;}
            if(!add_piece(h,length,0)){wr_native_free(b);return 0;}p=h->pieces;
        }
        size_t end=p->used+span,commit;
        if(!rounded(end,4096,&commit) || (commit>p->committed && wr_protect(p->base+p->committed,commit-p->committed,PE_R|PE_W))){wr_native_free(b);fail(8);return 0;}
        if(commit>p->committed)p->committed=commit;
        *b=(block){.next=h->blocks,.arena=p,.offset=p->used,.span=span};h->blocks=b;p->used=end;
    }
    b->busy=1;b->size=n;if(flags&8)memset(payload(b),0,n);seal(h,b);return payload(b);
}
void *wr_process_heap(void){return &heap;}
static void *WR_WIN GetProcessHeap(void){return &heap;}
static unsigned WR_WIN GetProcessHeaps(unsigned capacity,void **out){
    if(capacity && !out){fail(87);return 0;}
    void *list[257];unsigned count=1;list[0]=&heap;
    wr_lock(&registry);for(unsigned i=0;i<256;i++)if(slots[i].value)list[count++]=(void *)(0x5f000000000000ull|((uint64_t)slots[i].generation<<8)|i);wr_unlock(&registry);
    if(capacity>=count && wr_memory_copy(out,list,count*sizeof(void *))){fail(998);return 0;}return count;
}
static void *WR_WIN HeapCreate(unsigned flags,size_t initial,size_t maximum){
    if(flags&0x40000){fail(5);return 0;}if(flags&~1u){fail(50);return 0;}
    if(maximum && initial>maximum){fail(87);return 0;}
    heap_state *h=wr_native_alloc(sizeof(*h));if(!h){fail(8);return 0;}memset(h,0,sizeof(*h));
    if(!initialize(h,initial,maximum)){wr_native_free(h);return 0;}
    wr_lock(&registry);unsigned i=0;while(i<256 && slots[i].value)i++;
    if(i<256){if(!++slots[i].generation)++slots[i].generation;slots[i].value=h;slots[i].pins=0;}
    wr_unlock(&registry);
    if(i==256){wr_unmap(h->pieces->base,h->pieces->length);wr_native_free(h->pieces);wr_native_free(h);fail(8);return 0;}
    return (void *)(0x5f000000000000ull|((uint64_t)slots[i].generation<<8)|i);
}
static int WR_WIN HeapDestroy(void *handle){
    if(handle==&heap)return fail(5);
    wr_lock(&registry);int i=index(handle);unsigned error=i<0?6:slots[i].pins?170:0;heap_state *h=error?0:slots[i].value;
    if(h)slots[i].value=0;wr_unlock(&registry);if(error)return fail(error);
    block *b=h->blocks;while(b){block *next=b->next;if(b->busy && !intact(h,b))error=13;wr_native_free(b);b=next;}
    piece *p=h->pieces;while(p){piece *next=p->next;if(wr_unmap(p->base,p->length))wr_exit(0xc000000du);wr_native_free(p);p=next;}
    wr_native_free(h);return error?fail(error):1;
}
static void *WR_WIN HeapAlloc(void *handle,unsigned flags,size_t n){
    heap_state *h=pin(handle);if(!h)return 0;
    if(flags&~9u){put(handle);fail(50);return 0;}
    enter(h);void *p=allocate(h,flags,n);leave(h);put(handle);return p;
}
static int WR_WIN HeapFree(void *handle,unsigned flags,void *p){
    heap_state *h=pin(handle);if(!h)return 0;if(flags&~1u){put(handle);return fail(50);}
    enter(h);block *b=p?find(h,p):0;int result=!p;
    if(b && intact(h,b)){b->busy=0;coalesce(h,b);result=1;}else if(p && !b)fail(87);
    leave(h);put(handle);return result;
}
static size_t WR_WIN HeapSize(void *handle,unsigned flags,void *p){
    heap_state *h=pin(handle);if(!h)return SIZE_MAX;if(flags&~1u){put(handle);fail(50);return SIZE_MAX;}
    enter(h);block *b=find(h,p);size_t n=SIZE_MAX;if(b && intact(h,b))n=b->size;else if(!b)fail(87);leave(h);put(handle);return n;
}
static void *WR_WIN HeapReAlloc(void *handle,unsigned flags,void *p,size_t n){
    heap_state *h=pin(handle);if(!h)return 0;if(flags&~0x19u){put(handle);fail(50);return 0;}
    enter(h);block *b=find(h,p);void *result=0;
    if(!b)fail(87);
    else if(intact(h,b)){
        if(n<=b->span-32){if((flags&8) && n>b->size)memset((uint8_t *)p+b->size,0,n-b->size);b->size=n;seal(h,b);result=p;}
        else if(flags&0x10)fail(8);
        else if((result=allocate(h,0,n))){memcpy(result,p,b->size);if(flags&8)memset((uint8_t *)result+b->size,0,n-b->size);b->busy=0;coalesce(h,b);}
    }
    leave(h);put(handle);return result;
}
static int WR_WIN HeapLock(void *handle){heap_state *h=pin(handle);if(!h)return 0;enter(h);return 1;}
static int WR_WIN HeapUnlock(void *handle){
    heap_state *h=pin(handle);if(!h)return 0;
    if(__atomic_load_n(&h->owner,__ATOMIC_ACQUIRE)!=(unsigned)wr_current_teb()->tid || !h->depth){put(handle);return fail(158);}
    leave(h);put(handle);put(handle);return 1;
}
typedef struct {void *data;uint32_t bytes;uint8_t overhead,region;uint16_t flags;uint8_t detail[24];} heap_entry;
_Static_assert(sizeof(heap_entry)==40,"AMD64 PROCESS_HEAP_ENTRY");
static int WR_WIN HeapWalk(void *handle,heap_entry *out){
    heap_entry previous;if(wr_memory_copy(&previous,out,sizeof(previous)))return fail(998);
    heap_state *h=pin(handle);if(!h)return 0;enter(h);block *b=h->blocks;unsigned error=0;
    if(previous.data){while(b && (!b->busy || payload(b)!=previous.data))b=b->next;if(!b)error=87;else b=b->next;}
    while(b && !b->busy)b=b->next;
    heap_entry next={0};if(!error){if(!b)error=259;else if(b->size>UINT32_MAX)error=50;else if(!intact(h,b))error=13;
        else next=(heap_entry){.data=payload(b),.bytes=(uint32_t)b->size,.overhead=32,.flags=4};}
    leave(h);put(handle);if(!error && wr_memory_copy(out,&next,sizeof(next)))error=998;return error?fail(error):1;
}
static int WR_WIN HeapSetInformation(void *handle,unsigned kind,const void *info,size_t length){
    heap_state *h=handle?pin(handle):0;if(handle && !h)return 0;
    unsigned error=0;if(kind!=1)error=50;else if(info || length)error=87;
    else __atomic_store_n(&terminate_on_corruption,1,__ATOMIC_RELEASE);
    if(h)put(handle);return error?fail(error):1;
}
static void *WR_WIN LocalAlloc(unsigned flags,size_t size){if(flags&~0x40u){fail(50);return 0;}return HeapAlloc(&heap,flags&0x40?8:0,size);}
static void *WR_WIN LocalFree(void *p){if(!p)return 0;if(HeapFree(&heap,0,p))return 0;fail(6);return p;}
void *wr_heap_exports(const char *name){
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(GetProcessHeap);E(GetProcessHeaps);E(HeapCreate);E(HeapDestroy);E(HeapSetInformation);
    E(HeapAlloc);E(HeapFree);E(HeapReAlloc);E(HeapSize);E(HeapLock);E(HeapUnlock);E(HeapWalk);E(LocalAlloc);E(LocalFree);
#undef E
    if(wr_equal(name,"RtlGetProcessHeap",0))return (void *)&GetProcessHeap;
    if(wr_equal(name,"RtlAllocateHeap",0))return (void *)&HeapAlloc;
    if(wr_equal(name,"RtlFreeHeap",0))return (void *)&HeapFree;
    if(wr_equal(name,"RtlReAllocateHeap",0))return (void *)&HeapReAlloc;
    if(wr_equal(name,"RtlSizeHeap",0))return (void *)&HeapSize;
    return 0;
}
