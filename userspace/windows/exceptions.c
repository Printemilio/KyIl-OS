#include "exception_abi.h"
#include "wer.h"
static struct handler {struct handler *next;int32_t (WR_WIN *function)(exception_pointers *);uint32_t refs,removed;uint64_t token;} *handlers;
static wr_mutex gate;
static uint64_t generation;
uint64_t wr_xstate_mask;
uint64_t wr_xstate_size=512;
void wr_xstate_init(void) {
    uint32_t a,b,c,d;a=1;__asm__ volatile("cpuid":"+a"(a),"=b"(b),"=c"(c),"=d"(d));
    if((c&(1u<<27)) && (c&(1u<<26))) {
        __asm__ volatile("xgetbv":"=a"(a),"=d"(d):"c"(0));wr_xstate_mask=((uint64_t)d<<32)|a;
        a=13;c=0;__asm__ volatile("cpuid":"+a"(a),"=b"(b),"+c"(c),"=d"(d));
        wr_xstate_size=b;
    }
}
static void *WR_WIN AddVectoredExceptionHandler(unsigned first,int32_t (WR_WIN *fn)(exception_pointers *)) {
    if(!fn) {wr_current_teb()->last_error=87;return 0;}
    struct handler *h=wr_native_alloc(sizeof(*h));if(!h) {wr_current_teb()->last_error=8;return 0;}
    memset(h,0,sizeof(*h));h->function=fn;h->refs=1;
    wr_lock(&gate);h->token=(++generation<<8)|0x7d;
    if(first || !handlers) {h->next=handlers;handlers=h;}
    else {struct handler *p=handlers;while(p->next)p=p->next;p->next=h;}
    wr_unlock(&gate);return (void *)(uintptr_t)h->token;
}
static unsigned WR_WIN RemoveVectoredExceptionHandler(void *token) {
    wr_lock(&gate);struct handler **p=&handlers;
    while(*p && (*p)->token!=(uintptr_t)token)p=&(*p)->next;
    struct handler *h=*p;
    if(h) {*p=h->next;h->removed=1;if(--h->refs==0)wr_native_free(h);}
    wr_unlock(&gate);return h!=0;
}
int wr_deliver_exception(exception_record *record,win_context *c) {
    wr_lock(&gate);size_t count=0;for(struct handler *h=handlers;h;h=h->next)count++;
    struct handler **list=count?wr_native_alloc(count*sizeof(*list)):0;
    if(count && !list) {wr_unlock(&gate);wr_exit(125);}
    size_t at=0;for(struct handler *h=handlers;h;h=h->next) {h->refs++;list[at++]=h;}
    wr_unlock(&gate);
    int handled=0;exception_pointers pointers={record,c};
    for(size_t i=0;i<count;i++)if(!handled) {
        int32_t result=list[i]->function(&pointers);
        if(result==-1)handled=1;
        else if(result!=0) {wr_log("windows: invalid VEH disposition\n");wr_exit(0xc000000du);}
    }
    wr_lock(&gate);for(size_t i=0;i<count;i++)if(--list[i]->refs==0)wr_native_free(list[i]);wr_unlock(&gate);
    if(list)wr_native_free(list);
    if(handled && (record->flags&1))wr_exit(0xc0000025u);
    if(handled || wr_seh_dispatch(record,c))return 1;
    wr_wer_report(record,c);return 0;
}
int wr_dispatch_exception(const vex_event_fault_payload_t *f,vex_event_user_context_t *u,void *xstate) {
    if(!f || !u)return 0;
    exception_record record={0};win_context c={0};
    record.address=(void *)(uintptr_t)u->rip;
    switch(f->vector) {
    case 0:record.code=0xc0000094u;break;
    case 3:record.code=0x80000003u;break;
    case 6:record.code=0xc000001du;break;
    case 14:record.code=0xc0000005u;record.count=2;record.arguments[0]=(f->access&4)?8:((f->access&2)?1:0);record.arguments[1]=f->address;break;
    default:return 0;
    }
    c.flags=0x10000bu;c.cs=(uint16_t)u->cs;c.ss=(uint16_t)u->ss;c.eflags=(uint32_t)u->rflags;
    __asm__ volatile("mov %%ds,%0; mov %%es,%1; mov %%fs,%2; mov %%gs,%3":"=r"(c.ds),"=r"(c.es),"=r"(c.fs),"=r"(c.gs));
#define REG(r) c.r=u->r
    REG(rax);REG(rcx);REG(rdx);REG(rbx);REG(rsp);REG(rbp);REG(rsi);REG(rdi);REG(r8);REG(r9);REG(r10);REG(r11);REG(r12);REG(r13);REG(r14);REG(r15);REG(rip);
#undef REG
    memcpy(c.fp,xstate,512);c.mxcsr=pe_u32(c.fp+24);
    wr_thread *thread=(void *)wr_current_teb();thread->native_fault_active=1;
    int delivered=wr_deliver_exception(&record,&c);thread->native_fault_active=0;
    if(!delivered){
        /* Only fatal faults: use PE unwind metadata, never guess a frame-pointer chain. */
        void *frames[24];win_context walk=c;unsigned count=wr_walk_backtrace(&walk,0,24,frames,0);
        wr_log("windows-fatal: PE unwind, last_error=");
        uint64_t values[4]={thread->teb.last_error,c.rax,c.rcx,c.rsi};
        for(unsigned k=0;k<4;k++){char text[19]="0x0000000000000000";for(unsigned j=0;j<16;j++)text[2+j]="0123456789abcdef"[(values[k]>>(60-4*j))&15];wr_log(text);wr_log(k==3?"\n":" ");}
        for(unsigned k=0;k<count;k++){uintptr_t pc=(uintptr_t)frames[k];wr_log("windows-fatal-frame: ");
            module *image=0;for(unsigned n=0;n<wr_modules_count();n++){module *m=wr_module_at(n);if(pc>=(uintptr_t)m->base && pc-(uintptr_t)m->base<m->image.size){image=m;break;}}
            if(image){wr_log(image->name);pc-=(uintptr_t)image->base;}else wr_log("unknown");
            char text[20]="+0x0000000000000000";for(unsigned j=0;j<16;j++)text[3+j]="0123456789abcdef"[(pc>>(60-4*j))&15];wr_log(text);wr_log("\n");}
        return 0;
    }
    uint32_t mxcsr_mask=pe_u32((uint8_t *)xstate+28);if(!mxcsr_mask)mxcsr_mask=0xffbfu;
    if((c.flags&~0x10000fu) || c.cs!=u->cs || c.ss!=u->ss ||
        c.rip>=0x800000000000ull || c.rsp>=0x800000000000ull || (c.mxcsr&~mxcsr_mask))wr_exit(0xc000000du);
#define REG(r) u->r=c.r
    REG(rax);REG(rcx);REG(rdx);REG(rbx);REG(rsp);REG(rbp);REG(rsi);REG(rdi);REG(r8);REG(r9);REG(r10);REG(r11);REG(r12);REG(r13);REG(r14);REG(r15);REG(rip);
#undef REG
    u->rflags=c.eflags;
    memcpy(xstate,c.fp,512);*(uint32_t *)((uint8_t *)xstate+24)=c.mxcsr;
    if(wr_xstate_mask)*(uint64_t *)((uint8_t *)xstate+512)|=3u;
    return 1;
}
void *wr_exception_exports(const char *name) {
    void *seh=wr_seh_exports(name);if(seh)return seh;
    void *unwind=wr_unwind_exports(name);if(unwind)return unwind;
    if(wr_equal(name,"AddVectoredExceptionHandler",0) || wr_equal(name,"RtlAddVectoredExceptionHandler",0))return (void *)&AddVectoredExceptionHandler;
    if(wr_equal(name,"RemoveVectoredExceptionHandler",0) || wr_equal(name,"RtlRemoveVectoredExceptionHandler",0))return (void *)&RemoveVectoredExceptionHandler;
    return 0;
}

