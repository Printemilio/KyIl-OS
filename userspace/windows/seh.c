#include "exception_abi.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
typedef struct {uint64_t pc,base;runtime_function *function;uint64_t frame,target;
    win_context *context;void *handler,*data,*history;uint32_t scope,pad;} dispatcher;
_Static_assert(sizeof(dispatcher)==80,"AMD64 DISPATCHER_CONTEXT");
typedef struct dispatch_state {struct dispatch_state *previous;win_context *origin;} dispatch_state;
typedef int32_t (WR_WIN *language_handler)(exception_record *,uint64_t,win_context *,dispatcher *);
static int32_t (WR_WIN *top_filter)(exception_pointers *);
extern void WR_WIN wr_raise_entry(unsigned,unsigned,unsigned,const uint64_t *);
extern void WR_WIN wr_unwind_entry(void *,void *,exception_record *,void *);
extern void WR_WIN wr_unwind_ex_entry(void *,void *,exception_record *,void *,win_context *,void *);
extern _Noreturn void wr_restore_context_raw(win_context *);
static int execute(uint64_t pc){vex_memory_view_info_t v;return !wr_memory_query(pc,&v) && (v.permissions&PE_X);}
_Noreturn void wr_restore_context(win_context *source) {
    win_context c;if(wr_memory_copy(&c,source,sizeof(c)) || (c.flags&~0x10000fu) || (c.flags&0x100003)!=0x100003 || !execute(c.rip) || c.rsp<8)wr_exit(0xc000000du);
    uint16_t cs,ss;__asm__ volatile("mov %%cs,%0;mov %%ss,%1":"=r"(cs),"=r"(ss));
    uint32_t mask=pe_u32(c.fp+28);if(!mask)mask=0xffbfu;
    if(c.cs!=cs || c.ss!=ss || (c.mxcsr&~mask))wr_exit(0xc000000du);
    /* Only userspace-writable state is resumable; no flags, selectors or
     * unvalidated stack access can escape into the native exception engine. */
    c.eflags=(c.eflags&0x00000cd5u)|0x202u;
    if(wr_memory_copy((void *)(uintptr_t)(c.rsp-8),&c.rip,8))wr_exit(0xc0000005u);
    wr_thread *thread=(void *)wr_current_teb();
    int target_regular=c.rsp>=thread->teb.stack_limit && c.rsp<=thread->teb.stack_base;
    int target_alt=thread->alt_stack && c.rsp>=(uintptr_t)thread->alt_stack && c.rsp<=(uintptr_t)thread->alt_stack+thread->alt_size;
    if(!target_regular && !target_alt)wr_exit(0xc000000du);
    dispatch_state *state=thread->seh_state;
    while(state){uint64_t sp=state->origin->rsp;
        int origin_regular=sp>=thread->teb.stack_limit && sp<=thread->teb.stack_base;
        if((target_regular && !origin_regular) || (target_regular==origin_regular && c.rsp>=sp))state=state->previous;else break;}
    thread->seh_state=state;
    /* A continuable software exception raised inside a native fault handler
     * resumes that handler, retaining its outer event. Only a nonlocal jump
     * back to the application stack abandons the native interrupted context. */
    if(thread->native_fault_active && target_regular){
        if((int64_t)vex_syscall0(VEX_SYS_EVENT_LEAVE)<0)wr_exit(0xc000000du);thread->native_fault_active=0;
    }
    wr_restore_context_raw(&c);
}
static int step(unsigned type,win_context *c,win_context *next,dispatcher *d) {
    memset(d,0,sizeof(*d));d->pc=c->rip;d->context=c;
    runtime_function *(WR_WIN *lookup)(uint64_t,uint64_t *,void *)=wr_unwind_exports("RtlLookupFunctionEntry");
    void *(WR_WIN *image)(void *,void **)=wr_unwind_exports("RtlPcToFileHeader");
    void *(WR_WIN *unwind)(unsigned,uint64_t,uint64_t,runtime_function *,win_context *,void **,uint64_t *,context_pointers *)=wr_unwind_exports("RtlVirtualUnwind");
    void *base=0;if(!image((void *)(uintptr_t)c->rip,&base))return 0;
    d->function=lookup(c->rip,&d->base,0);if(!d->function)d->base=(uintptr_t)base;
    *next=*c;d->handler=unwind(type,d->base,d->pc,d->function,next,&d->data,&d->frame,0);
    wr_teb *teb=wr_current_teb();
    uint64_t low=teb->stack_limit,high=teb->stack_base;wr_thread *thread=(void *)teb;
    if(thread->alt_stack && c->rsp>=(uintptr_t)thread->alt_stack && c->rsp<(uintptr_t)thread->alt_stack+thread->alt_size){low=(uintptr_t)thread->alt_stack;high=low+thread->alt_size;}
    if(d->frame<low || d->frame>=high || next->rsp<=c->rsp || next->rsp>high)wr_exit(0xc0000028u);
    return 1;
}
static _Noreturn void unwind_to(uint64_t target,uint64_t ip,exception_record *record,uint64_t value,win_context *origin,win_context *out) {
    win_context c=*origin,next;exception_record local;
    if(!record){local=(exception_record){.code=0xc0000027u,.address=(void *)(uintptr_t)c.rip};record=&local;}
    unsigned original_flags=record->flags;record->flags|=2;if(!target)record->flags|=4;
    for(unsigned depth=0;depth<8192;depth++) {
        dispatcher d;if(!step(2,&c,&next,&d))break;
        if(target && d.frame>target)wr_exit(0xc0000029u);
        int reached=target && d.frame==target;d.target=ip;if(reached)record->flags|=0x20;
        if(d.handler){int32_t disposition=((language_handler)d.handler)(record,d.frame,&c,&d);
            if(disposition!=1){wr_log("windows-seh: unsupported unwind disposition\n");wr_exit(0xc0000026u);}}
        if(reached){c.rsp=d.frame;c.rip=ip;c.rax=value;record->flags=original_flags;
            if(out && wr_memory_copy(out,&c,sizeof(c)))wr_exit(0xc0000005u);wr_restore_context(&c);}
        c=next;
    }
    record->flags=original_flags;
    /* Exit-unwind runs all reachable PE cleanup handlers, then terminates.
     * An unknown/native frame is never silently invented as a PE frame. */
    wr_exit(target?0xc0000029u:record->code);
}
static int32_t WR_WIN UnhandledExceptionFilter(exception_pointers *p) {
    if(!p || !p->record || !p->context)wr_exit(0xc000000du);
    int32_t (WR_WIN *filter)(exception_pointers *)=__atomic_load_n(&top_filter,__ATOMIC_ACQUIRE);
    int32_t result=filter?filter(p):1;
    if(result!=0 && result!=1 && result!=-1)wr_exit(0xc0000026u);
    if(result==-1 && (p->record->flags&1))wr_exit(0xc0000025u);return result;
}
static void *WR_WIN SetUnhandledExceptionFilter(int32_t (WR_WIN *filter)(exception_pointers *)) {
    if(filter && !execute((uintptr_t)filter)){wr_current_teb()->last_error=998;return 0;}
    return __atomic_exchange_n(&top_filter,filter,__ATOMIC_ACQ_REL);
}
int wr_seh_dispatch(exception_record *record,win_context *original) {
    wr_thread *thread=(void *)wr_current_teb();dispatch_state state={thread->seh_state,original};
    unsigned nesting=0;for(dispatch_state *p=state.previous;p;p=p->previous)if(++nesting>32)wr_exit(0xc00000fdu);
    thread->seh_state=&state;win_context c=*original,next;
    for(unsigned depth=0;depth<8192;depth++) {
        dispatcher d;if(!step(1,&c,&next,&d))break;
        if(d.handler){int32_t disposition=((language_handler)d.handler)(record,d.frame,original,&d);
            if(disposition==0){thread->seh_state=state.previous;if(record->flags&1)wr_exit(0xc0000025u);return 1;}
            if(disposition!=1){wr_log("windows-seh: unsupported dispatch disposition\n");wr_exit(0xc0000026u);}}
        c=next;
    }
    thread->seh_state=state.previous;exception_pointers p={record,original};return UnhandledExceptionFilter(&p)==-1;
}
typedef struct {uint32_t begin,end,handler,jump;} scope_entry;
static int32_t WR_WIN __C_specific_handler(exception_record *record,uint64_t frame,win_context *context,dispatcher *d) {
    uint32_t count;if(!d || d->pc<d->base || wr_memory_copy(&count,d->data,4) || count>65536)wr_exit(0xc00000ffu);
    uint64_t control=d->pc-d->base;
    for(unsigned i=d->scope;i<count;i++) {
        scope_entry entry;if(wr_memory_copy(&entry,(const uint8_t *)d->data+4+16ull*i,sizeof(entry)) || entry.begin>=entry.end)wr_exit(0xc00000ffu);
        if(control<entry.begin || control>=entry.end)continue;d->scope=i+1;
        if(record->flags&6) {
            if(entry.jump)continue;
            if((record->flags&0x20) && d->target>=d->base && d->target-d->base>=entry.begin && d->target-d->base<entry.end)continue;
            if(!entry.handler || !execute(d->base+entry.handler))wr_exit(0xc00000ffu);
            void (WR_WIN *cleanup)(int,uint64_t)=(void *)(uintptr_t)(d->base+entry.handler);cleanup(1,frame);
        }else if(entry.jump) {
            int result;if(entry.handler==1)result=1;
            else {if(!entry.handler || !execute(d->base+entry.handler))wr_exit(0xc00000ffu);
                int (WR_WIN *filter)(exception_pointers *,uint64_t)=(void *)(uintptr_t)(d->base+entry.handler);
                exception_pointers pointers={record,context};result=filter(&pointers,frame);}
            if(result<0)return 0;
            if(result>0){wr_thread *t=(void *)wr_current_teb();dispatch_state *state=t->seh_state;
                if(!state || !state->origin || !execute(d->base+entry.jump))wr_exit(0xc0000026u);
                unwind_to(frame,d->base+entry.jump,record,record->code,state->origin,0);}
        }
    }return 1;
}
_Noreturn void wr_raise_api(unsigned code,unsigned flags,unsigned count,const uint64_t *arguments,win_context *captured) {
    if((flags&~1u) || count>15 || (count && !arguments))wr_exit(0xc000000du);
    exception_record record={.code=code&~0x10000000u,.flags=flags,.address=(void *)(uintptr_t)captured->rip,.count=count};
    if(count && wr_memory_copy(record.arguments,arguments,count*8ull))wr_exit(0xc0000005u);
    if((code==0xc06d007eu || code==0xc06d007fu) && count==1){
        struct {uint32_t size,pad;uintptr_t descriptor,slot,dll;uint32_t by_name,pad2;uintptr_t symbol,module,function;uint32_t error,pad3;} detail;
        if(!wr_memory_copy(&detail,(void *)(uintptr_t)record.arguments[0],sizeof(detail)) && detail.size==sizeof(detail)){
            uintptr_t texts[2]={detail.dll,detail.symbol};wr_log("windows-delay-failure:");
            for(unsigned i=0;i<2;i++){char buffer[160];unsigned n=0;if(i && !detail.by_name)continue;
                for(;n<sizeof(buffer)-1;n++){if(wr_memory_copy(buffer+n,(void *)(texts[i]+n),1)){buffer[n]=0;break;}if(!buffer[n])break;}
                buffer[n]=0;wr_log(i?" symbol=":" dll=");wr_log(buffer);}
            wr_log("\n");}
    }
    if(!wr_deliver_exception(&record,captured))wr_exit(record.code);wr_restore_context(captured);
}
_Noreturn void wr_unwind_api(void *target,void *ip,exception_record *record,void *value,win_context *out,void *history,win_context *captured) {
    (void)history;exception_record local;if(record){if(wr_memory_copy(&local,record,sizeof(local)))wr_exit(0xc0000005u);record=&local;}
    unwind_to((uintptr_t)target,(uintptr_t)ip,record,(uintptr_t)value,captured,out);
}
static void WR_WIN RtlRestoreContext(win_context *context,exception_record *record) {
    if(record)wr_exit(0xc00000bbu); /* LONGJUMP/UNWIND_CONSOLIDATE protocols remain explicit. */
    wr_restore_context(context);
}
void *wr_seh_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(SetUnhandledExceptionFilter);E(UnhandledExceptionFilter);E(__C_specific_handler);E(RtlRestoreContext);
#undef E
    if(wr_equal(name,"RaiseException",0))return (void *)&wr_raise_entry;
    if(wr_equal(name,"RtlUnwind",0))return (void *)&wr_unwind_entry;
    if(wr_equal(name,"RtlUnwindEx",0))return (void *)&wr_unwind_ex_entry;
    return 0;
}
