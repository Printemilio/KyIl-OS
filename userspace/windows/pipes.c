#include "windows.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "process.h"
#include "pipe_protocol.h"
int wr_security_snapshot(const void *,void **,unsigned *);
/* Windows names, instances, negotiation and byte/message reading live here.
 * Ultra-Space owns discovery, security and protected session capabilities.
 * The data channel uses native attenuated message endpoints. OVERLAPPED,
 * remote pipes and writes beyond one native payload remain explicit errors. */
typedef struct {uint32_t version,access,type,max_instances,timeout,reserved[3];} pipe_traits;
typedef struct {uint32_t magic,version,access,reserved;} connect_message;
typedef struct {uint32_t magic,length,reserved;} data_header;
typedef struct pipe {unsigned refs,server,access,mode,type,closed,handle_flags;int fd,listener,lease,cancel;
    wr_mutex reader,writer,connector;uint8_t *pending;unsigned length,position;} wr_pipe;
static struct {wr_pipe *p;uint32_t generation;} handles[256];static wr_mutex gate;
unsigned wr_pipe_handle_count(void){wr_lock(&gate);unsigned count=0;for(unsigned i=0;i<256;i++)if(handles[i].p)count++;wr_unlock(&gate);return count;}
static unsigned lower(unsigned c){return c>='A' && c<='Z'?c+32:c;}
int wr_pipe_name(const uint16_t *s) {
    if(!s)return 0;const char *prefix="\\\\.\\pipe\\";
    for(unsigned i=0;prefix[i];i++)if(!s[i] || lower(s[i])!=(unsigned char)prefix[i])return 0;return 1;
}
static int label(const uint16_t *s,char *out,unsigned *length) {
    if(!wr_pipe_name(s))return 123;unsigned n=0;
    for(unsigned i=9;s[i];i++) {
        if(i>=256 || s[i]=='\\' || s[i]=='/' || s[i]<32)return 123;
        if(s[i]>126)return 50;out[n++]=(char)lower(s[i]);
    }
    if(!n)return 123;*length=n;return 0;
}
static int rendezvous(vex_rendezvous_req_t *r) {r->size=sizeof(*r);return (int)(int64_t)vex_syscall1(VEX_SYS_RENDEZVOUS,(uintptr_t)r);}
static int resource(unsigned op,int fd,unsigned rights) {
    vex_rendezvous_req_t r={.operation=op,.resource_fd=fd,.rights=rights};return rendezvous(&r);
}
static void close_fd(int fd){if(fd>=0)vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);}
static unsigned error(int n) {
    switch(-n){case 2:return 2;case 9:return 6;case 11:return 231;case 12:case 24:return 8;
    case 13:case 17:return 5;case 22:return 87;case 110:return 121;case 32:case 125:return 109;case 107:return 233;default:return 1117;}
}
static wr_pipe *get(void *h) {
    uintptr_t v=(uintptr_t)h;unsigned i=(v>>2)&255;
    if(wr_handle_tag(v)!=0x55 || (v&3) || wr_handle_generation(v)!=handles[i].generation)return 0;
    return handles[i].p;
}
static wr_pipe *borrow(void *h) {wr_lock(&gate);wr_pipe *p=get(h);if(p && p->refs==UINT32_MAX)p=0;if(p)p->refs++;wr_unlock(&gate);if(!p)wr_current_teb()->last_error=6;return p;}
int wr_pipe_valid(void *h){wr_lock(&gate);int valid=get(h)!=0;wr_unlock(&gate);if(!valid)wr_current_teb()->last_error=6;return valid;}
static void release(wr_pipe *p) {
    wr_lock(&gate);unsigned dead=!--p->refs;wr_unlock(&gate);if(!dead)return;
    close_fd(p->fd);close_fd(p->listener);close_fd(p->lease);if(p->pending)wr_native_free(p->pending);wr_native_free(p);
}
static void *install(wr_pipe *p) {
    wr_lock(&gate);unsigned i=0;while(i<256 && (handles[i].p || handles[i].generation>=WR_HANDLE_GENERATION_MAX))i++;
    if(i==256){wr_unlock(&gate);release(p);return 0;}
    if(!++handles[i].generation)++handles[i].generation;handles[i].p=p;
    void *h=(void *)(wr_handle_pack(0x55,handles[i].generation,i));wr_unlock(&gate);return h;
}
void wr_pipes_quiesce(int begin){if(begin)wr_lock(&gate);else wr_unlock(&gate);}
int wr_pipe_close(void *h) {
    if(wr_handle_tag((uintptr_t)h)!=0x55)return -1;
    wr_lock(&gate);wr_pipe *p=get(h);int lease=-1;
    if(p && (p->handle_flags&2)){wr_unlock(&gate);wr_current_teb()->last_error=5;return 0;}
    if(p){handles[((uintptr_t)h>>2)&255].p=0;p->refs++;__atomic_store_n(&p->closed,1,__ATOMIC_RELEASE);lease=p->lease;p->lease=-1;if(p->cancel>=0)resource(VEX_RENDEZVOUS_CANCEL_ENDPOINT,p->cancel,0);}
    wr_unlock(&gate);if(!p){wr_current_teb()->last_error=6;return 0;}
    close_fd(lease);if(p->fd>=0)resource(VEX_RENDEZVOUS_CANCEL_ENDPOINT,p->fd,0);
    /* The service session is shared by inherited handles: ordinary close
       must not cancel another process's retained capability. */
    release(p);release(p);return 1;
}
static int ultra_open(void){
    vex_fs_root_req_t open={.size=sizeof(open),.operation=VEX_FS_ROOT_OPEN,.fd=wr_system_root(),.path="pipe-service",
        .flags=O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE};
    int image=(int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&open);if(image<0)return image;
    vex_process_resource_req_t q={.size=sizeof(q),.operation=VEX_PROCESS_ULTRA_OPEN,.fd=image,.status=wr_prefix_root()};
    int fd=(int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)&q);close_fd(image);return fd;
}
static unsigned rpc(int channel,wp_packet *q,const void *sd,int *received,wr_pipe *owner){
    if(received)*received=-1;if(q->sd_size>65536-sizeof(*q))return 50;
    int fds[2];vex_message_pair_req_t pair={fds,0,0};if(vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair))return 8;
    size_t size=sizeof(*q)+q->sd_size;uint8_t *frame=wr_native_alloc(size);if(!frame){close_fd(fds[0]);close_fd(fds[1]);return 8;}
    q->magic=WP_MAGIC;q->error=0;memcpy(frame,q,sizeof(*q));if(q->sd_size)memcpy(frame+sizeof(*q),sd,q->sd_size);
    vex_message_attachment_req_t a={(unsigned)fds[1],VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    vex_message_io_req_t m={channel,0,frame,size,&a,1};int n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&m);
    wr_native_free(frame);close_fd(fds[1]);if(n!=(int)size){close_fd(fds[0]);return n<0?error(n):109;}
    if(owner){wr_lock(&gate);if(owner->closed){wr_unlock(&gate);close_fd(fds[0]);return 109;}owner->cancel=fds[0];wr_unlock(&gate);}
    a=(vex_message_attachment_req_t){0,VEX_MESSAGE_OBJECT_FD,0};m=(vex_message_io_req_t){fds[0],0,q,sizeof(*q),&a,1};
    n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&m);
    if(owner){wr_lock(&gate);owner->cancel=-1;wr_unlock(&gate);}close_fd(fds[0]);
    if(n!=(int)sizeof(*q))return n<0?error(n):109;
    if(q->magic!=WP_MAGIC){if(m.attachment_count)close_fd((int)a.handle);return 13;}
    if(m.attachment_count){if(received)*received=(int)a.handle;else close_fd((int)a.handle);}return q->error;
}
static wr_pipe *new_pipe(void){wr_pipe *p=wr_native_alloc(sizeof(*p));if(!p)return 0;memset(p,0,sizeof(*p));p->refs=1;p->fd=p->listener=p->lease=p->cancel=-1;return p;}
static void *WR_WIN CreateNamedPipeW(const uint16_t *name,unsigned access,unsigned mode,unsigned instances,unsigned out_hint,unsigned in_hint,unsigned timeout,void *security){
    (void)out_hint;(void)in_hint;unsigned why=0;wp_packet q={.op=WP_CREATE,.access=access,.mode=mode,.limit=instances,.timeout=timeout};unsigned size;
    if((why=label(name,q.name,&size))){wr_current_teb()->last_error=why;return (void *)-1;}q.name[size]=0;
    typedef struct {unsigned size,pad;void *descriptor;int inherit,pad2;} attributes;
    attributes a={0};void *sd=0;unsigned length=0;
    if(security){if(wr_memory_copy(&a,security,sizeof(a)) || a.size!=sizeof(a)){wr_current_teb()->last_error=87;return (void *)-1;}
        if(a.descriptor && !wr_security_snapshot(a.descriptor,&sd,&length))return (void *)-1;}
    q.sd_size=length;wr_pipe *p=new_pipe();if(!p)why=8;
    if(!why){p->lease=ultra_open();if(p->lease<0)why=error(p->lease);}
    if(!why)why=rpc(p->lease,&q,sd,&p->listener,0);if(sd)wr_native_free(sd);
    if(why){if(p)release(p);wr_current_teb()->last_error=why;return (void *)-1;}
    p->server=1;p->access=access&3;p->mode=mode&3;p->type=mode&4;p->handle_flags=a.inherit?1:0;
    void *h=install(p);if(!h){wr_current_teb()->last_error=8;return (void *)-1;}
    static unsigned traced;if(__atomic_fetch_add(&traced,1,__ATOMIC_RELAXED)<16){char message[]="windows-pipe: CREATE backend=Ultra-Space protected=0 inherit=0 success\n";for(unsigned i=0;message[i];i++){if(message[i]=='=' && message[i+1]=='0'){message[i+1]=a.descriptor?'1':'0';break;}}for(unsigned i=0;message[i];i++)if(message[i]=='i' && message[i+1]=='n' && message[i+2]=='h'){message[i+8]=a.inherit?'1':'0';break;}vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)message);}return h;
}
void *wr_pipe_open(const uint16_t *name,unsigned access,unsigned share,void *security,unsigned disposition,unsigned flags,void *template){
    /* Identification SQOS is metadata, not an impersonation grant. Other
       impersonation/security modes require a represented token contract. */
    if(security || template || (flags&~(0x80u|0x00110000u)) || ((flags&0x10000u) && !(flags&0x100000u))){wr_current_teb()->last_error=50;return (void *)-1;}
    if(!access || (access&~0xc0000000u) || (share&~7u) || disposition!=3){wr_current_teb()->last_error=87;return (void *)-1;}
    wp_packet q={.op=WP_OPEN,.access=access};unsigned size,why=label(name,q.name,&size);if(why){wr_current_teb()->last_error=why;return (void *)-1;}q.name[size]=0;
    wr_pipe *p=new_pipe();if(!p){wr_current_teb()->last_error=8;return (void *)-1;}
    p->lease=ultra_open();why=p->lease<0?error(p->lease):rpc(p->lease,&q,0,&p->fd,0);
    if(why){release(p);wr_current_teb()->last_error=why;return (void *)-1;}
    p->access=q.access;p->type=q.mode&4;p->mode=0;void *h=install(p);if(!h){wr_current_teb()->last_error=8;return (void *)-1;}return h;
}
static int WR_WIN ConnectNamedPipe(void *h,void *overlapped){
    if(overlapped){wr_current_teb()->last_error=50;return 0;}wr_pipe *p=borrow(h);if(!p)return 0;
    if(!p->server){release(p);wr_current_teb()->last_error=87;return 0;}
    wr_lock(&p->connector);unsigned why=p->fd>=0?535:0;int data=-1;wp_packet q={.op=WP_CONNECT,.mode=__atomic_load_n(&p->mode,__ATOMIC_ACQUIRE)};
    if(!why)why=rpc(p->listener,&q,0,&data,p);
    if(data>=0){wr_lock(&gate);if(p->closed)close_fd(data);else p->fd=data;wr_unlock(&gate);}
    if(p->closed)why=109;wr_unlock(&p->connector);release(p);if(why)wr_current_teb()->last_error=why;return !why;
}
static int WR_WIN DisconnectNamedPipe(void *h){
    wr_pipe *p=borrow(h);if(!p)return 0;if(!p->server){release(p);wr_current_teb()->last_error=87;return 0;}
    wr_lock(&gate);unsigned why=p->refs>2 || p->closed?50:0;
    if(!why){close_fd(p->fd);p->fd=-1;if(p->pending){wr_native_free(p->pending);p->pending=0;}p->position=p->length=0;}
    wr_unlock(&gate);wp_packet q={.op=WP_DISCONNECT};if(!why)why=rpc(p->listener,&q,0,0,0);release(p);if(why)wr_current_teb()->last_error=why;return !why;
}
static int pipe_io_locked(wr_pipe *p,void *buffer,unsigned count,unsigned *out,int writing) {
    if(out)*out=0;unsigned why=0,bytes=0;
    wr_lock(&gate);int connected_fd=p->fd;wr_unlock(&gate);
    if(!(p->access&(writing?2:1)))why=5;else if(connected_fd<0)why=233;
    if(!why && writing) {
        if(count>65524)why=50;
        else {
            uint8_t *frame=wr_native_alloc((size_t)count+12);if(!frame)why=8;
            else {data_header head={0x57444154,count,0};memcpy(frame,&head,12);if(count)memcpy(frame+12,buffer,count);
                vex_message_io_req_t send={connected_fd,(__atomic_load_n(&p->mode,__ATOMIC_ACQUIRE)&1)?MSG_DONTWAIT:0,frame,(size_t)count+12,0,0};
                int n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send);wr_native_free(frame);
                if(n<0)why=n==-11?232:error(n);else bytes=count;}
        }
    }else if(!why && count) {
        if(!p->pending) {
            uint8_t *frame=wr_native_alloc(65536);if(!frame)why=8;
            else {vex_message_io_req_t recv={connected_fd,(__atomic_load_n(&p->mode,__ATOMIC_ACQUIRE)&1)?MSG_DONTWAIT:0,frame,65536,0,0};int n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&recv);
                data_header head={0};if(n>=12)memcpy(&head,frame,12);
                if(n<=0)why=n==-11?232:(n<0?error(n):109);
                else if(n<12 || head.magic!=0x57444154 || head.reserved || head.length!=(unsigned)n-12)why=13;
                else {p->pending=frame;p->position=0;p->length=head.length;}
                if(why)wr_native_free(frame);}
        }
        if(!why) {
            bytes=p->length-p->position;if(bytes>count)bytes=count;
            if(bytes)memcpy(buffer,p->pending+12+p->position,bytes);p->position+=bytes;
            if(p->position==p->length){wr_native_free(p->pending);p->pending=0;p->position=p->length=0;}
            else if(__atomic_load_n(&p->mode,__ATOMIC_ACQUIRE)&2)why=234;
        }
    }
    if(out)*out=bytes;if(why)wr_current_teb()->last_error=why;return !why;
}
int wr_pipe_io(void *h,void *buffer,unsigned count,unsigned *out,void *overlapped,int writing) {
    if(out)*out=0;if(overlapped){wr_current_teb()->last_error=50;return 0;}
    wr_pipe *p=borrow(h);if(!p)return 0;wr_mutex *mutex=writing?&p->writer:&p->reader;
    wr_lock(mutex);int result=pipe_io_locked(p,buffer,count,out,writing);wr_unlock(mutex);release(p);return result;
}
/* A transaction owns both directions until its first reply read completes.
 * Ordinary reads/writes use the same gates; the kernel endpoint stays leased. */
static int WR_WIN TransactNamedPipe(void *h,void *in,unsigned in_size,void *out,unsigned out_size,unsigned *read,void *overlapped) {
    if(read)*read=0;if(overlapped){wr_current_teb()->last_error=50;return 0;}
    wr_pipe *p=borrow(h);if(!p)return 0;
    if(p->access!=3 || !p->type || !(p->mode&2)){release(p);wr_current_teb()->last_error=87;return 0;}
    wr_lock(&p->reader);wr_lock(&p->writer);
    unsigned written;int result=pipe_io_locked(p,in,in_size,&written,1);
    if(result)result=pipe_io_locked(p,out,out_size,read,0);
    wr_unlock(&p->writer);wr_unlock(&p->reader);release(p);return result;
}
static int WR_WIN PeekNamedPipe(void *h,void *buffer,unsigned capacity,unsigned *read,unsigned *available,unsigned *remaining) {
    if(read)*read=0;if(available)*available=0;if(remaining)*remaining=0;
    wr_pipe *p=borrow(h);if(!p)return 0;unsigned why=0;
    if(!(p->access&1))why=5;else if(p->fd<0)why=233;
    if(why){release(p);wr_current_teb()->last_error=why;return 0;}
    wr_lock(&p->reader);
    unsigned pending=p->length-p->position;
    size_t prefix_size=p->pending?0:(capacity>65524?65536:(size_t)capacity+12);
    uint8_t *prefix=prefix_size?wr_native_alloc(prefix_size):0;
    if(prefix_size && !prefix)why=8;
    vex_message_observe_req_t r={.fd=p->fd,.prefix=prefix,.capacity=prefix_size};
    if(!why){int e=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_OBSERVE,(uintptr_t)&r);if(e)why=error(e);}
    unsigned first=pending,copied=0;const void *source=p->pending?p->pending+12+p->position:0;
    if(!why && !p->pending && r.messages) {
        data_header header={0};if(r.copied>=12)memcpy(&header,prefix,12);
        if(r.first_length<12 || header.magic!=0x57444154 || header.reserved || header.length!=r.first_length-12)why=13;
        else {first=header.length;source=prefix+12;}
    }
    if(!why && r.bytes<r.messages*12)why=13;
    if(!why) {
        copied=first<capacity?first:capacity;
        if(copied && !buffer)why=87;
        else if(copied && wr_memory_copy(buffer,source,copied))why=998;
        else {if(read)*read=copied;if(available)*available=(unsigned)(pending+r.bytes-r.messages*12);
            if(remaining)*remaining=p->type?first-copied:0;}
    }
    if(prefix)wr_native_free(prefix);wr_unlock(&p->reader);release(p);
    if(why)wr_current_teb()->last_error=why;return !why;
}
static int WR_WIN SetNamedPipeHandleState(void *h,const unsigned *mode,const unsigned *collection,const unsigned *timeout) {
    if(collection || timeout){wr_current_teb()->last_error=50;return 0;}
    wr_pipe *p=borrow(h);if(!p)return 0;unsigned why=0;
    if(mode){unsigned value;if(wr_memory_copy(&value,mode,sizeof(value)))why=998;
        else if((value&~3u) || ((value&2) && !p->type))why=87;else __atomic_store_n(&p->mode,value,__ATOMIC_RELEASE);}
    release(p);if(why)wr_current_teb()->last_error=why;return !why;
}
static int WR_WIN WaitNamedPipeW(const uint16_t *name,unsigned timeout){wp_packet q={.op=WP_WAIT,.timeout=timeout};unsigned size,why=label(name,q.name,&size);
    if(!why){q.name[size]=0;int fd=ultra_open();why=fd<0?error(fd):rpc(fd,&q,0,0,0);close_fd(fd);}if(why)wr_current_teb()->last_error=why;return !why;
}
int wr_pipe_flags(void *h,unsigned mask,unsigned flags,unsigned *out){if(wr_handle_tag((uintptr_t)h)!=0x55)return -1;
    wr_lock(&gate);wr_pipe *p=get(h);if(p){if(out)*out=p->handle_flags;p->handle_flags=(p->handle_flags&~mask)|(flags&mask);}wr_unlock(&gate);if(!p)wr_current_teb()->last_error=6;return p!=0;
}
int wr_pipe_export(void *h,wr_inherited_file *out,unsigned *count){
    wr_pipe *p=borrow(h);if(!p)return -1;wr_lock(&gate);unsigned why=0;
    /* The handle and this borrow must be the only references while taking
       an inheritance snapshot. Active I/O owns another borrow and may be
       changing the partial-message state under its reader/writer lock. */
    if(p->refs>2 || p->closed || p->pending || p->cancel>=0)why=50;
    if(why){wr_unlock(&gate);release(p);wr_current_teb()->last_error=why;return -1;}
    int fds[3]={p->lease,p->listener,p->fd};unsigned metadata=p->access|(p->server<<2)|(p->type<<3)|(p->mode<<6);
    for(unsigned i=0;!why && i<3;i++)if(fds[i]>=0){if(*count>=WR_BOOT_MAX_FILES-8){why=8;break;}
        int fd=i==2?resource(VEX_RENDEZVOUS_ATTENUATE,fds[i],((p->access&1)?2:0)|((p->access&2)?1:0)|4):(int)(int64_t)vex_syscall1(VEX_SYS_DUP,fds[i]);
        if(fd<0){why=8;break;}out[(*count)++]=(wr_inherited_file){(uintptr_t)h,fd,metadata,p->handle_flags,i};}
    wr_unlock(&gate);release(p);if(why)wr_current_teb()->last_error=why;return why?-1:0;
}
int wr_pipes_inherit(wr_inherited_file *out,unsigned *count){
    void *list[256];unsigned n=0;wr_lock(&gate);for(unsigned i=0;i<256;i++)if(handles[i].p && (handles[i].p->handle_flags&1))list[n++]=(void *)(wr_handle_pack(0x55,handles[i].generation,i));wr_unlock(&gate);
    for(unsigned i=0;i<n;i++)if(wr_pipe_export(list[i],out,count))return -1;return 0;
}
int wr_pipe_adopt(const wr_inherited_file *r){uintptr_t v=r->handle;unsigned index=(v>>2)&255;
    if(wr_handle_tag(v)!=0x55 || (v&3) || r->fd<0 || r->overlapped>2 || (r->access&~255u) || !(r->access&3) || (r->flags&~3u))return -1;
    wr_pipe *p=handles[index].p;if(!p){if(r->overlapped)return -1;p=new_pipe();if(!p)return -1;
        p->access=r->access&3;p->server=(r->access>>2)&1;p->type=(r->access>>3)&4;p->mode=(r->access>>6)&3;p->handle_flags=r->flags;
        handles[index].p=p;handles[index].generation=wr_handle_generation(v);}
    int *field=r->overlapped==0?&p->lease:r->overlapped==1?&p->listener:&p->fd;if(*field>=0)return -1;
    *field=r->fd;return 0;
}
void *wr_pipe_duplicate(void *h,int inherit){wr_pipe *p=borrow(h);if(!p)return 0;wr_pipe *copy=new_pipe();
    wr_lock(&gate);if(!copy || p->refs>2 || p->pending || p->closed || p->cancel>=0){wr_unlock(&gate);release(p);if(copy)release(copy);wr_current_teb()->last_error=50;return 0;}
    copy->server=p->server;copy->access=p->access;copy->mode=p->mode;copy->type=p->type;copy->handle_flags=inherit?1:0;
    copy->lease=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,p->lease);
    if(p->listener>=0)copy->listener=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,p->listener);
    if(p->fd>=0)copy->fd=resource(VEX_RENDEZVOUS_ATTENUATE,p->fd,((p->access&1)?2:0)|((p->access&2)?1:0)|4);
    unsigned failed=copy->lease<0 || (p->listener>=0 && copy->listener<0) || (p->fd>=0 && copy->fd<0);wr_unlock(&gate);release(p);
    if(failed){release(copy);wr_current_teb()->last_error=8;return 0;}return install(copy);
}
void *wr_pipe_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(PeekNamedPipe);E(TransactNamedPipe);E(CreateNamedPipeW);E(ConnectNamedPipe);E(DisconnectNamedPipe);E(SetNamedPipeHandleState);E(WaitNamedPipeW);
#undef E
    return 0;
}
