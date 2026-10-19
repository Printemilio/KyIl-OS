#include "windows.h"
#include "process.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include <unos/vex_file_range_abi.h>
static struct {int fd;uint32_t generation,access,live,refs,overlapped,pending,signaled,handle_flags;
    void *port;uintptr_t key;} files[256];
static wr_mutex gate;
unsigned wr_file_handle_count(void){wr_lock(&gate);unsigned count=0;for(unsigned i=0;i<256;i++)if(files[i].live)count++;wr_unlock(&gate);return count;}
typedef struct io_job {struct io_job *next,*all_next;unsigned slot,count,writing,cancelled,started,registered,completed;
    uint32_t issuer;void *buffer,*event,*packet;wr_overlapped *overlap;uint64_t offset;} io_job;
static io_job *jobs,*queue_first,*queue_last;
static uint32_t queue_sequence,worker_alive,worker_tid,stopping;
static void *worker_stack;
static void io_worker(void *);
static int borrow(void *,unsigned);
static int release(unsigned);
static void *stdio_handles[3];
unsigned wr_file_error(int error) {
    switch(error) {case 2:return 2;case 9:return 6;case 12:case 24:return 8;case 11:return 33;
    case 13:case 1:case 21:return 5;case 14:return 998;case 17:return 80;
    case 22:return 87;case 28:return 112;case 30:return 19;
    case 16:case 95:return 50;case 36:return 206;case 39:return 145;default:return 1117;}
}
static int slot(void *h) {
    uintptr_t v=(uintptr_t)h;unsigned i=(unsigned)(wr_handle_slot(v));
    return (wr_handle_tag(v))==0x57 && files[i].live &&
        wr_handle_generation(v)==files[i].generation?(int)i:-1;
}
static void *install(int fd,unsigned access) {
    unsigned i=0;while(i<256 && (files[i].refs || files[i].generation>=WR_HANDLE_GENERATION_MAX))i++;
    if(i==256)return 0;
    if(!++files[i].generation)++files[i].generation;
    files[i].fd=fd;files[i].access=access;files[i].live=1;files[i].refs=1;
    files[i].overlapped=files[i].pending=0;files[i].signaled=1;files[i].port=0;files[i].key=0;
    files[i].handle_flags=0;
    return (void *)(wr_handle_pack(0x57,files[i].generation,i));
}
int wr_file_valid(void *h) {wr_lock(&gate);int valid=slot(h)>=0;wr_unlock(&gate);return valid;}
int wr_files_init(void) {
    if(wr_boot) {
        for(unsigned i=0;i<wr_boot->file_count;i++)if((wr_handle_tag(wr_boot->files[i].handle))==0x54?
            wr_process_adopt(&wr_boot->files[i]):(wr_handle_tag(wr_boot->files[i].handle))==0x55?wr_pipe_adopt(&wr_boot->files[i]):wr_handle_tag(wr_boot->files[i].handle)==0x59?wr_object_adopt(&wr_boot->files[i]):wr_file_adopt(&wr_boot->files[i]))return -1;
        wr_files_boot_stdio(wr_boot->startup.input,wr_boot->startup.output,wr_boot->startup.error);return 0;
    }
    for(unsigned i=0;i<3;i++) {
        int64_t fd=(int64_t)vex_syscall1(VEX_SYS_DUP,i);if(fd<0)return -1;
        stdio_handles[i]=install((int)fd,i==0?1:2);if(!stdio_handles[i])return -1;
        /* The native launcher explicitly supplied these three streams. Expose
           them as inherited Windows handles, not freshly opened private files.
           Later handle lists still select exactly which streams reach a child. */
        files[wr_handle_slot((uintptr_t)stdio_handles[i])].handle_flags=1;
    }
    return 0;
}
void *wr_file_stdio(unsigned i) {return i<3?stdio_handles[i]:0;}
int wr_file_console_kind(void *h,unsigned *access) {
    int i=borrow(h,0);if(i<0)return 0;
    int yes=(int)vex_syscall1(VEX_SYS_ISATTY,(uintptr_t)files[i].fd);
    if(yes==1)*access=files[i].access;else wr_current_teb()->last_error=6;
    release((unsigned)i);return yes==1;
}
int wr_file_console_byte(void *h,uint8_t *out) {
    int i=borrow(h,1);if(i<0)return 0;
    if(vex_syscall1(VEX_SYS_ISATTY,(uintptr_t)files[i].fd)!=1){release((unsigned)i);wr_current_teb()->last_error=6;return 0;}
    int yes=0;
    for(;;){vex_fd_io_req_t r={files[i].fd,out,1};int64_t n=(int64_t)vex_syscall1(VEX_SYS_READFD,(uintptr_t)&r);
        if(n==1){yes=1;break;}if(n==0)break;
        int e=(int)vex_syscall0(VEX_SYS_LAST_ERROR);if(e!=11){wr_current_teb()->last_error=wr_file_error(e);break;}
        struct pollfd fd={files[i].fd,POLLIN,0};vex_poll_req_t wait={&fd,1,-1};
        if((int64_t)vex_syscall1(VEX_SYS_POLL,(uintptr_t)&wait)<0){wr_current_teb()->last_error=1117;break;}
    }release((unsigned)i);return yes;
}
void wr_files_boot_stdio(void *i,void *o,void *e){stdio_handles[0]=i;stdio_handles[1]=o;stdio_handles[2]=e;}
int wr_file_adopt(const wr_inherited_file *r) {
    uintptr_t value=r->handle;unsigned i=wr_handle_slot(value);
    if(wr_handle_tag(value)!=0x57 || !r->access || r->access>3 || (r->flags&~3u) || files[i].refs || r->fd<0)return -1;
    files[i].fd=r->fd;files[i].generation=wr_handle_generation(value);files[i].access=r->access;
    files[i].live=files[i].refs=files[i].signaled=1;files[i].handle_flags=r->flags;files[i].overlapped=r->overlapped;return 0;
}
int wr_file_export(void *h,wr_inherited_file *out,int child_slot) {
    wr_lock(&gate);int i=slot(h);int fd=-1;
    if(i>=0 && !files[i].port) {
        fd=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,files[i].fd);
        if(fd>=0)*out=(wr_inherited_file){(uintptr_t)h,child_slot,files[i].access,files[i].handle_flags,files[i].overlapped};
    }
    wr_unlock(&gate);return fd;
}
int wr_files_inherit(wr_inherited_file *out,unsigned *count,int first_slot) {
    *count=0;wr_lock(&gate);
    for(unsigned i=0;i<256;i++)if(files[i].live && (files[i].handle_flags&1)) {
        if(*count>=WR_BOOT_MAX_FILES-6 || files[i].port){wr_unlock(&gate);return -1;}
        void *h=(void *)(wr_handle_pack(0x57,files[i].generation,i));
        int fd=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,files[i].fd);if(fd<0){wr_unlock(&gate);return -1;}
        out[*count]=(wr_inherited_file){(uintptr_t)h,fd,files[i].access,files[i].handle_flags,files[i].overlapped};
        (*count)++;
    }
    wr_unlock(&gate);(void)first_slot;return 0;
}
int wr_file_flags(void *h,unsigned mask,unsigned flags,unsigned *old) {
    if(wr_handle_tag((uintptr_t)h)!=0x57)return -1;
    wr_lock(&gate);int i=slot(h);
    if(i>=0){if(old)*old=files[i].handle_flags;files[i].handle_flags=(files[i].handle_flags&~mask)|(flags&mask);}
    wr_unlock(&gate);if(i<0)wr_current_teb()->last_error=6;return i>=0;
}
static int borrow(void *h,unsigned access) {
    wr_lock(&gate);int i=slot(h);
    if(i<0)wr_current_teb()->last_error=6;
    else if((access&~files[i].access) || files[i].refs==UINT32_MAX) {wr_current_teb()->last_error=5;i=-1;}
    else files[i].refs++;
    wr_unlock(&gate);return i;
}
static int release(unsigned i) {
    wr_lock(&gate);int fd=--files[i].refs?-1:files[i].fd;void *port=fd<0?0:files[i].port;
    if(fd>=0)files[i].port=0;wr_unlock(&gate);
    if(fd<0)return 0;int r=(int)vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);wr_completion_release(port);return r;
}
unsigned wr_file_associate(void *h,void *port,uintptr_t key) {
    wr_lock(&gate);int i=slot(h);unsigned error=0;
    if(i<0)error=6;else if(files[i].port)error=87;
    else if(!files[i].overlapped || files[i].pending)error=50;
    else {files[i].port=port;files[i].key=key;}
    wr_unlock(&gate);return error;
}
int wr_file_close(void *h) {
    if((wr_handle_tag((uintptr_t)h))!=0x57)return -1;
    wr_lock(&gate);int i=slot(h),ok=0;
    if(i>=0 && !(files[i].handle_flags&2)) {
        files[i].live=0;ok=1;
        for(io_job *j=jobs;j;j=j->all_next)if(j->slot==(unsigned)i)__atomic_store_n(&j->cancelled,1,__ATOMIC_RELEASE);
    }else wr_current_teb()->last_error=i<0?6:5;
    wr_unlock(&gate);
    if(ok && release((unsigned)i)) {wr_current_teb()->last_error=wr_file_error((int)vex_syscall0(VEX_SYS_LAST_ERROR));return 0;}
    return ok;
}
static void *WR_WIN GetStdHandle(unsigned which) {
    unsigned i=which==(unsigned)-10?0:(which==(unsigned)-11?1:(which==(unsigned)-12?2:3));
    if(i==3){wr_current_teb()->last_error=87;return (void *)-1;}return __atomic_load_n(&stdio_handles[i],__ATOMIC_ACQUIRE);
}
static int WR_WIN SetStdHandle(unsigned which,void *handle) {
    unsigned i=which==(unsigned)-10?0:(which==(unsigned)-11?1:(which==(unsigned)-12?2:3));
    if(i==3){wr_current_teb()->last_error=87;return 0;}
    /* Windows stores the caller's handle value; this does not create/duplicate
     * any native authority. Subsequent I/O still validates the typed handle. */
    __atomic_store_n(&stdio_handles[i],handle,__ATOMIC_RELEASE);return 1;
}
static void *WR_WIN CreateFileW(const uint16_t *path,unsigned access,unsigned share,void *security,unsigned disposition,unsigned attributes,void *template) {
    if(wr_pipe_name(path))return wr_pipe_open(path,access,share,security,disposition,attributes,template);
    static unsigned traces;if(__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)<32){char line[1100]="windows-open: ";
        if(!wr_utf8(path,line+14,sizeof(line)-16)){size_t n=wr_len(line);line[n++]='\n';line[n]=0;wr_log(line);}
        wr_trace_contract("CreateFileW: access share disposition-attrs site",access,share,((uintptr_t)disposition<<32)|attributes,(uintptr_t)__builtin_return_address(0));}
    /* KSW's deliberate share=READ|WRITE projection: an existing readonly file
       is opened with native namespace protection, orchestrated by Ultra-Space.
       This is not Windows' full symmetric sharing/DELETE-pending contract.
       Deny-read/deny-write/exclusive modes remain explicitly unsupported. */
    if((share!=7 && share!=3) || (share==3 && (access!=0x80000000u || disposition!=3)) ||
       security || template || (attributes&~0x40000080u) || disposition==2 || disposition==4) {
        wr_current_teb()->last_error=50;return (void *)-1;
    }
    if(!access || (access&~0xc0000000u) || (disposition!=1 && disposition!=3 && disposition!=5)) {
        wr_current_teb()->last_error=87;return (void *)-1;
    }
    unsigned rights=((access&0x80000000u)?1:0)|((access&0x40000000u)?2:0);
    if(disposition==5 && !(rights&2)){wr_current_teb()->last_error=5;return (void *)-1;}
    unsigned flags=rights==3?O_RDWR:(rights==2?O_WRONLY:O_RDONLY);
    flags|=O_CLOEXEC|VEX_FS_OPEN_REGULAR;
    if(share==3)flags|=VEX_FS_OPEN_PROTECTED;
    if(disposition==1)flags|=O_CREAT|0x80u;
    if(disposition==5)flags|=O_TRUNC;
    int fd=wr_namespace_call(VEX_FS_ROOT_OPEN,path,flags,0600,0,(rights&2) || disposition!=3);
    if(share==3)wr_trace_contract("KSW protected open: native-result access share site",(uintptr_t)(intptr_t)fd,access,share,(uintptr_t)__builtin_return_address(0));
    wr_lock(&gate);
    void *h=fd<0?0:install(fd,rights);
    if(h)files[slot(h)].overlapped=(attributes&0x40000000u)!=0;
    if(fd<0)wr_current_teb()->last_error=wr_file_error(-fd);
    else if(!h)wr_current_teb()->last_error=8;
    wr_unlock(&gate);if(fd>=0 && !h)vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);return h?h:(void *)-1;
}
void *wr_file_duplicate(void *h,int inherit) {
    wr_inherited_file copy;int fd=wr_file_export(h,&copy,0);if(fd<0){wr_current_teb()->last_error=6;return 0;}
    wr_lock(&gate);void *out=install(fd,copy.access);
    if(out){int i=slot(out);files[i].overlapped=copy.overlapped;files[i].handle_flags=inherit?1:0;}
    wr_unlock(&gate);if(!out){vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);wr_current_teb()->last_error=8;}return out;
}
static void *WR_WIN CreateFileA(const char *path,unsigned access,unsigned share,void *security,unsigned disposition,unsigned attributes,void *template) {
    uint16_t wide[1024];
    if(!path || wr_utf16(path,wide,1024)<0){wr_current_teb()->last_error=1113;return (void *)-1;}
    return CreateFileW(wide,access,share,security,disposition,attributes,template);
}
static int WR_WIN CreatePipe(void **read,void **write,void *security,unsigned hint) {
    (void)hint;unsigned inherit=0;
    typedef struct {unsigned size,pad;void *descriptor;int inherit,pad2;} attributes;
    if(!read || !write){wr_current_teb()->last_error=87;return 0;}
    *read=*write=0;
    if(security){attributes *a=security;if(a->size!=sizeof(*a) || a->descriptor){wr_current_teb()->last_error=50;return 0;}inherit=a->inherit!=0;}
    int pair[2];vex_pipe_req_t r={pair};
    if(vex_syscall1(VEX_SYS_PIPE,(uintptr_t)&r)){wr_current_teb()->last_error=8;return 0;}
    wr_lock(&gate);*read=install(pair[0],1);*write=install(pair[1],2);
    if(*read)files[slot(*read)].handle_flags=inherit;if(*write)files[slot(*write)].handle_flags=inherit;
    wr_unlock(&gate);
    if(!*read || !*write){if(*read)wr_file_close(*read);else vex_syscall1(VEX_SYS_CLOSE,pair[0]);if(*write)wr_file_close(*write);else vex_syscall1(VEX_SYS_CLOSE,pair[1]);*read=*write=0;wr_current_teb()->last_error=8;return 0;}return 1;
}
static int start_worker(void) {
    if(worker_stack)return 0;
    worker_stack=wr_map(0,65536,PE_R|PE_W);if(!worker_stack)return -1;
    worker_alive=1;
    if(wr_start_thread(io_worker,0,(char *)worker_stack+65536,&worker_tid,&worker_alive)<0) {
        wr_unmap(worker_stack,65536);worker_stack=0;worker_alive=0;return -1;
    }return 0;
}
static unsigned status_error(uintptr_t s) {
    switch((uint32_t)s){case 0:return 0;case 0x103:return 996;case 0xc0000120:return 995;
    case 0xc0000011:return 38;case 0xc0000005:return 998;case 0xc0000022:return 5;
    case 0xc0000054:return 33;case 0xc0000008:return 6;case 0xc0000017:return 8;case 0xc000000d:return 87;default:return 1117;}
}
static uintptr_t error_status(unsigned e) {
    switch(e){case 0:return 0;case 995:return 0xc0000120;case 38:return 0xc0000011;
    case 998:return 0xc0000005;case 5:return 0xc0000022;case 6:return 0xc0000008;
    case 33:return 0xc0000054;case 8:return 0xc0000017;case 87:return 0xc000000d;default:return 0xc0000185;}
}
static void discard_job(io_job *j) {
    if(j->registered) {
        wr_lock(&gate);io_job **link=&jobs;while(*link && *link!=j)link=&(*link)->all_next;
        if(*link){*link=j->all_next;files[j->slot].pending--;}j->registered=0;wr_unlock(&gate);
    }
    if(j->packet)wr_native_free(j->packet);wr_event_io_release(j->event);release(j->slot);wr_native_free(j);
}
static int submit(unsigned i,void *buffer,unsigned count,wr_overlapped *overlap,int writing) {
    if((uintptr_t)overlap&7){wr_current_teb()->last_error=87;return 0;}
    uint64_t offset=((uint64_t)overlap->offset_high<<32)|overlap->offset;
    if(offset>INT64_MAX-count){wr_current_teb()->last_error=87;return 0;}
    io_job *j=wr_native_alloc(sizeof(*j));if(!j){wr_current_teb()->last_error=8;return 0;}
    *j=(io_job){.slot=i,.buffer=buffer,.count=count,.overlap=overlap,.offset=offset,.writing=writing!=0,.issuer=wr_tid()};
    if(overlap->event) {
        j->event=wr_event_io_acquire((void *)((uintptr_t)overlap->event&~(uintptr_t)1));
        if(!j->event){wr_native_free(j);wr_current_teb()->last_error=6;return 0;}
    }
    wr_lock(&gate);unsigned error=0;
    if(stopping || !files[i].live)error=995;
    for(io_job *old=jobs;old;old=old->all_next)if(!old->completed && old->overlap==overlap)error=87;
    if(!error && start_worker())error=8;
    /* A job owns its own file reference; closing the public handle cannot
       reuse its slot or destroy the native FD/port association underneath it. */
    if(!error) {
        files[i].refs++;files[i].pending++;j->registered=1;j->all_next=jobs;jobs=j;
    }
    void *port=files[i].port;uintptr_t key=files[i].key;wr_unlock(&gate);
    if(error){wr_event_io_release(j->event);wr_native_free(j);wr_current_teb()->last_error=error;return 0;}
    if(port && !((uintptr_t)overlap->event&1)) {
        j->packet=wr_completion_prepare(key,overlap);
        if(!j->packet){discard_job(j);wr_current_teb()->last_error=8;return 0;}
    }
    if(j->event)wr_event_io_reset(j->event);
    wr_lock(&gate);
    /* Recheck after allocations: another thread may close or concurrently
       submit the same OVERLAPPED while this caller is preparing its job. */
    if(stopping || !files[i].live)error=995;
    for(io_job *old=jobs;old;old=old->all_next)if(old!=j && !old->completed && old->overlap==overlap)error=87;
    if(!error) {
        overlap->bytes=0;__atomic_store_n(&overlap->internal,0x103,__ATOMIC_RELEASE);
        files[i].signaled=0;
        if(queue_last)queue_last->next=j;else queue_first=j;queue_last=j;
        __atomic_fetch_add(&queue_sequence,1,__ATOMIC_RELEASE);wr_futex(&queue_sequence,1,1,0);
    }
    wr_unlock(&gate);
    if(error){discard_job(j);wr_current_teb()->last_error=error;return 0;}
    wr_current_teb()->last_error=997;return 0;
}
static void io_worker(void *unused) {
    (void)unused;vex_thread_name_req_t name={0,VEX_THREAD_NAME_SET,"win-io-worker",14};
    vex_syscall1(VEX_SYS_THREAD_NAME,(uintptr_t)&name);
    for(;;) {
        wr_lock(&gate);io_job *j=queue_first;uint32_t sequence=queue_sequence;
        if(j){queue_first=j->next;if(!queue_first)queue_last=0;j->started=1;}
        unsigned stop=stopping;wr_unlock(&gate);
        if(!j){if(stop)break;wr_futex(&queue_sequence,0,sequence,0);continue;}
        unsigned error=0,bytes=0;
        if(__atomic_load_n(&j->cancelled,__ATOMIC_ACQUIRE))error=995;
        else {
            vex_rw_offset_req_t r={files[j->slot].fd,j->buffer,j->count,(off_t)j->offset};
            int64_t n=(int64_t)vex_syscall1(j->writing?VEX_SYS_PWRITE:VEX_SYS_PREAD,(uintptr_t)&r);
            if(n<0)error=wr_file_error((int)vex_syscall0(VEX_SYS_LAST_ERROR));
            else {bytes=(unsigned)n;if(!j->writing && j->count && !n)error=38;}
        }
        wr_lock(&gate);void *port=files[j->slot].port;
        /* Completion is distinct from retirement. Cancel/reuse must stop
           seeing this request BEFORE status/event/IOCP can expose completion,
           while the job retains its file/event/packet leases until cleanup. */
        j->completed=1;
        j->overlap->bytes=bytes;__atomic_store_n(&j->overlap->internal,error_status(error),__ATOMIC_RELEASE);
        unsigned active=0;
        for(io_job *other=jobs;other;other=other->all_next)if(other->slot==j->slot && !other->completed){active=1;break;}
        __atomic_store_n(&files[j->slot].signaled,!active,__ATOMIC_RELEASE);
        wr_unlock(&gate);
        wr_futex((uint32_t *)&j->overlap->internal,1,0x7fffffff,0);
        wr_event_io_finish(&j->event);
        if(j->packet)wr_completion_publish(port,&j->packet,bytes,error);
        wr_lock(&gate);
        wr_futex(&files[j->slot].signaled,1,0x7fffffff,0);
        io_job **link=&jobs;while(*link!=j)link=&(*link)->all_next;*link=j->all_next;
        files[j->slot].pending--;j->registered=0;wr_unlock(&gate);
        release(j->slot);wr_native_free(j);
    }
    vex_syscall1(VEX_SYS_THREAD_EXIT,0);wr_exit(125);
}
static int WR_WIN CancelIoEx(void *h,wr_overlapped *overlap) {
    wr_lock(&gate);int i=slot(h);unsigned found=0;
    if(i>=0)for(io_job *j=jobs;j;j=j->all_next)if(!j->completed && j->slot==(unsigned)i && (!overlap || j->overlap==overlap)) {
        __atomic_store_n(&j->cancelled,1,__ATOMIC_RELEASE);found++;
    }
    wr_unlock(&gate);if(!found)wr_current_teb()->last_error=i<0?6:1168;return found!=0;
}
static int WR_WIN GetOverlappedResult(void *h,wr_overlapped *overlap,unsigned *bytes,int wait) {
    if(!overlap || !bytes || ((uintptr_t)overlap&7)){wr_current_teb()->last_error=87;return 0;}
    int i=borrow(h,0);if(i<0)return 0;uintptr_t status=__atomic_load_n(&overlap->internal,__ATOMIC_ACQUIRE);
    while(status==0x103 && wait) {
        if(overlap->event) {
            if(wr_wait_one((void *)((uintptr_t)overlap->event&~(uintptr_t)1),UINT32_MAX)==UINT32_MAX){release((unsigned)i);return 0;}
        }else wr_futex((uint32_t *)&overlap->internal,0,0x103,0);
        status=__atomic_load_n(&overlap->internal,__ATOMIC_ACQUIRE);
    }
    unsigned error=status_error(status);*bytes=(unsigned)overlap->bytes;release((unsigned)i);
    if(error)wr_current_teb()->last_error=error;return !error;
}
int wr_file_wait(void *h,unsigned timeout,unsigned *result) {
    if((wr_handle_tag((uintptr_t)h))!=0x57)return 0;
    int i=borrow(h,0);if(i<0){*result=UINT32_MAX;return 1;}
    uint64_t start=wr_ticks();*result=0;
    while(!__atomic_load_n(&files[i].signaled,__ATOMIC_ACQUIRE)) {
        uint64_t elapsed=wr_ticks()-start;
        if(timeout!=UINT32_MAX && elapsed>=timeout){*result=258;break;}
        wr_futex(&files[i].signaled,0,0,timeout==UINT32_MAX?0:((uint64_t)timeout-elapsed)*1000000);
    }
    release((unsigned)i);return 1;
}
void wr_files_quiesce(int begin) {if(begin)wr_lock(&gate);else wr_unlock(&gate);}
void wr_files_stop_worker_locked(void) {
    /* Object/completion/file gates are held, and peer stacks are still live. */
    stopping=1;
    if(worker_stack && __atomic_load_n(&worker_alive,__ATOMIC_ACQUIRE))wr_stop_thread(worker_tid,0);
    while(__atomic_load_n(&worker_alive,__ATOMIC_ACQUIRE))wr_futex(&worker_alive,0,1,0);
}
void wr_files_shutdown(void) {
    /* The worker was stopped before peer stack retirement. Never access an
       aborted OVERLAPPED/buffer here; their original stacks may be gone. */
    wr_lock(&gate);
    io_job *aborted=jobs;jobs=queue_first=queue_last=0;
    wr_unlock(&gate);
    while(aborted){io_job *next=aborted->all_next;aborted->registered=0;files[aborted->slot].pending--;discard_job(aborted);aborted=next;}
    if(worker_stack){wr_unmap(worker_stack,65536);worker_stack=0;}
}
static int io(void *h,void *bytes,unsigned count,unsigned *written,void *overlapped,int writing) {
    if((wr_handle_tag((uintptr_t)h))==0x55)return wr_pipe_io(h,bytes,count,written,overlapped,writing);
    if(written)*written=0;
    int i=borrow(h,writing?2:1),ok=0;
    if(i>=0) {
        if(overlapped || files[i].overlapped) {
            if(!overlapped){wr_current_teb()->last_error=87;release((unsigned)i);return 0;}
            if(!files[i].overlapped){wr_current_teb()->last_error=50;release((unsigned)i);return 0;}
            ok=submit((unsigned)i,bytes,count,overlapped,writing);release((unsigned)i);return ok;
        }
        vex_fd_io_req_t r={files[i].fd,bytes,count};
        int64_t n=(int64_t)vex_syscall1(writing?VEX_SYS_WRITEFD:VEX_SYS_READFD,(uintptr_t)&r);
        if(n<0)wr_current_teb()->last_error=wr_file_error((int)vex_syscall0(VEX_SYS_LAST_ERROR));
        else {if(written)*written=(unsigned)n;ok=1;}
    }
    if(i>=0)release((unsigned)i);return ok;
}
static int WR_WIN WriteFile(void *h,const void *b,unsigned n,unsigned *out,void *overlap) {return io(h,(void *)b,n,out,overlap,1);}
static int WR_WIN ReadFile(void *h,void *b,unsigned n,unsigned *out,void *overlap) {return io(h,b,n,out,overlap,0);}
static int file_range(void *handle,unsigned flags,unsigned reserved,unsigned low,unsigned high,wr_overlapped *overlap,int unlock) {
    if(reserved || (flags&~3u) || !overlap || ((uintptr_t)overlap&7)){wr_current_teb()->last_error=87;return 0;}
    wr_overlapped request;
    if(wr_memory_copy(&request,overlap,sizeof(request))){wr_current_teb()->last_error=998;return 0;}
    int i=borrow(handle,0);if(i<0)return 0;
    /* Pending asynchronous locks require cancellation independently of file
       I/O. Reject that option rather than block the caller or fake completion.
       Synchronous waits and immediate locks have the actual native contract. */
    if(files[i].overlapped && !unlock && !(flags&1)){release((unsigned)i);wr_current_teb()->last_error=50;return 0;}
    void *event=0;
    if(request.event){event=wr_event_io_acquire((void *)((uintptr_t)request.event&~(uintptr_t)1));
        if(!event){release((unsigned)i);wr_current_teb()->last_error=6;return 0;}}
    vex_file_range_req_t r={sizeof(r),unlock?0:((flags&2)?2:1),files[i].fd,
        VEX_FILE_RANGE_DESCRIPTION|(!unlock && !(flags&1)?VEX_FILE_RANGE_WAIT:0),
        ((uint64_t)request.offset_high<<32)|request.offset,((uint64_t)high<<32)|low};
    int64_t result=(int64_t)vex_syscall1(VEX_SYS_FILE_RANGE,(uintptr_t)&r);
    unsigned error=result<0?(result==-2?158:result==-4?995:wr_file_error((int)-result)):0;
    if(!error) {
        request.internal=0;request.bytes=0;
        if(wr_memory_copy(overlap,&request,sizeof(request))) {
            if(!unlock){r.mode=0;r.flags=VEX_FILE_RANGE_DESCRIPTION;vex_syscall1(VEX_SYS_FILE_RANGE,(uintptr_t)&r);}
            error=998;
        }
        if(!error && event)wr_event_io_finish(&event);
    }
    wr_event_io_release(event);release((unsigned)i);
    if(error)wr_current_teb()->last_error=error;return !error;
}
static int WR_WIN LockFileEx(void *h,unsigned f,unsigned reserved,unsigned lo,unsigned hi,wr_overlapped *o){return file_range(h,f,reserved,lo,hi,o,0);}
static int WR_WIN UnlockFileEx(void *h,unsigned reserved,unsigned lo,unsigned hi,wr_overlapped *o){return file_range(h,0,reserved,lo,hi,o,1);}
static int WR_WIN FlushFileBuffers(void *h) {
    int i=borrow(h,0),ok=0;
    if(i>=0) {int r=(int)vex_syscall1(VEX_SYS_FSYNC,(uintptr_t)files[i].fd);ok=!r;
        if(r)wr_current_teb()->last_error=wr_file_error((int)vex_syscall0(VEX_SYS_LAST_ERROR));}
    if(i>=0)release((unsigned)i);return ok;
}
static int WR_WIN GetFileSizeEx(void *h,int64_t *size) {
    if(!size){wr_current_teb()->last_error=87;return 0;}
    int i=borrow(h,0),ok=0;vex_stat_ex_t st;
    if(i>=0) {vex_stat_ex_req_t r={VEX_STAT_EX_FD,files[i].fd,0,&st};
        int result=(int)vex_syscall1(VEX_SYS_STAT_EX,(uintptr_t)&r);
        if(!result){*size=st.size;ok=1;}else wr_current_teb()->last_error=wr_file_error(-result);}
    if(i>=0)release((unsigned)i);return ok;
}
static int WR_WIN SetFilePointerEx(void *h,int64_t distance,int64_t *position,unsigned origin) {
    if(origin>2){wr_current_teb()->last_error=87;return 0;}
    int i=borrow(h,0);if(i<0)return 0;
    int64_t n=(int64_t)vex_syscall3(VEX_SYS_LSEEK,(uintptr_t)files[i].fd,(uint64_t)distance,origin);
    int ok=n>=0;
    if(!ok)wr_current_teb()->last_error=wr_file_error((int)vex_syscall0(VEX_SYS_LAST_ERROR));
    else if(position)*position=n;
    release((unsigned)i);return ok;
}
static unsigned WR_WIN GetFileSize(void *h,unsigned *high) {
    int64_t size;if(!GetFileSizeEx(h,&size))return UINT32_MAX;
    if(high)*high=(unsigned)((uint64_t)size>>32);
    wr_current_teb()->last_error=0;return (unsigned)size;
}
static uint64_t timestamp(int64_t sec,unsigned ns,unsigned valid) {
    if(!valid || sec < -11644473600ll || ns>=1000000000u ||
       sec>(int64_t)(UINT64_MAX/10000000ull)-11644473601ll)return 0;
    return (uint64_t)(sec+11644473600ll)*10000000ull+ns/100;
}
static int WR_WIN GetFileTime(void *h,void *creation,void *access,void *write) {
    int i=borrow(h,1),ok=0;vex_stat_ex_t st;
    if(i>=0) {
        vex_stat_ex_req_t r={VEX_STAT_EX_FD,files[i].fd,0,&st};
        int result=(int)vex_syscall1(VEX_SYS_STAT_EX,(uintptr_t)&r);
        if(result)wr_current_teb()->last_error=wr_file_error(-result);
        else {
            uint64_t absent=0,at=timestamp(st.atime_sec,st.atime_nsec,st.valid&VEX_STAT_EX_HAS_ATIME);
            uint64_t mt=timestamp(st.mtime_sec,st.mtime_nsec,st.valid&VEX_STAT_EX_HAS_MTIME);
            /* STAT_EX has no birth time. Never substitute inode-change time. */
            if(creation)memcpy(creation,&absent,8);
            if(access)memcpy(access,&at,8);if(write)memcpy(write,&mt,8);ok=1;
        }
        release((unsigned)i);
    }
    return ok;
}
static unsigned WR_WIN GetFileType(void *h) {
    if(wr_handle_tag((uintptr_t)h)==0x55)return wr_pipe_valid(h)?3:0;
    int i=borrow(h,0);if(i<0)return 0;
    vex_stat_ex_t st;vex_stat_ex_req_t r={VEX_STAT_EX_FD,files[i].fd,0,&st};
    int result=(int)(int64_t)vex_syscall1(VEX_SYS_STAT_EX,(uintptr_t)&r);release((unsigned)i);
    if(result){wr_current_teb()->last_error=wr_file_error(-result);return 0;}
    switch(st.mode&0170000){case 0100000:case 0040000:case 0060000:return 1;case 0020000:return 2;case 0010000:case 0140000:return 3;default:wr_current_teb()->last_error=0;return 0;}
}
typedef struct {uint32_t attributes,times[6],volume,size_high,size_low,links,index_high,index_low;} handle_file_info;
_Static_assert(sizeof(handle_file_info)==52,"BY_HANDLE_FILE_INFORMATION");
static int WR_WIN GetFileInformationByHandle(void *h,handle_file_info *out) {
    if(!out){wr_current_teb()->last_error=87;return 0;}
    int i=borrow(h,0);if(i<0)return 0;vex_stat_ex_t st;vex_stat_ex_req_t r={VEX_STAT_EX_OBJECT,files[i].fd,0,&st};
    int result=(int)(int64_t)vex_syscall1(VEX_SYS_STAT_EX,(uintptr_t)&r);release((unsigned)i);
    if(result){wr_current_teb()->last_error=wr_file_error(-result);return 0;}
    if((st.mode&0170000)!=0100000 && (st.mode&0170000)!=0040000){wr_current_teb()->last_error=50;return 0;}
    handle_file_info value={.volume=(unsigned)st.dev,.links=st.nlink,.index_high=(unsigned)(st.ino>>32),.index_low=(unsigned)st.ino};
    value.attributes=(st.mode&0170000)==0040000?0x10:0;if(!(st.mode&0222))value.attributes|=1;if(!value.attributes)value.attributes=0x80;
    uint64_t at=timestamp(st.atime_sec,st.atime_nsec,st.valid&VEX_STAT_EX_HAS_ATIME),mt=timestamp(st.mtime_sec,st.mtime_nsec,st.valid&VEX_STAT_EX_HAS_MTIME);
    memcpy(value.times+2,&at,8);memcpy(value.times+4,&mt,8);value.size_low=(unsigned)st.size;value.size_high=(unsigned)((uint64_t)st.size>>32);
    if(wr_memory_copy(out,&value,sizeof(value))){wr_current_teb()->last_error=998;return 0;}return 1;
}
static int WR_WIN SetEndOfFile(void *h) {
    int i=borrow(h,2);if(i<0)return 0;
    int64_t position=(int64_t)vex_syscall3(VEX_SYS_LSEEK,files[i].fd,0,1);
    int result=position<0?-1:(int)vex_syscall2(VEX_SYS_FTRUNCATE,files[i].fd,(uintptr_t)position);
    if(result)wr_current_teb()->last_error=wr_file_error((int)vex_syscall0(VEX_SYS_LAST_ERROR));
    release((unsigned)i);return !result;
}
static int WR_WIN GetFileInformationByHandleEx(void *h,unsigned kind,void *out,unsigned capacity){
    unsigned length=kind==0?40:kind==1?24:kind==9?8:0;
    if(!length){wr_current_teb()->last_error=50;return 0;}
    if(capacity<length){wr_current_teb()->last_error=24;return 0;}
    int i=borrow(h,0);if(i<0)return 0;vex_stat_ex_t st;vex_stat_ex_req_t q={VEX_STAT_EX_FD,files[i].fd,0,&st};
    int result=(int)(int64_t)vex_syscall1(VEX_SYS_STAT_EX,(uintptr_t)&q);release((unsigned)i);
    if(result){wr_current_teb()->last_error=wr_file_error(-result);return 0;}
    if((st.mode&0170000)!=0100000 && (st.mode&0170000)!=0040000){wr_current_teb()->last_error=50;return 0;}
    unsigned attrs=(st.mode&0170000)==0040000?0x10:0;if(!(st.mode&0222))attrs|=1;if(!attrs)attrs=0x80;
    union {struct {uint64_t creation,access,write,change;uint32_t attrs,pad;} basic;
        struct {int64_t allocated,size;uint32_t links;uint8_t pending,directory,pad[2];} standard;
        struct {unsigned attrs,tag;} tag;} value={0};
    if(kind==0){value.basic.access=timestamp(st.atime_sec,st.atime_nsec,st.valid&VEX_STAT_EX_HAS_ATIME);
        value.basic.write=timestamp(st.mtime_sec,st.mtime_nsec,st.valid&VEX_STAT_EX_HAS_MTIME);
        value.basic.change=timestamp(st.ctime_sec,st.ctime_nsec,st.valid&VEX_STAT_EX_HAS_CTIME);value.basic.attrs=attrs;}
    else if(kind==1){if(st.blocks<0 || st.blocks>INT64_MAX/512){wr_current_teb()->last_error=50;return 0;}
        value.standard.allocated=st.blocks*512;value.standard.size=st.size;value.standard.links=st.nlink;
        /* Native remove rejects outstanding file references; no delete-pending
           file can be fabricated here. Regular files have no reparse tag. */
        value.standard.directory=(st.mode&0170000)==0040000;}
    else value.tag.attrs=attrs;
    if(wr_memory_copy(out,&value,length)){wr_current_teb()->last_error=998;return 0;}return 1;
}
static int WR_WIN SetFileInformationByHandle(void *h,unsigned kind,const void *input,unsigned length){
    if(kind!=6){wr_current_teb()->last_error=50;return 0;}
    int64_t end;if(length!=8){wr_current_teb()->last_error=24;return 0;}
    if(wr_memory_copy(&end,input,8)){wr_current_teb()->last_error=998;return 0;}
    if(end<0){wr_current_teb()->last_error=87;return 0;}
    int i=borrow(h,2);if(i<0)return 0;
    int result=(int)vex_syscall2(VEX_SYS_FTRUNCATE,files[i].fd,(uintptr_t)end);
    if(result)wr_current_teb()->last_error=wr_file_error((int)vex_syscall0(VEX_SYS_LAST_ERROR));
    release((unsigned)i);return !result;
}
void *wr_file_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(GetStdHandle);E(CreateFileW);E(ReadFile);E(WriteFile);E(FlushFileBuffers);E(GetFileSizeEx);E(SetFilePointerEx);
    E(CancelIoEx);E(GetOverlappedResult);E(LockFileEx);E(UnlockFileEx);
    E(CreatePipe);
    E(CreateFileA);E(GetFileSize);E(GetFileTime);E(GetFileType);E(GetFileInformationByHandle);E(SetEndOfFile);E(SetStdHandle);
    E(GetFileInformationByHandleEx);E(SetFileInformationByHandle);
#undef E
    return 0;
}
