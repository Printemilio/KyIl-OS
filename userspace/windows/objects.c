#include "windows.h"
#include "process.h"
#include "job.h"
#include "wer.h"
#include "event_protocol.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
/* Unnamed Windows events are userspace objects. A native futex waits on each
 * registered waiter; the object lock makes Set/Reset and registration atomic.
 * In particular Reset cannot revoke a waiter already released by SetEvent.
 * Explicitly shareable events use an Ultra-Space service and endpoint
 * capabilities. Named objects/security descriptors remain unsupported here.
 * No Windows object engine in kernel. */
struct wait_group;
typedef struct waiter {struct waiter *next;uint32_t released,status;wr_thread *thread;uint32_t *cancel;struct wait_group *group;} waiter;
typedef struct event {unsigned refs,manual,signaled,mutex,recursion,abandoned,semaphore,count,maximum;
    waiter *first,*last;wr_thread *owner;struct event *next;int remote;} event;
static struct {event *object;uint32_t generation,flags;} handles[256];
static event *objects;
static wr_mutex gate;
#include "remote_event.inc"
unsigned wr_object_handle_count(void){wr_lock(&gate);unsigned count=0;for(unsigned i=0;i<256;i++)if(handles[i].object)count++;wr_unlock(&gate);return count;}
typedef struct wait_group {uint32_t done,result;unsigned count,all;wr_thread *thread;event *objects[64];waiter links[64];} wait_group;
static int try_group(wait_group *);
static unsigned take(event *e,wr_thread *thread){
    if(e->semaphore){e->count--;return 0;}
    if(e->mutex){unsigned status=0;if(e->owner)e->recursion++;
        else{e->owner=thread;e->recursion=1;e->refs++;status=e->abandoned?128:0;e->abandoned=0;}return status;}
    if(!e->manual)e->signaled=0;return 0;
}
static int available(event *e,wr_thread *thread){return e->semaphore?e->count!=0:e->mutex?(!e->owner || e->owner==thread):e->signaled!=0;}
static void dispatch(event *e){
    for(waiter *w=e->first;w && available(e,w->thread);w=w->next)if(!w->released){
        if(w->group){try_group(w->group);continue;}
        w->status=take(e,w->thread);__atomic_store_n(&w->released,1,__ATOMIC_RELEASE);wr_futex(&w->released,1,1,0);
    }
}
static int is_event(void *h) {return (wr_handle_tag((uintptr_t)h))==0x59;}
static event *lookup(void *h,unsigned *slot) {
    uintptr_t v=(uintptr_t)h;unsigned i=(unsigned)((v>>2)&255);
    if(!is_event(h) || (v&3) || wr_handle_generation(v)!=handles[i].generation || !handles[i].object)return 0;
    if(slot)*slot=i;return handles[i].object;
}
const char *wr_object_type(void *h){
    wr_lock(&gate);event *e=lookup(h,0);const char *name=e?(e->mutex?"Mutant":e->semaphore?"Semaphore":"Event"):0;
    wr_unlock(&gate);return name;
}
static void release(event *e) {
    if(--e->refs)return;
    event **p=&objects;while(*p!=e)p=&(*p)->next;*p=e->next;event_close_fd(e->remote);wr_native_free(e);
}
static int try_group(wait_group *g){
    if(g->done)return 1;
    unsigned first=UINT32_MAX,abandoned=UINT32_MAX;
    for(unsigned i=0;i<g->count;i++){
        event *e=g->objects[i];int ready=available(e,g->thread);
        if(!ready && g->all)return 0;
        if(ready && first==UINT32_MAX)first=i;
    }
    if(first==UINT32_MAX)return 0;
    for(unsigned i=0;i<g->count;i++)if(g->all || i==first){
        event *e=g->objects[i];if(e->mutex && e->owner==g->thread && e->recursion==UINT32_MAX){
            g->result=UINT32_MAX;goto complete;
        }
    }
    for(unsigned i=0;i<g->count;i++)if(g->all || i==first){if(take(g->objects[i],g->thread)==128 && abandoned==UINT32_MAX)abandoned=i;}
    g->result=abandoned!=UINT32_MAX?128+abandoned:g->all?0:first;
complete:
    for(unsigned i=0;i<g->count;i++)g->links[i].released=1;
    __atomic_store_n(&g->done,1,__ATOMIC_RELEASE);wr_futex(&g->done,1,1,0);return 1;
}
static void *create(void *security,int manual,int initial,const uint16_t *name,int mutex) {
    unsigned inherit=0;if(security){struct {uint32_t size,pad;void *descriptor;int inherit,pad2;} a;
        if(wr_memory_copy(&a,security,sizeof(a)) || a.size!=sizeof(a)){wr_current_teb()->last_error=87;return NULL;}
        if(a.descriptor || mutex){wr_current_teb()->last_error=50;return NULL;}inherit=a.inherit!=0;}
    if(name){wr_current_teb()->last_error=50;return NULL;}
    event *e=wr_native_alloc(sizeof(*e));if(!e) {wr_current_teb()->last_error=8;return 0;}
    memset(e,0,sizeof(*e));e->remote=-1;e->refs=1;e->manual=manual!=0;e->signaled=initial!=0;
    if(security){unsigned error=0;e->remote=shared_event_create(manual!=0,initial!=0,&error);if(error){wr_native_free(e);wr_current_teb()->last_error=error;return NULL;}}
    e->mutex=mutex;e->signaled=mutex?0:e->signaled;
    if(mutex && initial){e->owner=(void *)wr_current_teb();e->recursion=1;e->refs++;}
    wr_lock(&gate);unsigned i=0;while(i<256 && (handles[i].object || handles[i].generation>=WR_HANDLE_GENERATION_MAX))i++;
    if(i==256) {wr_unlock(&gate);event_close_fd(e->remote);wr_native_free(e);wr_current_teb()->last_error=8;return 0;}
    if(!++handles[i].generation)++handles[i].generation;
    e->next=objects;objects=e;
    handles[i].object=e;handles[i].flags=inherit;void *h=(void *)(wr_handle_pack(0x59,handles[i].generation,i));
    wr_unlock(&gate);return h;
}
static void *WR_WIN CreateEventW(void *security,int manual,int initial,const uint16_t *name) {return create(security,manual,initial,name,0);}
static void *WR_WIN CreateMutexW(void *security,int initial,const uint16_t *name) {return create(security,0,initial,name,1);}
static void *WR_WIN CreateSemaphoreW(void *security,int initial,int maximum,const uint16_t *name) {
    if(security){wr_current_teb()->last_error=50;return NULL;}
    if(initial<0 || maximum<=0 || initial>maximum){wr_current_teb()->last_error=87;return 0;}
    void *h=create(security,0,0,name,0);if(!h)return 0;
    wr_lock(&gate);event *e=lookup(h,0);e->semaphore=1;e->count=(unsigned)initial;e->maximum=(unsigned)maximum;wr_unlock(&gate);return h;
}
static int WR_WIN ReleaseSemaphore(void *h,int count,int *previous) {
    wr_lock(&gate);event *e=lookup(h,0);unsigned why=0;
    if(!e || !e->semaphore)why=6;else if(count<=0)why=87;else if((unsigned)count>e->maximum-e->count)why=298;
    else {
        if(previous)*previous=(int)e->count;e->count+=(unsigned)count;
        dispatch(e);
    }
    wr_unlock(&gate);if(why)wr_current_teb()->last_error=why;return !why;
}
static void *WR_WIN CreateEventA(void *security,int manual,int initial,const char *name) {
    uint16_t wide[256];if(name && wr_utf16(name,wide,256)<0){wr_current_teb()->last_error=1113;return 0;}return CreateEventW(security,manual,initial,name?wide:0);
}
static void *WR_WIN CreateMutexA(void *security,int initial,const char *name) {
    uint16_t wide[256];if(name && wr_utf16(name,wide,256)<0){wr_current_teb()->last_error=1113;return 0;}return CreateMutexW(security,initial,name?wide:0);
}
static void *WR_WIN CreateSemaphoreA(void *security,int initial,int maximum,const char *name) {
    uint16_t wide[256];if(name && wr_utf16(name,wide,256)<0){wr_current_teb()->last_error=1113;return 0;}return CreateSemaphoreW(security,initial,maximum,name?wide:0);
}
static void signal(event *e) {
    e->signaled=1;dispatch(e);
}
static int WR_WIN SetEvent(void *h) {
    wr_lock(&gate);event *e=lookup(h,0);
    if(e && (e->mutex || e->semaphore))e=0;
    if(e && e->remote>=0){if(e->refs==UINT32_MAX){wr_unlock(&gate);wr_current_teb()->last_error=8;return 0;}e->refs++;int fd=e->remote;wr_unlock(&gate);
        we_packet p={.operation=WE_SET};unsigned error=event_rpc(fd,&p,NULL,NULL);wr_lock(&gate);release(e);wr_unlock(&gate);if(error)wr_current_teb()->last_error=error;return !error;}
    if(e)signal(e);
    wr_unlock(&gate);if(!e)wr_current_teb()->last_error=6;return e!=0;
}
void *wr_event_io_acquire(void *h) {
    wr_lock(&gate);event *e=lookup(h,0);
    if(e && (e->mutex || e->semaphore || e->remote>=0 || e->refs==UINT32_MAX))e=0;if(e)e->refs++;
    wr_unlock(&gate);return e;
}
void wr_event_io_reset(void *opaque) {wr_lock(&gate);((event *)opaque)->signaled=0;wr_unlock(&gate);}
void wr_event_io_release(void *opaque) {if(opaque){wr_lock(&gate);release(opaque);wr_unlock(&gate);}}
void wr_event_io_finish(void **reference) {
    if(!*reference)return;wr_lock(&gate);event *e=*reference;*reference=0;signal(e);release(e);wr_unlock(&gate);
}
static int WR_WIN ResetEvent(void *h) {
    wr_lock(&gate);event *e=lookup(h,0);if(e && (e->mutex || e->semaphore))e=0;
    if(e && e->remote>=0){if(e->refs==UINT32_MAX){wr_unlock(&gate);wr_current_teb()->last_error=8;return 0;}e->refs++;int fd=e->remote;wr_unlock(&gate);
        we_packet p={.operation=WE_RESET};unsigned error=event_rpc(fd,&p,NULL,NULL);wr_lock(&gate);release(e);wr_unlock(&gate);if(error)wr_current_teb()->last_error=error;return !error;}
    if(e)e->signaled=0;
    wr_unlock(&gate);if(!e)wr_current_teb()->last_error=6;return e!=0;
}
static void release_owner(event *e,unsigned abandoned) {
    e->owner=0;e->recursion=0;e->abandoned=abandoned;dispatch(e);release(e);
}
void wr_object_thread_exit(wr_thread *t) {
    wr_lock(&gate);
    for(event *e=objects,*next;e;e=next) {
        next=e->next;e->refs++; /* Keep object alive while cancelling wait refs. */
        waiter **link=&e->first,*previous=0;
        while(*link) {
            waiter *w=*link;
            if(w->thread==t){*link=w->next;if(e->last==w)e->last=previous;release(e);}
            else {previous=w;link=&w->next;}
        }
        if(e->mutex && e->owner==t)release_owner(e,1);release(e);
    }
    void *group=t->multiple_wait;t->multiple_wait=0;
    wr_unlock(&gate);if(group)wr_native_free(group);
}
void wr_object_quiesce(int begin) {if(begin)wr_lock(&gate);else wr_unlock(&gate);}
static int WR_WIN ReleaseMutex(void *h) {
    wr_lock(&gate);event *e=lookup(h,0);unsigned error=0;
    if(!e || !e->mutex)error=6;
    else if(e->owner!=(wr_thread *)wr_current_teb())error=288;
    else if(!--e->recursion)release_owner(e,0);
    wr_unlock(&gate);if(error)wr_current_teb()->last_error=error;return !error;
}
void *wr_object_wait_acquire(void *h) {
    wr_lock(&gate);event *e=lookup(h,0);if(e && (e->mutex || e->refs==UINT32_MAX))e=0;if(e)e->refs++;wr_unlock(&gate);return e;
}
void wr_object_wait_release(void *object){wr_lock(&gate);release(object);wr_unlock(&gate);}
void wr_object_wait_cancel(uint32_t *cancel) {
    wr_lock(&gate);__atomic_store_n(cancel,1,__ATOMIC_RELEASE);
    for(remote_wait *w=remote_waits;w;w=w->next)if(w->cancel==cancel)event_cancel_fd(w->fd);
    for(event *e=objects;e;e=e->next) {
        for(waiter *w=e->first;w;w=w->next)if(w->cancel==cancel && !w->released) {
            w->status=UINT32_MAX;__atomic_store_n(&w->released,1,__ATOMIC_RELEASE);wr_futex(&w->released,1,1,0);
        }
    }
    wr_unlock(&gate);
}
unsigned wr_object_wait_reference(void *object,unsigned timeout,uint32_t *cancel) {
    event *e=object;wr_lock(&gate);
    if(cancel && __atomic_load_n(cancel,__ATOMIC_ACQUIRE)){wr_unlock(&gate);return UINT32_MAX;}
    if(e->remote>=0){int fd=e->remote;wr_unlock(&gate);we_packet p={.operation=WE_WAIT,.timeout=timeout};unsigned error=event_rpc(fd,&p,cancel,NULL);if(error){wr_current_teb()->last_error=error;return UINT32_MAX;}return p.result;}
    if(e->semaphore && e->count){e->count--;wr_unlock(&gate);return 0;}
    if(e->mutex && (!e->owner || e->owner==(wr_thread *)wr_current_teb())) {
        unsigned result=0;
        if(e->owner){if(e->recursion==UINT32_MAX){wr_unlock(&gate);wr_current_teb()->last_error=8;return UINT32_MAX;}e->recursion++;}
        else {e->owner=(void *)wr_current_teb();e->recursion=1;e->refs++;result=e->abandoned?128:0;e->abandoned=0;}
        wr_unlock(&gate);return result;
    }
    if(e->signaled) {if(!e->manual)e->signaled=0;wr_unlock(&gate);return 0;}
    if(!timeout) {wr_unlock(&gate);return 258;}
    waiter w={.thread=(void *)wr_current_teb(),.cancel=cancel};e->refs++;if(e->last)e->last->next=&w;else e->first=&w;e->last=&w;
    wr_unlock(&gate);uint64_t start=wr_ticks();unsigned result=0;
    while(!__atomic_load_n(&w.released,__ATOMIC_ACQUIRE)) {
        uint64_t elapsed=wr_ticks()-start;
        if(timeout!=UINT32_MAX && elapsed>=timeout) {result=258;break;}
        wr_futex(&w.released,0,0,timeout==UINT32_MAX?0:((uint64_t)timeout-elapsed)*1000000);
    }
    wr_lock(&gate);
    /* Resolve timeout vs SetEvent while still holding the registration lock. */
    if(__atomic_load_n(&w.released,__ATOMIC_ACQUIRE))result=w.status;
    waiter **p=&e->first,*previous=0;
    while(*p!=&w) {previous=*p;p=&(*p)->next;}
    *p=w.next;if(e->last==&w)e->last=previous;release(e);wr_unlock(&gate);return result;
}
static unsigned WR_WIN WaitForSingleObject(void *h,unsigned timeout) {
    unsigned job;if(wr_job_wait(h,timeout,&job))return job;
    unsigned result;if(wr_process_wait(h,timeout,&result) || wr_file_wait(h,timeout,&result))return result;
    if(!is_event(h))return wr_thread_wait(h,timeout);
    wr_lock(&gate);event *e=lookup(h,0);if(e)e->refs++;wr_unlock(&gate);
    if(!e){wr_current_teb()->last_error=6;return UINT32_MAX;}
    result=wr_object_wait_reference(e,timeout,0);wr_object_wait_release(e);return result;
}
unsigned WR_WIN wr_wait_one(void *h,unsigned timeout) {return WaitForSingleObject(h,timeout);}
static unsigned WR_WIN WaitForMultipleObjects(unsigned count,const void *input,int all,unsigned timeout){
    if(!count || count>64 || !input){wr_current_teb()->last_error=87;return UINT32_MAX;}
    void *snapshot[64];if(wr_memory_copy(snapshot,input,count*sizeof(void *))){wr_current_teb()->last_error=998;return UINT32_MAX;}
    for(unsigned i=0;i<count;i++)for(unsigned j=0;j<i;j++)if(snapshot[i]==snapshot[j]){wr_current_teb()->last_error=87;return UINT32_MAX;}
    if(count==1)return WaitForSingleObject(snapshot[0],timeout);
    for(unsigned i=0;i<count;i++)if(!is_event(snapshot[i])){
        unsigned tag=(unsigned)(wr_handle_tag((uintptr_t)snapshot[i]));
        wr_current_teb()->last_error=(tag>=0x54 && tag<=0x5e) || wr_handle_tag((uintptr_t)snapshot[i])==0x5a || snapshot[i]==(void *)-1 || snapshot[i]==(void *)-2?50:6;
        return UINT32_MAX;/* Mixed native process/IO waitsets need retained observers. */
    }
    wait_group *g=wr_native_alloc(sizeof(*g));if(!g){wr_current_teb()->last_error=8;return UINT32_MAX;}
    memset(g,0,sizeof(*g));g->all=all!=0;g->count=count;g->thread=(void *)wr_current_teb();
    wr_lock(&gate);unsigned retained=0,error=0;
    for(unsigned i=0;i<count;i++){
        event *e=lookup(snapshot[i],0);if(!e){error=6;break;}if(e->remote>=0){error=50;break;}if(e->refs==UINT32_MAX){error=8;break;}
        g->objects[i]=e;e->refs++;retained++;
    }
    if(!error && !try_group(g) && timeout){
        g->thread->multiple_wait=g;
        for(unsigned i=0;i<count;i++){
            event *e=g->objects[i];waiter *w=&g->links[i];w->thread=g->thread;w->group=g;
            if(e->last)e->last->next=w;else e->first=w;e->last=w;
        }
        wr_unlock(&gate);uint64_t start=wr_ticks();
        while(!__atomic_load_n(&g->done,__ATOMIC_ACQUIRE)){
            uint64_t elapsed=wr_ticks()-start;if(timeout!=UINT32_MAX && elapsed>=timeout)break;
            wr_futex(&g->done,0,0,timeout==UINT32_MAX?0:((uint64_t)timeout-elapsed)*1000000);
        }
        wr_lock(&gate);
        for(unsigned i=0;i<count;i++){
            event *e=g->objects[i];waiter *target=&g->links[i],**link=&e->first,*previous=0;
            while(*link!=target){previous=*link;link=&(*link)->next;}
            *link=target->next;if(e->last==target)e->last=previous;
        }
        g->thread->multiple_wait=0;
    }
    unsigned result=error?UINT32_MAX:g->done?g->result:258;
    for(unsigned i=0;i<retained;i++)release(g->objects[i]);wr_unlock(&gate);wr_native_free(g);
    if(error || result==UINT32_MAX)wr_current_teb()->last_error=error?error:8;return result;
}
static int WR_WIN CloseHandle(void *h) {
    int snapshot=wr_wer_thread_close(h);if(snapshot>=0)return snapshot;
    int job=wr_job_close(h);if(job>=0)return job;
    int registry=wr_registry_close(h);if(registry>=0)return registry;
    int process=wr_process_close(h);if(process>=0)return process;
    int pipe=wr_pipe_close(h);if(pipe>=0)return pipe;
    int completion=wr_completion_close(h);if(completion>=0)return completion;
    int file=wr_file_close(h);if(file>=0)return file;
    int mapped=wr_mapping_close(h);if(mapped>=0)return mapped;
    if(!is_event(h))return wr_thread_close(h);
    wr_lock(&gate);unsigned i;event *e=lookup(h,&i);
    if(e && (handles[i].flags&2)){wr_unlock(&gate);wr_current_teb()->last_error=5;return 0;}
    if(e) {handles[i].object=0;release(e);}
    wr_unlock(&gate);if(!e)wr_current_teb()->last_error=6;return e!=0;
}
int wr_object_flags(void *h,unsigned mask,unsigned flags,unsigned *out){if(!is_event(h))return -1;
    wr_lock(&gate);unsigned i;event *e=lookup(h,&i);unsigned error=0;if(!e)error=6;
    else if((mask&flags&1) && e->remote<0)error=50;
    else{if(out)*out=handles[i].flags;handles[i].flags=(handles[i].flags&~mask)|(flags&mask);}
    wr_unlock(&gate);if(error)wr_current_teb()->last_error=error;return !error;
}
void *wr_object_duplicate(void *h,int inherit){wr_lock(&gate);event *e=lookup(h,NULL);unsigned i=0;
    if(e)while(i<256 && (handles[i].object || handles[i].generation>=WR_HANDLE_GENERATION_MAX))i++;
    void *result=NULL;unsigned error=!e?6:i==256 || e->refs==UINT32_MAX?8:inherit && e->remote<0?50:0;
    if(!error){++e->refs;++handles[i].generation;handles[i].object=e;handles[i].flags=inherit?1:0;result=(void *)wr_handle_pack(0x59,handles[i].generation,i);}
    wr_unlock(&gate);if(error)wr_current_teb()->last_error=error;return result;
}
int wr_object_export(void *h,wr_inherited_file *out,int slot){wr_lock(&gate);unsigned i;event *e=lookup(h,&i);int fd=-1;
    if(e && e->remote>=0 && (handles[i].flags&1)){fd=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,e->remote);
        if(fd>=0)*out=(wr_inherited_file){(uintptr_t)h,slot,1,handles[i].flags,0};}
    wr_unlock(&gate);return fd;
}
int wr_objects_inherit(wr_inherited_file *out,unsigned *count){wr_lock(&gate);
    for(unsigned i=0;i<256;i++)if(handles[i].object && (handles[i].flags&1)){
        if(*count>=WR_BOOT_MAX_FILES-6 || handles[i].object->remote<0){wr_unlock(&gate);return -1;}
        int fd=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,handles[i].object->remote);if(fd<0){wr_unlock(&gate);return -1;}
        out[(*count)++]=(wr_inherited_file){wr_handle_pack(0x59,handles[i].generation,i),fd,1,handles[i].flags,0};}
    wr_unlock(&gate);return 0;
}
int wr_object_adopt(const wr_inherited_file *r){unsigned i=wr_handle_slot(r->handle);
    if(wr_handle_tag(r->handle)!=0x59 || r->access!=1 || r->overlapped || (r->flags&~3u) || r->fd<0 || handles[i].object)return -1;
    vex_rendezvous_req_t q={.size=sizeof(q),.operation=VEX_RENDEZVOUS_ATTENUATE,.resource_fd=r->fd,.rights=5};int fd=(int)(int64_t)vex_syscall1(VEX_SYS_RENDEZVOUS,(uintptr_t)&q);if(fd<0)return -1;
    event *e=wr_native_alloc(sizeof(*e));if(!e){event_close_fd(fd);return -1;}memset(e,0,sizeof(*e));e->refs=1;e->remote=fd;e->next=objects;objects=e;
    handles[i].object=e;handles[i].generation=wr_handle_generation(r->handle);handles[i].flags=r->flags;event_close_fd(r->fd);return 0;
}
void *wr_object_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(CreateEventW);E(SetEvent);E(ResetEvent);E(WaitForSingleObject);E(WaitForMultipleObjects);E(CloseHandle);
    E(CreateMutexW);E(ReleaseMutex);
    E(CreateSemaphoreW);E(ReleaseSemaphore);E(CreateEventA);E(CreateMutexA);E(CreateSemaphoreA);
#undef E
    return 0;
}
