#include "windows.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
static wr_mutex gate;
static int (WR_WIN *handlers[64])(unsigned);
static unsigned count,initialized,ignore_interrupt;
static uint32_t pending,sequence;
extern void wr_control_entry(void);
static _Noreturn void event_return(void){vex_syscall0(VEX_SYS_EVENT_RETURN);__builtin_trap();}
/* Entered on the existing native event stack/context engine. No userspace
 * mutex, allocation or Windows callback is allowed on the interrupted thread. */
void wr_control_dispatch(uint64_t value,const void *payload,void *context,void *xstate) {
    (void)payload;(void)context;(void)xstate;
    __atomic_fetch_or(&pending,(uint32_t)value,__ATOMIC_RELEASE);
    __atomic_add_fetch(&sequence,1,__ATOMIC_RELEASE);wr_futex(&sequence,1,UINT32_MAX,0);
}
static unsigned WR_WIN worker(void *unused) {
    (void)unused;
    for(;;){unsigned old=__atomic_load_n(&sequence,__ATOMIC_ACQUIRE);
        uint32_t events=__atomic_exchange_n(&pending,0,__ATOMIC_ACQ_REL);
        if(!events){wr_futex(&sequence,0,old,0);continue;}
        for(unsigned code=0;code<6;code++)if(events&(1u<<code)) {
            int (WR_WIN *snapshot[64])(unsigned);wr_lock(&gate);unsigned n=count;
            int ignore=!code && ignore_interrupt;memcpy(snapshot,handlers,n*sizeof(*snapshot));wr_unlock(&gate);
            int handled=ignore;while(!handled && n)handled=snapshot[--n](code)!=0;
            if(!handled)wr_finish(0xc000013au);
        }
    }
}
static int initialize(void) {
    /* Caller owns the loader gate, so CreateThread cannot invert init/loader
       ordering with process teardown or a concurrent registration. */
    if(initialized)return 1;
    const unsigned classes[4]={VEX_EVENT_USER_INTERRUPT,VEX_EVENT_QUIT,VEX_EVENT_HANGUP,VEX_EVENT_TERMINATE};
    const unsigned codes[4]={0,1,2,5};unsigned i=0;
    for(;i<4;i++){vex_event_register_req_t r={.event_id=(uint16_t)classes[i],.handler=(uintptr_t)wr_control_entry,
        .restorer=(uintptr_t)event_return,.flags=VEX_EVENT_SIGINFO|VEX_EVENT_ON_ALT_STACK,.delivery_value=1u<<codes[i]};
        if(vex_syscall1(VEX_SYS_EVENT_REGISTER,(uintptr_t)&r))break;}
    if(i==4){void *(WR_WIN *create)(void *,size_t,unsigned (WR_WIN *)(void *),void *,unsigned,unsigned *)=wr_thread_exports("CreateThread");
        void *thread=create(0,0,worker,0,0,0);if(thread){int (WR_WIN *close)(void *)=wr_object_exports("CloseHandle");close(thread);initialized=1;return 1;}}
    while(i){vex_event_register_req_t r={.event_id=(uint16_t)classes[--i]};vex_syscall1(VEX_SYS_EVENT_REGISTER,(uintptr_t)&r);}
    return 0;
}
static int WR_WIN SetConsoleCtrlHandler(int (WR_WIN *handler)(unsigned),int add) {
    wr_loader_lock();int ready=initialize();if(!ready){wr_loader_unlock();wr_current_teb()->last_error=8;return 0;}
    unsigned error=0;wr_lock(&gate);
    if(!handler)ignore_interrupt=add!=0;
    else if(add){if(count==64)error=8;else handlers[count++]=handler;}
    else {unsigned i=count;while(i && handlers[i-1]!=handler)i--;if(!i)error=87;
        else {i--;for(unsigned j=i+1;j<count;j++)handlers[j-1]=handlers[j];count--;}}
    wr_unlock(&gate);wr_loader_unlock();if(error)wr_current_teb()->last_error=error;return !error;
}
void wr_console_quiesce(int begin){if(begin)wr_lock(&gate);else wr_unlock(&gate);}
void *wr_console_control_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(SetConsoleCtrlHandler);
#undef E
    return 0;
}
