#include "windows.h"
typedef struct registration {unsigned refs,timeout,flags;uint32_t cancel,done,tid,callback_active;
    void *object,*completion;void (WR_WIN *callback)(void *,uint8_t);void *argument;} registration;
static struct {registration *object;uint32_t generation;} handles[256];
static wr_mutex gate;
static void release(registration *r){wr_lock(&gate);int dead=!--r->refs;wr_unlock(&gate);if(dead)wr_native_free(r);}
static unsigned WR_WIN worker(void *argument) {
    registration *r=argument;wr_lock(&gate);r->tid=wr_tid();wr_unlock(&gate);
    for(;;) {
        unsigned result=wr_object_wait_reference(r->object,r->timeout,&r->cancel);
        wr_lock(&gate);int stop=r->cancel || result==UINT32_MAX;
        if(!stop)r->callback_active=1;wr_unlock(&gate);if(stop)break;
        r->callback(r->argument,result==258);
        wr_lock(&gate);r->callback_active=0;stop=r->cancel || (r->flags&8);wr_unlock(&gate);if(stop)break;
    }
    wr_object_wait_release(r->object);
    wr_lock(&gate);void *completion=r->completion;r->completion=0;
    __atomic_store_n(&r->done,1,__ATOMIC_RELEASE);wr_futex(&r->done,1,UINT32_MAX,0);wr_unlock(&gate);
    if(completion)wr_event_io_finish(&completion);release(r);return 0;
}
static int WR_WIN RegisterWaitForSingleObject(void **out,void *object,void (WR_WIN *callback)(void *,uint8_t),void *argument,unsigned timeout,unsigned flags) {
    if(!out || !callback){wr_current_teb()->last_error=87;return 0;}
    if(flags&~(8u|4u|16u)){wr_current_teb()->last_error=50;return 0;}
    void *none=0;if(wr_memory_copy(out,&none,sizeof(none))){wr_current_teb()->last_error=998;return 0;}
    void *reference=wr_object_wait_acquire(object);if(!reference){wr_current_teb()->last_error=6;return 0;}
    registration *r=wr_native_alloc(sizeof(*r));if(!r){wr_object_wait_release(reference);wr_current_teb()->last_error=8;return 0;}
    memset(r,0,sizeof(*r));r->refs=2;r->object=reference;r->timeout=timeout;r->flags=flags;r->callback=callback;r->argument=argument;
    wr_lock(&gate);unsigned i=0;while(i<256 && (handles[i].object || handles[i].generation>=WR_HANDLE_GENERATION_MAX))i++;
    if(i==256){wr_unlock(&gate);wr_object_wait_release(reference);wr_native_free(r);wr_current_teb()->last_error=8;return 0;}
    if(!++handles[i].generation)++handles[i].generation;handles[i].object=r;
    void *handle=(void *)(wr_handle_pack(0x53,handles[i].generation,i));wr_unlock(&gate);
    void *(WR_WIN *create)(void *,size_t,unsigned (WR_WIN *)(void *),void *,unsigned,unsigned *)=wr_thread_exports("CreateThread");
    int (WR_WIN *close)(void *)=wr_object_exports("CloseHandle");void *thread=create(0,0,worker,r,0,0);
    if(!thread){wr_lock(&gate);handles[i].object=0;wr_unlock(&gate);wr_object_wait_release(reference);wr_native_free(r);return 0;}
    close(thread);
    if(wr_memory_copy(out,&handle,sizeof(handle))){wr_lock(&gate);handles[i].object=0;wr_unlock(&gate);wr_object_wait_cancel(&r->cancel);release(r);wr_current_teb()->last_error=998;return 0;}
    return 1;
}
static int WR_WIN UnregisterWaitEx(void *handle,void *completion_event) {
    uintptr_t v=(uintptr_t)handle;unsigned i=(v>>2)&255;void *completion=0;
    if(completion_event && completion_event!=(void *)-1){completion=wr_event_io_acquire(completion_event);if(!completion){wr_current_teb()->last_error=6;return 0;}}
    wr_lock(&gate);registration *r=0;
    if(wr_handle_tag(v)==0x53 && !(v&3) && wr_handle_generation(v)==handles[i].generation)r=handles[i].object;
    if(!r || (completion_event==(void *)-1 && r->tid==wr_tid())){wr_unlock(&gate);wr_event_io_release(completion);wr_current_teb()->last_error=r?997:6;return 0;}
    handles[i].object=0;r->refs++;unsigned active=r->callback_active;int done=r->done;
    if(!done && completion){r->completion=completion;completion=0;}wr_unlock(&gate);
    wr_object_wait_cancel(&r->cancel);
    if(done && completion)wr_event_io_finish(&completion);
    if(completion_event==(void *)-1)while(!__atomic_load_n(&r->done,__ATOMIC_ACQUIRE))wr_futex(&r->done,0,0,0);
    release(r);release(r);
    if(active && completion_event!=(void *)-1){wr_current_teb()->last_error=997;return 0;}return 1;
}
static int WR_WIN UnregisterWait(void *handle){return UnregisterWaitEx(handle,0);}
void wr_registered_wait_quiesce(int begin){if(begin)wr_lock(&gate);else wr_unlock(&gate);}
void *wr_registered_wait_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(RegisterWaitForSingleObject);E(UnregisterWaitEx);E(UnregisterWait);
#undef E
    return 0;
}
