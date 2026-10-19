/* Windows socket handles and layouts stay userspace. Native network/socket
 * resources retain all capability checks; no private network stack. */
#include "process.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
typedef uintptr_t ws_socket;
typedef struct {uint16_t version,high,max_sockets,max_udp;void *vendor;char description[257],status[129];} ws_data;
_Static_assert(sizeof(ws_data)==408 && offsetof(ws_data,description)==16,"AMD64 WSADATA");
typedef struct ws_addrinfo {int flags,family,type,protocol;size_t length;char *canonical;struct sockaddr *address;struct ws_addrinfo *next;} ws_addrinfo;
_Static_assert(sizeof(ws_addrinfo)==48,"AMD64 ADDRINFOA");
static wr_mutex gate;
static unsigned started;
static struct {int fd;uint32_t tid;unsigned live;} operations[256];
static struct {int fd;unsigned live,generation,type;} handles[256];
typedef struct address_record {struct address_record *next;ws_addrinfo info;struct sockaddr_in address;char canonical[256];} address_record;
static address_record *addresses;
static int error(unsigned e){wr_current_teb()->last_error=e;return -1;}
static unsigned native_error(int e){switch(e){case 1:case 13:return 10013;case 4:return 10004;case 9:case 88:return 10038;
 case 11:return 10035;case 12:case 24:case 105:return 10055;case 14:return 10014;case 22:return 10022;case 90:return 10040;
 case 95:return 10045;case 97:return 10047;case 98:return 10048;case 99:return 10049;case 101:return 10051;
 case 104:return 10054;case 106:return 10056;case 107:return 10057;case 110:return 10060;case 111:return 10061;case 114:return 10037;case 115:return 10035;default:return 10022;}}
static int raw_error(void){int e=(int)vex_syscall0(VEX_SYS_LAST_ERROR);return error(native_error(e?e:22));}
static void close_fd(int fd){if(fd>=0)vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);}
static int index(ws_socket h){unsigned i=wr_handle_slot(h);return wr_handle_tag(h)==0x67 && !(h&3) && handles[i].live && wr_handle_generation(h)==handles[i].generation?(int)i:-1;}
static int acquire_type(ws_socket h,unsigned *type){wr_lock(&gate);int i=index(h),fd=-1;
    if(!started)error(10093);else if(i<0)error(10038);else {unsigned slot=0;while(slot<256 && operations[slot].live)slot++;
        if(slot==256)error(10055);else {fd=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,handles[i].fd);
            if(fd<0)raw_error();else {operations[slot].fd=fd;operations[slot].tid=wr_tid();operations[slot].live=1;if(type)*type=handles[i].type;}}}
    wr_unlock(&gate);return fd;
}
static int acquire(ws_socket h){return acquire_type(h,0);}
static void release_operation(int fd){wr_lock(&gate);
    for(unsigned i=0;i<256;i++)if(operations[i].live && operations[i].fd==fd){operations[i].live=0;close_fd(fd);break;}
    wr_unlock(&gate);
}
void wr_winsock_quiesce(int begin){if(begin)wr_lock(&gate);else wr_unlock(&gate);}
void wr_winsock_thread_exit(uint32_t tid){
    wr_lock(&gate);
    for(unsigned i=0;i<256;i++)if(operations[i].live && operations[i].tid==tid){operations[i].live=0;close_fd(operations[i].fd);}
    wr_unlock(&gate);
}
static ws_socket install(int fd,unsigned type){wr_lock(&gate);unsigned i=0;
    while(i<256 && (handles[i].live || handles[i].generation==WR_HANDLE_GENERATION_MAX))i++;
    ws_socket h=UINTPTR_MAX;if(!started)error(10093);else if(i==256)error(10055);
    else {handles[i].fd=fd;handles[i].live=1;handles[i].generation++;handles[i].type=type;h=wr_handle_pack(0x67,handles[i].generation,i);}
    wr_unlock(&gate);if(h==UINTPTR_MAX)close_fd(fd);return h;
}
static int ready(void){wr_lock(&gate);int ok=started!=0;wr_unlock(&gate);if(!ok)error(10093);return ok;}
unsigned wr_winsock_handle_count(void){wr_lock(&gate);unsigned n=0;for(unsigned i=0;i<256;i++)n+=handles[i].live!=0;wr_unlock(&gate);return n;}
static int WR_WIN WSAGetLastError(void){return (int)wr_current_teb()->last_error;}
static void WR_WIN WSASetLastError(int e){wr_current_teb()->last_error=(unsigned)e;}
static int WR_WIN WSAStartup(unsigned short version,ws_data *out){
    /* The supported personality is Winsock 2.2; old 1.x layouts/contracts are
       not silently negotiated. Initialization doesn't grant network access. */
    if(version!=0x202)return 10092;ws_data data={.version=0x202,.high=0x202};
    memcpy(data.description,"KyIlOS native socket provider",29);memcpy(data.status,"Ready",6);
    if(wr_memory_copy(out,&data,sizeof(data)))return 10014;
    wr_lock(&gate);if(started==UINT32_MAX){wr_unlock(&gate);return 10055;}started++;wr_unlock(&gate);return 0;
}
static int WR_WIN WSACleanup(void){wr_lock(&gate);if(!started){wr_unlock(&gate);return error(10093);}
    /* Native synchronous operations are retained, not fake-cancelled. A final
       cleanup with a live operation is explicitly refused until completion. */
    if(started==1)for(unsigned i=0;i<256;i++)if(operations[i].live){wr_unlock(&gate);return error(10036);}
    if(!--started)for(unsigned i=0;i<256;i++)if(handles[i].live){int fd=handles[i].fd;handles[i].live=0;close_fd(fd);}
    wr_unlock(&gate);return 0;
}
static ws_socket WR_WIN ws_socket_create(int family,int type,int protocol){
    if(!ready())return UINTPTR_MAX;if(family!=2){error(10047);return UINTPTR_MAX;}
    if((type!=1 && type!=2) || (protocol && protocol!=(type==1?6:17))){error(10043);return UINTPTR_MAX;}
    int fd=(int)(int64_t)vex_syscall3(VEX_SYS_SOCKET,family,type|SOCK_CLOEXEC,protocol);
    if(fd<0){raw_error();return UINTPTR_MAX;}return install(fd,(unsigned)type);
}
static int WR_WIN ws_close(ws_socket h){wr_lock(&gate);int i=index(h),fd=-1;
    if(!started)error(10093);else if(i<0)error(10038);else {fd=handles[i].fd;handles[i].live=0;}
    wr_unlock(&gate);if(fd<0)return -1;int result=(int)(int64_t)vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);return result<0?raw_error():0;
}
static int address_op(ws_socket h,const struct sockaddr *address,int length,unsigned op){
    int fd=acquire(h);if(fd<0)return -1;struct sockaddr_in copy;
    if(length<16 || wr_memory_copy(&copy,address,16)){release_operation(fd);return error(10014);}
    if(copy.sin_family!=2){release_operation(fd);return error(10047);}
    vex_sockaddr_req_t r={fd,(void *)&copy,16};int result=(int)(int64_t)vex_syscall1(op,(uintptr_t)&r);
    if(result<0)raw_error();release_operation(fd);return result<0?-1:result;
}
static int WR_WIN ws_connect(ws_socket h,const struct sockaddr *a,int n){return address_op(h,a,n,VEX_SYS_CONNECT);}
static int WR_WIN ws_bind(ws_socket h,const struct sockaddr *a,int n){return address_op(h,a,n,VEX_SYS_BIND);}
static int WR_WIN ws_listen(ws_socket h,int backlog){int fd=acquire(h);if(fd<0)return -1;
    int r=(int)(int64_t)vex_syscall2(VEX_SYS_LISTEN,fd,backlog);if(r<0)raw_error();release_operation(fd);return r<0?-1:0;
}
static int name_op(ws_socket h,struct sockaddr *out,int *length,unsigned op){
    int fd=acquire(h);if(fd<0)return -1;int cap;struct sockaddr_in a;
    if(!length || wr_memory_copy(&cap,length,4) || cap<16){release_operation(fd);return error(10014);}
    vex_socket_op_req_t r={.fd=fd,.op=op,.addrlen=16,.addr=&a};int64_t result=(int64_t)vex_syscall1(VEX_SYS_SOCKET_OP,(uintptr_t)&r);
    release_operation(fd);if(result<0)return error(native_error((int)-result));int actual=(int)r.addrlen;
    if(actual!=16)return error(10045);
    if(wr_memory_copy(out,&a,16) || wr_memory_copy(length,&actual,4))return error(10014);return 0;
}
static int WR_WIN ws_getsockname(ws_socket h,struct sockaddr *a,int *n){return name_op(h,a,n,VEX_SOCKET_GETSOCKNAME);}
static int WR_WIN ws_getpeername(ws_socket h,struct sockaddr *a,int *n){return name_op(h,a,n,VEX_SOCKET_GETPEERNAME);}
static ws_socket WR_WIN ws_accept(ws_socket h,struct sockaddr *out,int *length){
    int fd=acquire(h);if(fd<0)return UINTPTR_MAX;int cap=0;struct sockaddr_in a;uint32_t n=16;
    if(out && (!length || wr_memory_copy(&cap,length,4) || cap<16)){release_operation(fd);error(10014);return UINTPTR_MAX;}
    vex_accept_req_t r={fd,out?(void *)&a:0,out?&n:0};int child=(int)(int64_t)vex_syscall1(VEX_SYS_ACCEPT,(uintptr_t)&r);
    if(child<0)raw_error();release_operation(fd);if(child<0)return UINTPTR_MAX;
    if(out && (wr_memory_copy(out,&a,16) || wr_memory_copy(length,&n,4))){close_fd(child);error(10014);return UINTPTR_MAX;}return install(child,1);
}
static int transfer(ws_socket h,void *data,int count,int flags,int writing){
    unsigned type;int fd=acquire_type(h,&type);if(fd<0)return -1;if(count<0 || (flags&~2)){release_operation(fd);return error(count<0?10022:10045);}
    if(!writing && type==2){vex_socket_op_req_t r={.fd=fd,.op=VEX_SOCKET_RECVFROM,.flags=flags|0x20,.buffer=data,.length=(unsigned)count};
        int64_t n=(int64_t)vex_syscall1(VEX_SYS_SOCKET_OP,(uintptr_t)&r);release_operation(fd);
        return n<0?error(native_error((int)-n)):(n>count?error(10040):(int)n);}
    vex_send_recv_req_t r={fd,data,(size_t)count,flags};int result=(int)(int64_t)vex_syscall1(writing?VEX_SYS_SEND:VEX_SYS_RECV,(uintptr_t)&r);
    if(result<0)raw_error();release_operation(fd);return result<0?-1:result;
}
static int WR_WIN ws_send(ws_socket h,const char *p,int n,int f){return transfer(h,(void *)p,n,f,1);}
static int WR_WIN ws_recv(ws_socket h,char *p,int n,int f){return transfer(h,p,n,f,0);}
static int WR_WIN ws_sendto(ws_socket h,const char *p,int n,int flags,const struct sockaddr *address,int length){
    unsigned type;int fd=acquire_type(h,&type);if(fd<0)return -1;struct sockaddr_in a;
    if(n<0 || flags){release_operation(fd);return error(n<0?10022:10045);}
    if(type==1){vex_send_recv_req_t r={fd,(void *)p,(unsigned)n,0};int result=(int)(int64_t)vex_syscall1(VEX_SYS_SEND,(uintptr_t)&r);
        if(result<0)raw_error();release_operation(fd);return result<0?-1:result;}
    if(address && (length<16 || wr_memory_copy(&a,address,16))){release_operation(fd);return error(10014);}
    if(address && a.sin_family!=2){release_operation(fd);return error(10047);}
    vex_socket_op_req_t r={.fd=fd,.op=VEX_SOCKET_SENDTO,.addrlen=address?16:0,.addr=address?&a:0,.buffer=(void *)p,.length=(unsigned)n};
    int64_t result=(int64_t)vex_syscall1(VEX_SYS_SOCKET_OP,(uintptr_t)&r);release_operation(fd);return result<0?error(native_error((int)-result)):(int)result;
}
static int WR_WIN ws_recvfrom(ws_socket h,char *p,int n,int flags,struct sockaddr *out,int *length){
    unsigned type;int fd=acquire_type(h,&type);if(fd<0)return -1;int cap=0;struct sockaddr_in a;
    if(n<0 || (flags&~2)){release_operation(fd);return error(n<0?10022:10045);}
    if(type==1){vex_send_recv_req_t r={fd,p,(unsigned)n,flags};int result=(int)(int64_t)vex_syscall1(VEX_SYS_RECV,(uintptr_t)&r);
        if(result<0)raw_error();release_operation(fd);return result<0?-1:result;}
    if(out && (!length || wr_memory_copy(&cap,length,4) || cap<16)){release_operation(fd);return error(10014);}
    vex_socket_op_req_t r={.fd=fd,.op=VEX_SOCKET_RECVFROM,.flags=flags|0x20,.addrlen=16,.addr=&a,.buffer=p,.length=(unsigned)n};
    int64_t result=(int64_t)vex_syscall1(VEX_SYS_SOCKET_OP,(uintptr_t)&r);release_operation(fd);if(result<0)return error(native_error((int)-result));
    int actual=(int)r.addrlen;if(out && (actual!=16 || wr_memory_copy(out,&a,16) || wr_memory_copy(length,&actual,4)))return error(10014);return result>n?error(10040):(int)result;
}
static int WR_WIN ws_ioctl(ws_socket h,int32_t command,unsigned *argument){
    int fd=acquire(h);if(fd<0)return -1;unsigned nonblock;
    if((uint32_t)command!=0x8004667eu){release_operation(fd);return error(10045);}
    if(wr_memory_copy(&nonblock,argument,4)){release_operation(fd);return error(10014);}
    int flags=(int)(int64_t)vex_syscall3(VEX_SYS_FCNTL,fd,F_GETFL,0),r=-1;
    if(flags>=0)r=(int)(int64_t)vex_syscall3(VEX_SYS_FCNTL,fd,F_SETFL,nonblock?flags|O_NONBLOCK:flags&~O_NONBLOCK);
    if(flags<0 || r<0)raw_error();release_operation(fd);return flags<0 || r<0?-1:0;
}
static int option(ws_socket h,int level,int name,void *value,int *length,int setting){
    int fd=acquire(h);if(fd<0)return -1;int n=0,native_name=name,native_level=level;
    if(wr_memory_copy(&n,length,4) || n<4){release_operation(fd);return error(10014);}
    int timeout=0;if(level==0xffff){native_level=SOL_SOCKET;
        if(name==0x1008 && !setting)native_name=SO_TYPE;
        else if(name==0x1007 && !setting)native_name=SO_ERROR;
        else if(name==0x1006){native_name=SO_RCVTIMEO;timeout=1;}
        else if(name==0x1005){native_name=SO_SNDTIMEO;timeout=1;}
        else {release_operation(fd);return error(10042);}
    }else if(level!=6 || name!=1){release_operation(fd);return error(10042);}
    unsigned scalar=0;struct timeval time={0};uint32_t bytes=timeout?sizeof(time):4;
    if(setting){if(wr_memory_copy(&scalar,value,4)){release_operation(fd);return error(10014);}if(timeout){time.tv_sec=scalar/1000;time.tv_usec=(scalar%1000)*1000;}}
    vex_sockopt_req_t r={fd,native_level,native_name,timeout?(void *)&time:(void *)&scalar,&bytes};
    int result=(int)(int64_t)vex_syscall1(setting?VEX_SYS_SETSOCKOPT:VEX_SYS_GETSOCKOPT,(uintptr_t)&r);
    if(result<0)raw_error();release_operation(fd);if(result<0)return -1;
    if(!setting){if(timeout){uint64_t millis=(uint64_t)time.tv_sec*1000+(uint64_t)time.tv_usec/1000;scalar=millis>UINT32_MAX?UINT32_MAX:(unsigned)millis;}
        else if(level==0xffff && name==0x1007 && scalar)scalar=native_error((int)scalar);
        n=4;if(wr_memory_copy(value,&scalar,4) || wr_memory_copy(length,&n,4))return error(10014);}
    return 0;
}
static int WR_WIN ws_getsockopt(ws_socket h,int l,int name,char *out,int *length){return option(h,l,name,out,length,0);}
static int WR_WIN ws_setsockopt(ws_socket h,int l,int name,const char *in,int length){return option(h,l,name,(void *)in,&length,1);}
typedef struct {unsigned count,pad;ws_socket sockets[64];} ws_fdset;
typedef struct {int32_t seconds,micros;} ws_timeval;
typedef struct {ws_fdset input[3],output[3];struct pollfd polls[192];} select_workspace;
static void release_workspace(select_workspace **p){if(*p)wr_native_free(*p);}
static int WR_WIN ws_select(int ignored,ws_fdset *read,ws_fdset *write,ws_fdset *except,const ws_timeval *time){
    (void)ignored;if(!ready())return -1;
    select_workspace *space __attribute__((cleanup(release_workspace)))=wr_native_alloc(sizeof(*space));if(!space)return error(10055);memset(space,0,sizeof(*space));
    ws_fdset *input=space->input,*output=space->output;struct pollfd *polls=space->polls;
    ws_fdset *sets[]={read,write,except};unsigned bases[3],count=0;int timeout=-1;
    if(time){ws_timeval t;if(wr_memory_copy(&t,time,sizeof(t)))return error(10014);
        if(t.seconds<0 || t.micros<0 || t.micros>=1000000 || t.seconds>(INT32_MAX-1000)/1000)return error(10022);
        timeout=t.seconds*1000+(t.micros+999)/1000;}
    for(unsigned k=0;k<3;k++){bases[k]=count;if(!sets[k])continue;
        if(wr_memory_copy(&input[k].count,&sets[k]->count,4) || input[k].count>64)return error(10014);
        if(wr_memory_copy(input[k].sockets,sets[k]->sockets,input[k].count*8))return error(10014);
        count+=input[k].count;
    }
    if(!count)return error(10022);unsigned acquired=0;int result=-1;
    for(unsigned k=0;k<3;k++)for(unsigned i=0;i<input[k].count;i++){
        int fd=acquire(input[k].sockets[i]);if(fd<0)goto done;
        polls[acquired++]=(struct pollfd){fd,k==0?POLLIN:(k==1?POLLOUT:POLLERR),0};
    }
    {vex_poll_req_t request={polls,count,timeout};result=(int)(int64_t)vex_syscall1(VEX_SYS_POLL,(uintptr_t)&request);}
    if(result<0){raw_error();goto done;}result=0;
    for(unsigned k=0;k<3;k++)for(unsigned i=0;i<input[k].count;i++){
        short flags=polls[bases[k]+i].revents;
        if(flags&POLLNVAL){result=error(10038);goto done;}
        if((k==0 && (flags&(POLLIN|POLLHUP))) || (k==1 && (flags&POLLOUT)) || (k==2 && (flags&POLLERR))){output[k].sockets[output[k].count++]=input[k].sockets[i];result++;}
    }
    for(unsigned k=0;k<3;k++)if(sets[k] && (wr_memory_copy(&sets[k]->count,&output[k].count,4) || wr_memory_copy(sets[k]->sockets,output[k].sockets,output[k].count*8))){result=error(10014);break;}
done:
    for(unsigned i=0;i<acquired;i++)release_operation(polls[i].fd);return result;
}
static unsigned WR_WIN ws_htonl(unsigned n){return __builtin_bswap32(n);}
static unsigned short WR_WIN ws_htons(unsigned short n){return __builtin_bswap16(n);}
static int WR_WIN ws_gethostname(char *out,int capacity){
    if(!ready())return -1;struct utsname facts;if(vex_syscall1(VEX_SYS_UNAME,(uintptr_t)&facts))return error(10022);
    size_t n=wr_len(facts.nodename)+1;if(capacity<0 || n>(unsigned)capacity || wr_memory_copy(out,facts.nodename,n))return error(10014);return 0;
}
static int ipv4(const char *s,uint32_t *out){unsigned parts[4]={0};for(unsigned i=0;i<4;i++){
    unsigned digits=0;int leading_zero=*s=='0';while(*s>='0' && *s<='9'){if(++digits>3)return 0;parts[i]=parts[i]*10+(unsigned)(*s++-'0');}if(!digits || parts[i]>255 || (leading_zero && digits>1))return 0;
    if(i<3){if(*s++!='.')return 0;}else if(*s)return 0;}*out=parts[0]|parts[1]<<8|parts[2]<<16|parts[3]<<24;return 1;
}
static int copy_text(const char *s,char *out,unsigned cap){if(!s)return 0;for(unsigned i=0;i<cap;i++){if(wr_memory_copy(out+i,s+i,1))return 0;if(!out[i])return 1;}return 0;}
static const char *WR_WIN ws_inet_ntop(int family,const void *source,char *out,size_t size){
    if(family!=2){error(10047);return 0;}unsigned char p[4];if(wr_memory_copy(p,source,4)){error(10014);return 0;}
    char text[16];unsigned n=0;for(unsigned i=0;i<4;i++){if(i)text[n++]='.';if(p[i]>=100)text[n++]=(char)('0'+p[i]/100);
        if(p[i]>=10)text[n++]=(char)('0'+p[i]/10%10);text[n++]=(char)('0'+p[i]%10);}text[n]=0;
    if(size<=n || wr_memory_copy(out,text,n+1)){error(10014);return 0;}return out;
}
static int WR_WIN ws_getaddrinfo(const char *node,const char *service,const ws_addrinfo *hints,ws_addrinfo **out){
    if(!ready())return 10093;ws_addrinfo *empty=0,h={0};if(wr_memory_copy(out,&empty,8) || (hints && wr_memory_copy(&h,hints,sizeof(h))))return 10014;
    if(h.flags&~7)return 10022;if(h.family && h.family!=2)return 10047;
    if(h.type && h.type!=1 && h.type!=2)return 10044;if(h.protocol && h.protocol!=6 && h.protocol!=17)return 10043;
    int type=h.type?h.type:(h.protocol==17?2:1);if(h.protocol && h.protocol!=(type==1?6:17))return 10043;
    unsigned results=!h.type && !h.protocol?2:1;
    char host[256],port[16];unsigned number=0;if(service){if(!copy_text(service,port,sizeof(port)))return 10014;
        if(!port[0])return 10109;for(unsigned i=0;port[i];i++){if(port[i]<'0' || port[i]>'9')return 10109;number=number*10+(unsigned)(port[i]-'0');if(number>65535)return 10109;}}
    if(!node && !service)return 11001;uint32_t address;
    if(!node){address=h.flags&1?0:0x0100007f;host[0]=0;}
    else {if(!copy_text(node,host,sizeof(host)))return 10014;if(!ipv4(host,&address)){
        int numeric=1;for(unsigned i=0;host[i];i++)if((host[i]<'0' || host[i]>'9') && host[i]!='.')numeric=0;
        if((h.flags&4) || numeric)return 11001;if(h.flags&2)return 10045; /* resolver does not return canonical CNAME data */
        vex_resolve_req_t r={host,&address,1,0};int64_t n=(int64_t)vex_syscall1(VEX_SYS_RESOLVE,(uintptr_t)&r);if(n<1)return n==-12?8:11001;}}
    address_record *r=wr_native_alloc(results*sizeof(*r));if(!r)return 8;memset(r,0,results*sizeof(*r));
    r->address=(struct sockaddr_in){.sin_family=2,.sin_port=__builtin_bswap16((uint16_t)number),.sin_addr={address}};
    if(node && (h.flags&2))memcpy(r->canonical,host,wr_len(host)+1);
    r->info=(ws_addrinfo){h.flags,2,type,type==1?6:17,16,node && (h.flags&2)?r->canonical:0,(void *)&r->address,0};
    if(results==2){r[1].address=r->address;r[1].info=(ws_addrinfo){h.flags,2,2,17,16,0,(void *)&r[1].address,0};r->info.next=&r[1].info;}
    wr_lock(&gate);r->next=addresses;addresses=r;wr_unlock(&gate);ws_addrinfo *result=&r->info;
    if(wr_memory_copy(out,&result,8)){wr_lock(&gate);address_record **p=&addresses;while(*p && *p!=r)p=&(*p)->next;if(*p)*p=r->next;wr_unlock(&gate);wr_native_free(r);return 10014;}return 0;
}
static void WR_WIN ws_freeaddrinfo(ws_addrinfo *info){if(!info)return;wr_lock(&gate);address_record **p=&addresses;
    while(*p && &(*p)->info!=info)p=&(*p)->next;address_record *r=*p;if(r)*p=r->next;wr_unlock(&gate);if(r)wr_native_free(r);else error(10022);
}
static void *WR_WIN WSACreateEvent(void){void *(WR_WIN *create)(void *,int,int,const uint16_t *)=wr_object_exports("CreateEventW");return create(0,1,0,0);}
static int WR_WIN WSACloseEvent(void *h){int (WR_WIN *close)(void *)=wr_object_exports("CloseHandle");return close(h);}
static int WR_WIN WSASetEvent(void *h){int (WR_WIN *set)(void *)=wr_object_exports("SetEvent");return set(h);}
static int WR_WIN WSAResetEvent(void *h){int (WR_WIN *reset)(void *)=wr_object_exports("ResetEvent");return reset(h);}
void *wr_winsock_exports(const char *name){
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(WSAStartup);E(WSACleanup);E(WSAGetLastError);E(WSASetLastError);E(WSACreateEvent);E(WSACloseEvent);E(WSASetEvent);E(WSAResetEvent);
#undef E
    if(wr_equal(name,"socket",0))return (void *)&ws_socket_create;
    if(wr_equal(name,"closesocket",0))return (void *)&ws_close;
    if(wr_equal(name,"connect",0))return (void *)&ws_connect;
    if(wr_equal(name,"bind",0))return (void *)&ws_bind;
    if(wr_equal(name,"listen",0))return (void *)&ws_listen;
    if(wr_equal(name,"accept",0))return (void *)&ws_accept;
    if(wr_equal(name,"getsockname",0))return (void *)&ws_getsockname;
    if(wr_equal(name,"getpeername",0))return (void *)&ws_getpeername;
    if(wr_equal(name,"send",0))return (void *)&ws_send;
    if(wr_equal(name,"recv",0))return (void *)&ws_recv;
    if(wr_equal(name,"sendto",0))return (void *)&ws_sendto;
    if(wr_equal(name,"recvfrom",0))return (void *)&ws_recvfrom;
    if(wr_equal(name,"ioctlsocket",0))return (void *)&ws_ioctl;
    if(wr_equal(name,"getsockopt",0))return (void *)&ws_getsockopt;
    if(wr_equal(name,"setsockopt",0))return (void *)&ws_setsockopt;
    if(wr_equal(name,"select",0))return (void *)&ws_select;
    if(wr_equal(name,"htonl",0) || wr_equal(name,"ntohl",0))return (void *)&ws_htonl;
    if(wr_equal(name,"htons",0) || wr_equal(name,"ntohs",0))return (void *)&ws_htons;
    if(wr_equal(name,"gethostname",0))return (void *)&ws_gethostname;
    if(wr_equal(name,"inet_ntop",0))return (void *)&ws_inet_ntop;
    if(wr_equal(name,"getaddrinfo",0))return (void *)&ws_getaddrinfo;
    if(wr_equal(name,"freeaddrinfo",0))return (void *)&ws_freeaddrinfo;
    return 0;
}
