#include "windows.h"
#include "process.h"
#include "job.h"
#include "exception_abi.h"
#include "wer.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
wr_peb wr_process_peb;
wr_critical_section wr_loader_gate={.count=-1};
/* Windows loader callbacks can re-enter the loader on the same thread.
 * Cross-thread execution stays serialized by the existing native futex gate. */
void wr_loader_lock(void) {
    wr_critical_enter(&wr_loader_gate,wr_tid());
}
void wr_loader_unlock(void) {
    wr_critical_leave(&wr_loader_gate,wr_tid());
}
static wr_ldr_data ldr;
static wr_thread *main_thread,*threads;
/* A reader can be suspended while resolving a handle. It must never own a
   userspace lock needed by ResumeThread/GetThreadContext. Unpublished dead
   records are reclaimed only when no lock-free handle reader is active. */
static uint32_t handle_readers;
static unsigned exiting;
static unsigned WR_WIN SleepEx(unsigned,int);
typedef struct wr_apc {struct wr_apc *next;void (WR_WIN *callback)(uintptr_t);uintptr_t value;} wr_apc;
static void apc_cleanup(wr_thread *t) {
    wr_lock(&t->apc_gate);t->apc_closed=1;wr_apc *p=t->apc_first;t->apc_first=t->apc_last=0;wr_unlock(&t->apc_gate);
    while(p){wr_apc *next=p->next;wr_native_free(p);p=next;}
}
static uint64_t tls_bitmap;
typedef struct {
    uint32_t maximum,length,flags,debug;
    void *console;uint32_t console_flags,pad;
    void *input,*output,*error;
    wr_unicode cwd;void *cwd_handle;
    wr_unicode dll_path,image_path,command_line;
    void *environment;
} process_parameters;
_Static_assert(offsetof(process_parameters,command_line)==0x70,"parameters command line");
_Static_assert(offsetof(process_parameters,environment)==0x80,"parameters environment");
static process_parameters params;
static char command_line[32768];
static uint16_t command_w[32768],image_w[512],cwd_w[513];
static uint16_t *environment_w;
static size_t environment_size;
static wr_mutex environment_gate;
const uint16_t *wr_environment_block(void){return environment_w;}
void wr_environment_lock(int begin){if(begin)wr_lock(&environment_gate);else wr_unlock(&environment_gate);}
int wr_environment_snapshot(uint16_t *out,size_t cap) {
    wr_lock(&environment_gate);size_t n=environment_size/2;
    if(!n || n>cap){wr_unlock(&environment_gate);return -1;}memcpy(out,environment_w,n*2);wr_unlock(&environment_gate);return (int)n;
}
int wr_environment_replace(const uint16_t *data,size_t count) {
    uint16_t *replacement=wr_map(0,count*2,PE_R|PE_W);if(!replacement)return -1;
    memcpy(replacement,data,count*2);uint16_t *old=environment_w;size_t previous=environment_size;
    environment_w=replacement;environment_size=count*2;params.environment=replacement;
    if(old)wr_unmap(old,previous);return 0;
}
static void link_init(wr_link *p) {p->next=p->prev=p;}
static void link_append(wr_link *head,wr_link *p) {
    p->next=head;p->prev=head->prev;head->prev->next=p;head->prev=p;
}
int wr_utf16(const char *s,uint16_t *out,size_t capacity) {
    size_t n=0;
    for(size_t i=0;s[i];) {
        unsigned char c=s[i++];uint32_t cp;unsigned extra;
        if(c<128) {cp=c;extra=0;}
        else if(c>=0xc2 && c<=0xdf) {cp=c&31;extra=1;}
        else if(c>=0xe0 && c<=0xef) {cp=c&15;extra=2;}
        else if(c>=0xf0 && c<=0xf4) {cp=c&7;extra=3;}
        else return -1;
        for(unsigned j=0;j<extra;j++) {
            unsigned char b=s[i++];if((b&0xc0)!=0x80)return -1;cp=(cp<<6)|(b&63);
        }
        if((extra==1 && cp<128) || (extra==2 && cp<2048) || (extra==3 && cp<65536) ||
            cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return -1;
        size_t count=cp>=65536?2:1;if(n+count>=capacity)return -1;
        if(count==2) {cp-=65536;out[n++]=(uint16_t)(0xd800+(cp>>10));out[n++]=(uint16_t)(0xdc00+(cp&1023));}
        else out[n++]=(uint16_t)cp;
    }
    out[n]=0;return (int)n;
}
static int unicode(wr_unicode *u,const char *s,uint16_t *buffer,size_t cap) {
    int n=wr_utf16(s,buffer,cap);if(n<0 || n>32766)return -1;
    *u=(wr_unicode){(uint16_t)(n*2),(uint16_t)((n+1)*2),0,buffer};return 0;
}
static int parameters_cwd_length(int n){
    if(n<0)return -1;
    if(n && cwd_w[n-1]!='\\'){if((size_t)n+1>=sizeof(cwd_w)/2)return -1;cwd_w[n++]='\\';cwd_w[n]=0;}
    params.cwd=(wr_unicode){(uint16_t)(n*2),sizeof(cwd_w),0,cwd_w};return 0;
}
void wr_parameters_cwd(const char *text){
    wr_lock(&environment_gate);if(params.cwd.buffer && parameters_cwd_length(wr_utf16(text,cwd_w,512)))wr_exit(0xc000000du);wr_unlock(&environment_gate);
}
int wr_module_ldr(module *m) {
    int n=wr_module_guest_path(m,m->full_name,512);
    if(n<0 || unicode(&m->ldr.base_name,m->name,m->base_name,128))return -1;
    m->ldr.full_name=(wr_unicode){(uint16_t)(n*2),(uint16_t)((n+1)*2),0,m->full_name};
    m->ldr.base=m->base;m->ldr.entry=m->image.entry?m->base+m->image.entry:0;
    m->ldr.size=m->image.size;m->ldr.refs=1;m->ldr.tls_index=(uint16_t)m->tls.index;
    link_append(&ldr.load,&m->ldr.load);link_append(&ldr.memory,&m->ldr.memory);
    return 0;
}
void wr_module_unlink(module *m) {
    wr_link *lists[]={&m->ldr.load,&m->ldr.memory,&m->ldr.init};
    for(unsigned i=0;i<3;i++){wr_link *p=lists[i];if(p->next && p->prev){p->prev->next=p->next;p->next->prev=p->prev;p->next=p->prev=0;}}
}
static int quote_argument(size_t *at,const char *s) {
#define PUT(c) do{if(*at>=sizeof(command_line)-1)return -1;command_line[(*at)++]=(c);}while(0)
    PUT('"');size_t slashes=0;
    for(size_t i=0;;i++) {
        char c=s[i];if(c=='\\') {slashes++;continue;}
        size_t copies=(c=='"'||!c)?slashes*2:slashes;
        while(copies--)PUT('\\');slashes=0;
        if(!c)break;if(c=='"')PUT('\\');PUT(c);
    }
    PUT('"');return 0;
#undef PUT
}
int wr_tls_describe(module *m) {
    m->tls.index=UINT32_MAX;
    uint32_t r=m->image.dirs[9][0],n=m->image.dirs[9][1];if(!n)return 0;
    if(n<40)return -1;
    uint8_t *d=m->base+r;uintptr_t begin=pe_u64(d),end=pe_u64(d+8),index=pe_u64(d+16),callbacks=pe_u64(d+24);
    uintptr_t b=(uintptr_t)m->base;unsigned code=pe_u32(d+36)>>20&15;
    if(code==15 || (pe_u32(d+36)&~0x00f00000u) || end<begin ||
        ((begin||end) && (begin<b || !pe_span(begin-b,end-begin,m->image.size))) ||
        index<b || (index&3) || !pe_span(index-b,4,m->image.size) ||
        !(pe_page_access(&m->image,(uint32_t)(index-b)&~4095u)&PE_W))return -1;
    m->tls.data=(void *)begin;m->tls.length=end-begin;m->tls.zero=pe_u32(d+32);
    m->tls.alignment=code?1u<<(code-1):16;
    if(m->tls.length>SIZE_MAX-m->tls.zero-m->tls.alignment)return -1;
    unsigned idx=0;
    for(unsigned i=0;i<wr_modules_count();i++)if(wr_module_at(i)->tls.index!=UINT32_MAX)idx++;
    if(idx>=WR_MAX_MODULES)return -1;
    m->tls.index=idx;*(uint32_t *)index=idx;
    if(callbacks) {
        if(callbacks<b || (callbacks&7))return -1;
        for(uint64_t k=0;;k+=8) {
            if(!pe_span(callbacks-b+k,8,m->image.size))return -1;
            if(!pe_u64((uint8_t *)callbacks+k))break;
            m->tls.callback_count++;
        }
        m->tls.callbacks=(void *)callbacks;
    }
    return 0;
}
int wr_env_allocate_tls(wr_thread *t) {
    t->teb.static_tls=t->tls_vector;
    for(unsigned i=0;i<wr_modules_count();i++) {
        wr_tls_image *tls=&wr_module_at(i)->tls;if(tls->index==UINT32_MAX)continue;
        size_t n=tls->length+tls->zero;if(!n)n=1;
        size_t size=n+tls->alignment;
        uint8_t *p=wr_map(0,size,PE_R|PE_W);if(!p) {wr_env_free_tls(t);return -1;}
        uint8_t *aligned=(void *)(((uintptr_t)p+tls->alignment-1)&~(uintptr_t)(tls->alignment-1));
        t->tls_alloc[tls->index]=p;t->tls_size[tls->index]=size;t->tls_vector[tls->index]=aligned;
        if(tls->length)memcpy(aligned,tls->data,tls->length);
        /* Native anonymous pages are demand-zero; no eager zeroing of a huge zero tail. */
    }
    return 0;
}
void wr_env_free_tls(wr_thread *t) {
    for(unsigned i=0;i<WR_MAX_MODULES;i++)if(t->tls_alloc[i]) {
        wr_unmap(t->tls_alloc[i],t->tls_size[i]);t->tls_alloc[i]=t->tls_vector[i]=0;
    }
}
void wr_fault_dispatch(uint64_t event,const vex_event_fault_payload_t *fault,vex_event_user_context_t *context,void *xstate) {
    (void)event;(void)context;
    wr_thread *t=(void *)wr_current_teb();uintptr_t limit=t->teb.stack_limit;
    if(fault && fault->vector==14 && fault->address_valid && t->stack &&
        fault->address>=limit-4096 && fault->address<limit &&
        limit>(uintptr_t)t->stack+4096 && limit<=(uintptr_t)t->stack+t->stack_size && !(fault->access&4)) {
        if(!wr_protect((void *)(limit-4096),4096,PE_R|PE_W)) {t->teb.stack_limit=limit-4096;return;}
    }
    if(wr_dispatch_exception(fault,context,xstate))return;
    wr_log("windows: unhandled synchronous fault\n");
    wr_exit(t->stack && fault && fault->address_valid &&
        fault->address>=(uintptr_t)t->stack && fault->address<t->teb.stack_limit?0xc00000fdu:0xc0000005u);
}
static _Noreturn void fault_return(void) {vex_syscall0(VEX_SYS_EVENT_RETURN);__builtin_trap();}
static int teb_bind(wr_thread *t) {
    t->teb.self=(uintptr_t)&t->teb;t->teb.peb=&wr_process_peb;
    t->teb.pid=wr_pid();t->teb.tid=wr_tid();t->teb.exception_list=UINT64_MAX;
    t->teb.current_locale=0x409;
    if(wr_set_gs(&t->teb))return -1;
    t->alt_size=16384;t->alt_stack=wr_map(0,t->alt_size,PE_R|PE_W);if(!t->alt_stack)return -1;
    vex_event_alt_stack_req_t a={.base=(uintptr_t)t->alt_stack,.size=t->alt_size,.set=1};
    return (int)vex_syscall1(VEX_SYS_EVENT_ALT_STACK,(uintptr_t)&a);
}
static int stack(wr_thread *t,size_t reserve,size_t commit) {
    if(reserve<8192 || reserve>SIZE_MAX-65535 || commit>SIZE_MAX-4095)return -1;
    reserve=(reserve+65535)&~(size_t)65535;commit=(commit+4095)&~(size_t)4095;
    if(!commit)commit=4096;if(commit>reserve-4096)return -1;
    t->stack=wr_map(0,reserve,0);t->stack_size=reserve;if(!t->stack)return -1;
    t->teb.stack_base=(uintptr_t)t->stack+reserve;t->teb.stack_limit=t->teb.stack_base-commit;
    if(wr_protect((void *)(uintptr_t)t->teb.stack_limit,commit,PE_R|PE_W)) {wr_unmap(t->stack,reserve);t->stack=0;return -1;}return 0;
}
static void free_record(wr_thread *t) {
    if(t->teb.tid){wr_user_thread_exit((uint32_t)t->teb.tid);wr_winsock_thread_exit((uint32_t)t->teb.tid);}
    wr_object_thread_exit(t);
    wr_completion_thread_exit(t);
    apc_cleanup(t);
    wr_env_free_tls(t);if(t->alt_stack)wr_unmap(t->alt_stack,t->alt_size);
    if(t->stack && !t->stack_retired)wr_unmap(t->stack,t->stack_size);
    wr_unmap(t,sizeof(*t));
}
static void reap(void) {
    wr_thread **p=&threads;
    while(*p) {
        wr_thread *t=*p;
        if(t!=main_thread && !__atomic_load_n(&t->alive,__ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&t->handle_refs,__ATOMIC_SEQ_CST) && !__atomic_load_n(&handle_readers,__ATOMIC_SEQ_CST)) {
            *p=t->next;free_record(t);
        } else p=&t->next;
    }
}
int wr_env_main(uint64_t argc,char **argv,module *exe) {
    if(wr_process_init() || (!wr_boot && wr_namespace_init(argv[1])) || wr_files_init() || wr_job_adopt())return -1;
    memset(&wr_process_peb,0,sizeof(wr_process_peb));
    /* Same declared userspace compatibility version as GetVersionEx. */
    *(uint32_t *)((uint8_t *)&wr_process_peb+0x118)=10;
    *(uint32_t *)((uint8_t *)&wr_process_peb+0x11c)=0;
    *(uint16_t *)((uint8_t *)&wr_process_peb+0x120)=0;
    *(uint32_t *)((uint8_t *)&wr_process_peb+0x124)=2;
    ldr.size=sizeof(ldr);ldr.initialized=1;link_init(&ldr.load);link_init(&ldr.memory);link_init(&ldr.init);
    wr_process_peb.image=exe->base;wr_process_peb.ldr=&ldr;wr_process_peb.parameters=&params;wr_process_peb.heap=wr_process_heap();
    wr_process_peb.loader_lock=&wr_loader_gate;
    params.maximum=params.length=sizeof(params);params.flags=1;params.input=wr_file_stdio(0);params.output=wr_file_stdio(1);params.error=wr_file_stdio(2);
    int cwd_units=wr_cwd_wide(cwd_w,512);
    if(unicode(&params.image_path,wr_guest_image(),image_w,512) || cwd_units<0)return -1;
    if(parameters_cwd_length(cwd_units))return -1;
    size_t at=0;
    for(uint64_t i=1;i<argc;i++) {if(i>1)command_line[at++]=' ';if(quote_argument(&at,i==1?wr_guest_image():argv[i]))return -1;}
    command_line[at]=0;if(unicode(&params.command_line,command_line,command_w,32768))return -1;
    if(wr_boot) {
        memcpy(command_w,wr_boot->command,(size_t)wr_boot->command_length*2);
        params.command_line=(wr_unicode){(uint16_t)((wr_boot->command_length-1)*2),(uint16_t)(wr_boot->command_length*2),0,command_w};
        if(wr_utf8(command_w,command_line,sizeof(command_line)))return -1;
    }
    char *empty_env[]={0};char **env=wr_boot?empty_env:argv+argc+1;size_t size=4;
    for(unsigned i=0;env[i];i++) {size_t n=wr_len(env[i]);if(n>32767 || size>65536-n*2-2)return -1;size+=n*2+2;}
    environment_w=wr_map(0,size,PE_R|PE_W);environment_size=size;if(!environment_w)return -1;
    size_t pos=0;
    for(unsigned i=0;env[i];i++) {int n=wr_utf16(env[i],environment_w+pos,size/2-pos);if(n<0)return -1;pos+=(size_t)n+1;}
    environment_w[pos]=0;params.environment=environment_w;
    if(wr_boot) {
        wr_unmap(environment_w,environment_size);environment_size=(size_t)wr_boot->environment_length*2;
        environment_w=wr_map(0,environment_size,PE_R|PE_W);if(!environment_w)return -1;
        memcpy(environment_w,wr_boot->environment,environment_size);params.environment=environment_w;
    }
    main_thread=wr_map(0,sizeof(*main_thread),PE_R|PE_W);if(!main_thread)return -1;
    threads=main_thread;main_thread->alive=1;
    if(vex_syscall1(VEX_SYS_THREAD_EXIT_WORD,(uintptr_t)&main_thread->alive))return -1;
    uint64_t low,high;if(wr_context(&low,&high,0) || !low || !high)return -1;
    main_thread->teb.stack_base=high;main_thread->teb.stack_limit=low;
    if(teb_bind(main_thread) || wr_env_allocate_tls(main_thread))return -1;
    /* A native launch has no Windows logon environment. Project the actual
       confined prefix/profile; an explicit CreateProcess environment is exact. */
    if(!wr_boot && wr_environment_defaults())return -1;
    for(unsigned i=0;i<wr_modules_count();i++)if(wr_module_ldr(wr_module_at(i)))return -1;
    wr_xstate_init();extern void wr_fault_entry(void);
    vex_event_register_req_t r={.event_id=VEX_EVENT_SYNCHRONOUS_FAULT,.handler=(uintptr_t)wr_fault_entry,
        .restorer=(uintptr_t)fault_return,.flags=VEX_EVENT_SIGINFO|VEX_EVENT_ON_ALT_STACK,.delivery_value=VEX_EVENT_SYNCHRONOUS_FAULT};
    return (int)vex_syscall1(VEX_SYS_EVENT_REGISTER,(uintptr_t)&r);
}
extern int wr_enter_on_stack(void *,int (*)(void *),void *);
int wr_env_enter(int (*entry)(void *),void *arg) {
    module *exe=wr_module_at(0);
    uint64_t old_base=main_thread->teb.stack_base,old_limit=main_thread->teb.stack_limit;
    if(stack(main_thread,(size_t)exe->image.stack_reserve,(size_t)exe->image.stack_commit))return -1;
    int result=wr_enter_on_stack((void *)(uintptr_t)main_thread->teb.stack_base,entry,arg);
    main_thread->teb.stack_base=old_base;main_thread->teb.stack_limit=old_limit;return result;
}
void wr_tls_callbacks(module *m,unsigned reason) {
    for(unsigned i=0;i<m->tls.callback_count;i++) {
        uintptr_t target=m->tls.callbacks[i];int executable=0;
        for(unsigned j=0;j<wr_modules_count();j++) {
            module *v=wr_module_at(j);uintptr_t b=(uintptr_t)v->base;
            if(target>=b && pe_span(target-b,1,v->image.size) &&
                (pe_page_access(&v->image,(uint32_t)(target-b)&~4095u)&PE_X))executable=1;
        }
        if(!executable) {wr_log("windows: invalid TLS callback target\n");wr_exit(126);}
        void (WR_WIN *cb)(void *,unsigned,void *)=(void *)target;cb(m->base,reason,0);
    }
}
static void end_thread(unsigned code) {
    wr_thread *t=(void *)wr_current_teb();
    wr_user_thread_exit((uint32_t)t->teb.tid);wr_winsock_thread_exit((uint32_t)t->teb.tid);
    apc_cleanup(t);
    extern void wr_fls_thread_exit(wr_thread *);wr_fls_thread_exit(t);
    wr_loader_lock();wr_loader_thread_notify(3);t->result=code;t->finished=1;
    t->stack_retired=1;wr_loader_unlock();
    wr_object_thread_exit(t);
    wr_completion_thread_exit(t);
    wr_end_thread(t->stack,t->stack_size,code);
}
static void thread_entry(void *arg) {
    wr_thread *t=arg;
    if(teb_bind(t))wr_exit(125);
    wr_loader_lock();wr_loader_thread_notify(2);wr_loader_unlock();
    SleepEx(0,1); /* Windows APCs queued before startup run before the entry. */
    end_thread(t->entry(t->arg));
}
/* Opaque process-local handles, never a raw TID/address. Slot generations
   make closed/reused handles invalid. The execution reference is alive/clear. */
static struct {wr_thread *thread;uint32_t generation;} handles[256];
unsigned wr_thread_handle_count(void){
    wr_loader_lock();unsigned count=0;for(unsigned i=0;i<256;i++)if(__atomic_load_n(&handles[i].thread,__ATOMIC_SEQ_CST))count++;wr_loader_unlock();return count;
}
static void *handle_new(wr_thread *t) {
    for(unsigned i=0;i<256;i++)if(!__atomic_load_n(&handles[i].thread,__ATOMIC_SEQ_CST) && __atomic_load_n(&handles[i].generation,__ATOMIC_SEQ_CST)<WR_HANDLE_GENERATION_MAX) {
        uint32_t g=__atomic_add_fetch(&handles[i].generation,1,__ATOMIC_SEQ_CST);if(!g)g=__atomic_add_fetch(&handles[i].generation,1,__ATOMIC_SEQ_CST);
        __atomic_add_fetch(&t->handle_refs,1,__ATOMIC_SEQ_CST);__atomic_store_n(&handles[i].thread,t,__ATOMIC_SEQ_CST);
        return (void *)(uintptr_t)(wr_handle_pack(0x5a,g,i));
    }
    return 0;
}
static wr_thread *handle_get(void *h,unsigned *slot) {
    uint64_t v=(uintptr_t)h;unsigned i=wr_handle_slot(v);uint32_t g=wr_handle_generation(v);
    if(wr_handle_tag(v)!=0x5a || i>=256 || __atomic_load_n(&handles[i].generation,__ATOMIC_SEQ_CST)!=g)return 0;
    wr_thread *t=__atomic_load_n(&handles[i].thread,__ATOMIC_SEQ_CST);
    if(__atomic_load_n(&handles[i].generation,__ATOMIC_SEQ_CST)!=g)return 0;
    if(slot)*slot=i;return t;
}
static void *WR_WIN CreateThread(void *security,size_t size,uint32_t (WR_WIN *entry)(void *),void *arg,unsigned flags,uint32_t *tid) {
    wr_teb *teb=wr_current_teb();if(security || (flags&~4u) || !entry) {teb->last_error=50;return 0;}
    wr_loader_lock();reap();
    wr_thread *t=wr_map(0,sizeof(*t),PE_R|PE_W);void *h=0;
    if(exiting || !t)goto fail;
    module *exe=wr_module_at(0);
    if(stack(t,size?size:(size_t)exe->image.stack_reserve,(size_t)exe->image.stack_commit) || wr_env_allocate_tls(t))goto fail;
    t->entry=entry;t->arg=arg;t->alive=1;t->result=0xc0000001u;
    h=handle_new(t);if(!h)goto fail;
    t->next=threads;threads=t;
    int id=wr_start_thread_flags(thread_entry,t,(void *)(uintptr_t)t->teb.stack_base,(uint32_t *)&t->teb.tid,&t->alive,
        (flags&4)?VEX_THREAD_START_PAUSED:0);
    if(id<0) {threads=t->next;unsigned i;handle_get(h,&i);__atomic_store_n(&handles[i].thread,0,__ATOMIC_SEQ_CST);__atomic_sub_fetch(&t->handle_refs,1,__ATOMIC_SEQ_CST);goto fail;}
    if(tid)*tid=(uint32_t)id;wr_loader_unlock();return h;
fail:
    if(t)free_record(t);wr_loader_unlock();teb->last_error=8;return 0;
}
/* Never hold the loader gate while waiting for another execution context:
   the target can need that gate before reaching a safe return boundary. */
static wr_thread *thread_pin(void *h) {
    __atomic_add_fetch(&handle_readers,1,__ATOMIC_SEQ_CST);
    wr_thread *t=h==(void *)-2?(wr_thread *)wr_current_teb():handle_get(h,0);
    if(t)__atomic_add_fetch(&t->handle_refs,1,__ATOMIC_SEQ_CST);
    __atomic_sub_fetch(&handle_readers,1,__ATOMIC_SEQ_CST);return t;
}
static void thread_unpin(wr_thread *t){
    if(__atomic_sub_fetch(&t->handle_refs,1,__ATOMIC_SEQ_CST))return;
    /* Opportunistic collection may skip a suspended loader owner; control
       operations never wait for that owner. Create/exit also collect. */
    uint32_t tid=wr_tid();
    if(__atomic_load_n(&wr_loader_gate.owner,__ATOMIC_ACQUIRE)==tid){reap();return;}
    if(!wr_critical_try(&wr_loader_gate,tid))return;
    reap();wr_loader_unlock();
}
static unsigned control_error(int e){switch(-e){case 14:return 998;case 22:case 11:return 87;case 75:return 298;case 1:return 5;case 3:return 6;default:return 50;}}
static unsigned thread_control(void *h,unsigned operation,vex_thread_control_req_t *r) {
    wr_thread *t=thread_pin(h);
    if(!t){unsigned id;return wr_primary_id(h,&id)==1?50:6;}
    if(!__atomic_load_n(&t->alive,__ATOMIC_ACQUIRE)){thread_unpin(t);return 50;}
    r->size=sizeof(*r);r->operation=operation;r->tid=(uint32_t)t->teb.tid;
    if(operation==VEX_THREAD_PAUSE)r->value=127u; /* Windows MAXIMUM_SUSPEND_COUNT */
    int e=(int)(int64_t)vex_syscall1(VEX_SYS_THREAD_CONTROL,(uintptr_t)r);
    thread_unpin(t);return e?control_error(e):0;
}
static unsigned WR_WIN ResumeThread(void *h) {
    unsigned result;if(wr_primary_resume(h,&result))return result;
    vex_thread_control_req_t r={0};unsigned e=thread_control(h,VEX_THREAD_RESUME,&r);
    if(e){wr_current_teb()->last_error=e;return UINT32_MAX;}return (unsigned)r.value;
}
static unsigned WR_WIN SuspendThread(void *h) {
    vex_thread_control_req_t r={0};unsigned e=thread_control(h,VEX_THREAD_PAUSE,&r);
    if(e){wr_current_teb()->last_error=e;return UINT32_MAX;}return (unsigned)r.value;
}
static int WR_WIN GetThreadContext(void *h,win_context *out) {
    win_context c;
    if(((uintptr_t)out&15u) || wr_memory_copy(&c,out,sizeof(c))) {wr_current_teb()->last_error=998;return 0;}
    if((c.flags&0xffff0000u)!=0x100000u || !(c.flags&11u) || (c.flags&~0x10000bu)) {wr_current_teb()->last_error=50;return 0;}
    unsigned requested=c.flags;int captured=wr_wer_thread(h,&c,0);
    if(captured>=0){if(!captured)return 0;c.flags=requested;if(wr_memory_copy(out,&c,sizeof(c))){wr_current_teb()->last_error=998;return 0;}return 1;}
    vex_thread_control_req_t r={0};unsigned e=thread_control(h,VEX_THREAD_SNAPSHOT,&r);
    if(e){wr_current_teb()->last_error=e;return 0;}
    vex_event_user_context_t *u=&r.context;
    if(c.flags&1u){c.rsp=u->rsp;c.rip=u->rip;c.cs=(uint16_t)u->cs;c.ss=(uint16_t)u->ss;c.eflags=(uint32_t)u->rflags;}
    if(c.flags&2u) {
#define REG(n) c.n=u->n
        REG(rax);REG(rcx);REG(rdx);REG(rbx);REG(rbp);REG(rsi);REG(rdi);REG(r8);REG(r9);REG(r10);REG(r11);REG(r12);REG(r13);REG(r14);REG(r15);
#undef REG
    }
    if(c.flags&8u){memcpy(c.fp,r.fxsave,512);c.mxcsr=pe_u32(c.fp+24);}
    if(wr_memory_copy(out,&c,sizeof(c))){wr_current_teb()->last_error=998;return 0;}return 1;
}
static int WR_WIN QueryThreadCycleTime(void *h,uint64_t *out) {
    vex_thread_control_req_t r={0};unsigned e=thread_control(h,VEX_THREAD_CYCLES,&r);
    if(!e && wr_memory_copy(out,&r.value,8))e=998;
    if(e){wr_current_teb()->last_error=e;return 0;}return 1;
}
static uintptr_t WR_WIN SetThreadAffinityMask(void *h,uintptr_t mask){
    vex_memory_machine_info_t machine;vex_memory_view_req_t query={.size=sizeof(query),.operation=VEX_MEMORY_VIEW_MACHINE,.out=&machine};
    if(!mask || vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&query)){wr_current_teb()->last_error=87;return 0;}
    if(machine.online_cpus>64){wr_current_teb()->last_error=50;return 0;} /* processor groups not represented yet */
    uintptr_t available=machine.online_cpus==64?UINT64_MAX:((1ull<<machine.online_cpus)-1);
    if(mask&~available){wr_current_teb()->last_error=87;return 0;}
    uint32_t ids[64],old[64];unsigned count=0;for(unsigned i=0;i<machine.online_cpus;i++)if(mask&(1ull<<i))ids[count++]=i;
    vex_thread_cpu_set_t set={.size=sizeof(set),.count=count,.capacity=64,.cpus=ids,.previous=old};
    vex_thread_control_req_t r={.value=(uintptr_t)&set};unsigned error=thread_control(h,VEX_THREAD_CPU_SET,&r);
    if(error){wr_current_teb()->last_error=error;return 0;}
    uintptr_t previous=0;for(unsigned i=0;i<set.previous_count;i++)previous|=1ull<<old[i];return previous;
}
static int WR_WIN SwitchToThread(void) {
    vex_thread_control_req_t r={.size=sizeof(r),.operation=VEX_THREAD_YIELD};
    int e=(int)(int64_t)vex_syscall1(VEX_SYS_THREAD_CONTROL,(uintptr_t)&r);
    if(e){wr_current_teb()->last_error=control_error(e);return 0;}return r.value!=0;
}
static int WR_WIN GetThreadPriority(void *handle) {
    vex_thread_control_req_t r={0};unsigned error=thread_control(handle,VEX_THREAD_QOS_QUERY,&r);
    if(error){wr_current_teb()->last_error=error;return 0x7fffffff;}
    int value=(int8_t)r.value;
    if(value==4)return 15;if(value== -4)return -15;
    if(value< -2 || value>2){wr_current_teb()->last_error=50;return 0x7fffffff;}return value;
}
static int WR_WIN SetThreadPriority(void *handle,int priority) {
    if(priority!= -15 && priority!=15 && (priority< -2 || priority>2)){wr_current_teb()->last_error=87;return 0;}
    int band=priority==15?4:priority== -15?-4:priority;
    vex_thread_control_req_t r={.value=(uint64_t)(int64_t)band};unsigned error=thread_control(handle,VEX_THREAD_LATENCY_CLASS,&r);
    if(error)wr_current_teb()->last_error=error;return !error;
}
static int WR_WIN GetThreadPriorityBoost(void *handle,int *disabled) {
    vex_thread_control_req_t r={0};unsigned error=thread_control(handle,VEX_THREAD_QOS_QUERY,&r);int value=(r.value>>8)&1;
    if(!error && wr_memory_copy(disabled,&value,4))error=998;if(error)wr_current_teb()->last_error=error;return !error;
}
static int WR_WIN SetThreadPriorityBoost(void *handle,int disabled) {
    vex_thread_control_req_t r={.value=!disabled};unsigned error=thread_control(handle,VEX_THREAD_AUTOMATIC_FLOWS,&r);
    if(error)wr_current_teb()->last_error=error;return !error;
}
static int WR_WIN SetThreadInformation(void *handle,unsigned kind,const void *data,unsigned size) {
    /* ThreadPowerThrottling expresses an execution QoS preference. The native
       software policy alters deadlines, not process entitlement or guessed
       hardware P-states. Other classes aren't silently recorded as effects. */
    if(kind!=3){wr_current_teb()->last_error=50;return 0;}
    struct {uint32_t version,control,state;} policy;
    if(size!=sizeof(policy) || wr_memory_copy(&policy,data,sizeof(policy)) || policy.version!=1 ||
       (policy.control&~1u) || (policy.state&~policy.control)){wr_current_teb()->last_error=87;return 0;}
    vex_thread_control_req_t r={.value=(policy.control&policy.state&1u)!=0};unsigned error=thread_control(handle,VEX_THREAD_EFFICIENCY,&r);
    if(error)wr_current_teb()->last_error=error;return !error;
}
unsigned WR_WIN wr_thread_wait(void *h,unsigned timeout) {
    wr_thread *t=thread_pin(h);
    if(!t) {wr_current_teb()->last_error=6;return UINT32_MAX;}
    uint64_t start=wr_ticks();unsigned result=0;
    while(__atomic_load_n(&t->alive,__ATOMIC_ACQUIRE)) {
        uint64_t elapsed=wr_ticks()-start;
        if(timeout!=UINT32_MAX && elapsed>=timeout) {result=258;break;}
        wr_futex(&t->alive,0,1,timeout==UINT32_MAX?0:((uint64_t)timeout-elapsed)*1000000);
    }
    thread_unpin(t);return result;
}
int WR_WIN wr_thread_close(void *h) {
    if(h==(void *)-2){wr_current_teb()->last_error=6;return 0;}
    wr_thread *t=thread_pin(h);unsigned slot=wr_handle_slot((uintptr_t)h);wr_thread *expected=t;
    if(!t || !__atomic_compare_exchange_n(&handles[slot].thread,&expected,0,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)) {
        if(t)thread_unpin(t);wr_current_teb()->last_error=6;return 0;
    }
    __atomic_sub_fetch(&t->handle_refs,1,__ATOMIC_SEQ_CST);thread_unpin(t);return 1;
}
static int WR_WIN GetExitCodeThread(void *h,unsigned *code) {
    if(!code) {wr_current_teb()->last_error=87;return 0;}
    int primary=wr_primary_exit_code(h,code);if(primary>=0)return primary;
    wr_thread *t=thread_pin(h);
    if(t)*code=__atomic_load_n(&t->alive,__ATOMIC_ACQUIRE)?259:t->result;
    if(t)thread_unpin(t);if(!t)wr_current_teb()->last_error=6;return t!=0;
}
static void WR_WIN ExitThread(unsigned code) {end_thread(code);}
static unsigned WR_WIN GetCurrentThreadId(void) {return (unsigned)wr_current_teb()->tid;}
static unsigned WR_WIN GetThreadId(void *handle) {
    if(handle==(void *)-2)return (unsigned)wr_current_teb()->tid;
    unsigned snapshot;int captured=wr_wer_thread(handle,0,&snapshot);if(captured>=0)return captured?snapshot:0;
    unsigned id;int primary=wr_primary_id(handle,&id);if(primary>=0)return primary?id:0;
    wr_thread *thread=thread_pin(handle);id=thread?(unsigned)thread->teb.tid:0;
    if(thread)thread_unpin(thread);if(!thread)wr_current_teb()->last_error=6;return id;
}
static int WR_WIN Wow64GetThreadContext(void *handle,void *out){
    (void)out;
    if(!GetThreadId(handle))return 0;
    /* Every thread admitted by this PE32+ personality is AMD64. A WOW64/i386
       CONTEXT cannot describe it. Windows returns ERROR_INVALID_PARAMETER for
       this case too; do not truncate AMD64 registers into an invented context. */
    wr_current_teb()->last_error=87;return 0;
}
static unsigned WR_WIN GetCurrentProcessorNumber(void) {
    uint32_t cpu;if(wr_context(0,0,&cpu))wr_exit(125);return cpu;
}
static void WR_WIN Sleep(unsigned ms) {
    if(!ms) {wr_yield();return;}
    struct timespec ts={(int64_t)(ms/1000),(long)(ms%1000)*1000000};
    vex_nanosleep_req_t r={&ts,0};
    if(vex_syscall1(VEX_SYS_NANOSLEEP,(uintptr_t)&r))wr_current_teb()->last_error=50;
}
static unsigned WR_WIN QueueUserAPC(void (WR_WIN *callback)(uintptr_t),void *handle,uintptr_t value) {
    if(!callback){wr_current_teb()->last_error=87;return 0;}
    wr_thread *t=thread_pin(handle);if(!t){wr_current_teb()->last_error=6;return 0;}
    wr_apc *item=wr_native_alloc(sizeof(*item));if(!item){thread_unpin(t);wr_current_teb()->last_error=8;return 0;}
    *item=(wr_apc){0,callback,value};wr_lock(&t->apc_gate);int closed=t->apc_closed || !__atomic_load_n(&t->alive,__ATOMIC_ACQUIRE);
    if(!closed){if(t->apc_last)((wr_apc *)t->apc_last)->next=item;else t->apc_first=item;t->apc_last=item;__atomic_add_fetch(&t->apc_sequence,1,__ATOMIC_RELEASE);}
    wr_unlock(&t->apc_gate);if(closed)wr_native_free(item);else wr_futex(&t->apc_sequence,1,UINT32_MAX,0);
    thread_unpin(t);if(closed)wr_current_teb()->last_error=31;return !closed;
}
static unsigned WR_WIN SleepEx(unsigned timeout,int alertable) {
    if(!alertable && timeout!=UINT32_MAX){Sleep(timeout);return 0;}
    wr_thread *self=(void *)wr_current_teb();uint64_t start=wr_ticks();unsigned invoked=0;
    for(;;) {
        wr_lock(&self->apc_gate);unsigned sequence=self->apc_sequence;wr_apc *item=alertable?self->apc_first:0;
        if(item){self->apc_first=item->next;if(!item->next)self->apc_last=0;}
        wr_unlock(&self->apc_gate);
        if(item){void (WR_WIN *callback)(uintptr_t)=item->callback;uintptr_t value=item->value;wr_native_free(item);callback(value);invoked=1;continue;}
        if(invoked)return 0xc0;
        uint64_t elapsed=wr_ticks()-start;if(timeout!=UINT32_MAX && elapsed>=timeout){if(!timeout)wr_yield();return 0;}
        wr_futex(&self->apc_sequence,0,sequence,timeout==UINT32_MAX?0:((uint64_t)timeout-elapsed)*1000000);
    }
}
static unsigned WR_WIN TlsAlloc(void) {
    wr_loader_lock();
    for(unsigned i=0;i<64;i++)if(!(tls_bitmap&(1ull<<i))) {tls_bitmap|=1ull<<i;wr_loader_unlock();return i;}
    wr_loader_unlock();wr_current_teb()->last_error=8;return UINT32_MAX;
}
static int WR_WIN TlsFree(unsigned i) {
    wr_loader_lock();int valid=i<64 && (tls_bitmap&(1ull<<i));
    if(valid) {tls_bitmap&=~(1ull<<i);for(wr_thread *t=threads;t;t=t->next)t->teb.slots[i]=0;}
    wr_loader_unlock();if(!valid)wr_current_teb()->last_error=87;return valid;
}
static void *WR_WIN TlsGetValue(unsigned i) {
    wr_loader_lock();int valid=i<64 && (tls_bitmap&(1ull<<i));
    void *p=valid?wr_current_teb()->slots[i]:0;wr_loader_unlock();
    wr_current_teb()->last_error=valid?0:87;return p;
}
static int WR_WIN TlsSetValue(unsigned i,void *value) {
    wr_loader_lock();int valid=i<64 && (tls_bitmap&(1ull<<i));
    if(valid)wr_current_teb()->slots[i]=value;wr_loader_unlock();
    if(!valid)wr_current_teb()->last_error=87;return valid;
}
static uint16_t *WR_WIN GetCommandLineW(void) {return params.command_line.buffer;}
typedef struct {unsigned state;void (WR_WIN *callback)(void *);} fls_slot;
static fls_slot fls_slots[128];
static unsigned WR_WIN FlsAlloc(void (WR_WIN *callback)(void *)) {
    if(callback){vex_memory_view_info_t info;if(wr_memory_query((uintptr_t)callback,&info) || !(info.permissions&4)){wr_current_teb()->last_error=87;return UINT32_MAX;}}
    wr_loader_lock();unsigned i=0;while(i<128 && fls_slots[i].state)i++;
    if(i<128){fls_slots[i]=(fls_slot){1,callback};for(wr_thread *t=threads;t;t=t->next)t->fls[i]=0;}
    wr_loader_unlock();if(i==128){wr_current_teb()->last_error=8;return UINT32_MAX;}return i;
}
static void *WR_WIN FlsGetValue(unsigned index) {
    wr_loader_lock();int valid=index<128 && fls_slots[index].state==1;
    void *result=valid?((wr_thread *)wr_current_teb())->fls[index]:0;wr_loader_unlock();
    wr_current_teb()->last_error=valid?0:87;return result;
}
static int WR_WIN FlsSetValue(unsigned index,void *value) {
    wr_loader_lock();int valid=index<128 && fls_slots[index].state==1;
    if(valid)((wr_thread *)wr_current_teb())->fls[index]=value;
    wr_loader_unlock();if(!valid)wr_current_teb()->last_error=87;return valid;
}
void wr_fls_thread_exit(wr_thread *t) {
    /* Detach before invoking userspace code; no loader lock or raw thread-list
     * reference is held across callbacks. Free and normal exit take a value
     * exactly once under the existing thread inventory transaction. */
    for(unsigned i=0;i<128;i++) {
        wr_loader_lock();void *value=t->fls[i];t->fls[i]=0;
        void (WR_WIN *callback)(void *)=fls_slots[i].state==1?fls_slots[i].callback:0;
        wr_loader_unlock();if(value && callback)callback(value);
    }
}
static int WR_WIN FlsFree(unsigned index) {
    wr_loader_lock();if(index>=128 || fls_slots[index].state!=1){wr_loader_unlock();wr_current_teb()->last_error=87;return 0;}
    unsigned count=0;for(wr_thread *t=threads;t;t=t->next)if(t->fls[index])count++;
    void **values=count?wr_native_alloc((size_t)count*sizeof(void *)):0;
    if(count && !values){wr_loader_unlock();wr_current_teb()->last_error=8;return 0;}
    void (WR_WIN *callback)(void *)=fls_slots[index].callback;fls_slots[index].state=2;
    unsigned at=0;for(wr_thread *t=threads;t;t=t->next)if(t->fls[index]){values[at++]=t->fls[index];t->fls[index]=0;}
    wr_loader_unlock();for(unsigned i=0;i<count;i++)if(callback)callback(values[i]);
    if(values)wr_native_free(values);wr_loader_lock();fls_slots[index]=(fls_slot){0};wr_loader_unlock();return 1;
}
static char *WR_WIN GetCommandLineA(void) {return command_line;}
void wr_env_stop_peers(unsigned status) {
    wr_thread *self=(void *)wr_current_teb();wr_loader_lock();exiting=1;
    size_t count=0;for(wr_thread *t=threads;t;t=t->next)if(t!=self)count++;
    wr_thread **saved=count?wr_native_alloc(count*sizeof(*saved)):0;
    if(count && !saved) {wr_loader_unlock();wr_exit(125);}
    size_t at=0;
    /* Do not stop a peer while it owns userspace object metadata. These gates
     * never require loader_gate, so this order also matches record retirement.
     * Keep them until native clear-on-exit confirms all peer contexts stopped. */
    wr_winsock_quiesce(1);wr_token_quiesce(1);wr_user_quiesce(1);wr_console_quiesce(1);wr_registered_wait_quiesce(1);wr_object_quiesce(1);wr_completion_quiesce(1);wr_job_quiesce(1);wr_files_quiesce(1);wr_pipes_quiesce(1);wr_process_quiesce(1);wr_namespace_quiesce(1);wr_environment_quiesce(1);
    for(wr_thread *t=threads;t;t=t->next)if(t!=self) {
        saved[at++]=t;__atomic_add_fetch(&t->handle_refs,1,__ATOMIC_SEQ_CST);
        wr_lock(&t->apc_gate);
        if(__atomic_load_n(&t->alive,__ATOMIC_ACQUIRE))wr_stop_thread((uint32_t)t->teb.tid,status);
    }
    wr_loader_unlock();
    for(size_t i=0;i<count;i++) {wr_thread *t=saved[i];
        while(__atomic_load_n(&t->alive,__ATOMIC_ACQUIRE))wr_futex(&t->alive,0,1,0);
        wr_unlock(&t->apc_gate);
    }
    wr_registry_after_stop();wr_files_stop_worker_locked();wr_environment_quiesce(0);wr_namespace_quiesce(0);wr_process_quiesce(0);wr_pipes_quiesce(0);wr_files_quiesce(0);wr_job_quiesce(0);wr_completion_quiesce(0);wr_object_quiesce(0);wr_registered_wait_quiesce(0);wr_console_quiesce(0);wr_user_quiesce(0);wr_token_quiesce(0);wr_winsock_quiesce(0);
    /* Wait registrations and mutex ownership belong to the dead execution
       context, not to its still-open inspection HANDLE. Retire them before
       DLL_PROCESS_DETACH can signal or wait on these userspace objects. */
    for(size_t i=0;i<count;i++){wr_object_thread_exit(saved[i]);wr_winsock_thread_exit((uint32_t)saved[i]->teb.tid);}
    wr_loader_lock();for(size_t i=0;i<count;i++)__atomic_sub_fetch(&saved[i]->handle_refs,1,__ATOMIC_SEQ_CST);reap();wr_loader_unlock();
    if(saved)wr_native_free(saved);
}
void *wr_thread_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(CreateThread);E(ResumeThread);E(SuspendThread);E(GetThreadContext);E(QueryThreadCycleTime);E(SwitchToThread);E(GetExitCodeThread);E(ExitThread);
    E(GetCurrentThreadId);E(GetCurrentProcessorNumber);E(Sleep);E(TlsAlloc);E(TlsFree);E(TlsGetValue);E(TlsSetValue);E(GetCommandLineW);E(GetCommandLineA);
    E(GetThreadId);E(Wow64GetThreadContext);E(SleepEx);E(QueueUserAPC);
    E(GetThreadPriority);E(SetThreadPriority);E(GetThreadPriorityBoost);E(SetThreadPriorityBoost);E(SetThreadInformation);
    E(SetThreadAffinityMask);
    E(FlsAlloc);E(FlsFree);E(FlsGetValue);E(FlsSetValue);
#undef E
    return 0;
}
