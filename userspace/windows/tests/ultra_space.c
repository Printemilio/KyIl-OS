#include "../runtime.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "ultra_probe.h"
static void check(int ok,const char *s){if(!ok){wr_log("ultra-space: FAIL ");wr_log(s);wr_log("\n");wr_exit(1);}}
static int native(vex_process_resource_req_t *r){r->size=sizeof(*r);return (int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)r);}
static int root(unsigned op,int fd,unsigned rights,const char *path,unsigned flags){vex_fs_root_req_t q={.size=sizeof(q),.operation=op,.fd=fd,.rights=rights,.path=path,.flags=flags};return (int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&q);}
static void close_fd(int fd){vex_syscall1(VEX_SYS_CLOSE,fd);}
static uint64_t now(void){struct timespec t;vex_clock_gettime_req_t q={1,&t};check(!vex_syscall1(VEX_SYS_CLOCK_GETTIME,(uintptr_t)&q),"monotonic clock");return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void number(uint64_t v){char b[24];unsigned n=0;do{b[n++]=(char)('0'+v%10);v/=10;}while(v);char s[24];for(unsigned i=0;i<n;i++)s[i]=b[n-1-i];s[n]=0;wr_log(s);}
static void ping(int channel,unsigned service){int fds[2];vex_message_pair_req_t pair={fds,0,0};check(!vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair),"reply pair");
    ultra_probe p={.pid=0x12345678,.uid=0xffffffff,.value=0x123456789abcdef0ull};vex_message_attachment_req_t a={(unsigned)fds[1],VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    vex_message_io_req_t q={channel,0,&p,sizeof(p),&a,1};check(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&q)==sizeof(p),"request");close_fd(fds[1]);
    q=(vex_message_io_req_t){fds[0],0,&p,sizeof(p),0,0};check(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&q)==sizeof(p),"reply");close_fd(fds[0]);
    check(p.pid==wr_pid() && p.uid==vex_syscall0(VEX_SYS_GETUID) && p.tid && p.service==service && p.authority==(VEX_FS_READ|VEX_FS_ENUM) && p.value==0x123456789abcdef0ull,"authenticated sender, not forged payload");
    if(p.stdio!=-9 || p.ambient!=-13 || p.legacy!=-13 || p.gui!=-13){wr_log("ultra-probe: stdio=");number((unsigned)p.stdio);wr_log(" ambient=");number((unsigned)p.ambient);wr_log(" legacy=");number((unsigned)p.legacy);wr_log(" gui=");number((unsigned)p.gui);wr_log("\n");}
    check(p.stdio==-9 && p.ambient==-13 && p.legacy==-13 && p.gui==-13,"no console, path, legacy or GUI ambient authority");
}
typedef struct {int image,scope;unsigned service;uint32_t alive,tid;} concurrent;
static void worker(void *opaque){concurrent *c=opaque;
    for(unsigned i=0;i<8;i++){vex_process_resource_req_t q={.operation=VEX_PROCESS_ULTRA_OPEN,.fd=c->image,.status=c->scope};int cap=native(&q);check(cap>=0,"concurrent domain acquire");ping(cap,c->service);close_fd(cap);}
    vex_syscall1(VEX_SYS_THREAD_EXIT,0);wr_exit(125);
}
int wr_main(uint64_t argc,char **argv){(void)argc;(void)argv;
    int dir=root(VEX_FS_ROOT_CREATE,0,VEX_FS_READ|VEX_FS_ENUM|VEX_FS_EXEC|VEX_FS_TRANSFER,"/usr/native-tests",0);
    int scope=root(VEX_FS_ROOT_CREATE,0,VEX_FS_READ|VEX_FS_ENUM,"/tmp",0);check(dir>=0 && scope>=0,"root authorities");
    int image=root(VEX_FS_ROOT_OPEN,dir,0,"ultra_service",O_RDONLY|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE);check(image>=0,"native image authority");
    uint64_t previous=0;
    for(unsigned cycle=0;cycle<8;cycle++){
        uint64_t t=now();vex_process_resource_req_t q={.operation=VEX_PROCESS_ULTRA_OPEN,.fd=image,.status=scope};int a=native(&q);uint64_t created=now()-t;check(a>=0,"domain creation");
        vex_ultra_info_t x,y;q=(vex_process_resource_req_t){.operation=VEX_PROCESS_ULTRA_QUERY,.fd=a,.out=&x};check(!native(&q) && x.identity>previous,"fresh domain identity");previous=x.identity;
        q=(vex_process_resource_req_t){.operation=VEX_PROCESS_ULTRA_OPEN,.fd=image,.status=scope};int b=native(&q);check(b>=0,"reuse");
        q=(vex_process_resource_req_t){.operation=VEX_PROCESS_ULTRA_QUERY,.fd=b,.out=&y};check(!native(&q) && y.identity==x.identity && y.pid==x.pid && y.clients==2,"controlled reuse");
        vex_rendezvous_req_t atten={.size=sizeof(atten),.operation=VEX_RENDEZVOUS_ATTENUATE,.resource_fd=a,.rights=2};check((int64_t)vex_syscall1(VEX_SYS_RENDEZVOUS,(uintptr_t)&atten)==-13,"no receive authority escalation");
        atten=(vex_rendezvous_req_t){.size=sizeof(atten),.operation=VEX_RENDEZVOUS_REVOKE_ENDPOINT,.resource_fd=a};check((int64_t)vex_syscall1(VEX_SYS_RENDEZVOUS,(uintptr_t)&atten)==-13,"no service ingress revoke");
        q=(vex_process_resource_req_t){.operation=VEX_PROCESS_ULTRA_OBSERVE,.fd=a};int observer=native(&q);check(observer>=0,"non-owning completion observer");
        ultra_probe hostile={0};vex_message_attachment_req_t cycle_cap={(unsigned)a,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
        vex_message_io_req_t cycle_send={a,0,&hostile,sizeof(hostile),&cycle_cap,1};check((int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&cycle_send)==-13,"no self-lifetime attachment cycle");
        close_fd(a);t=now();for(unsigned i=0;i<32;i++)ping(b,x.pid);uint64_t calls=now()-t;
        concurrent contexts[4];void *stacks[4];for(unsigned i=0;i<4;i++){contexts[i]=(concurrent){image,scope,x.pid,1,0};stacks[i]=wr_map(0,65536,3);check(stacks[i] && wr_start_thread(worker,&contexts[i],(uint8_t *)stacks[i]+65536,&contexts[i].tid,&contexts[i].alive)>=0,"concurrent workers");}
        for(unsigned i=0;i<4;i++){while(__atomic_load_n(&contexts[i].alive,__ATOMIC_ACQUIRE))wr_futex(&contexts[i].alive,0,1,0);wr_unmap(stacks[i],65536);}
        int relay[2];vex_message_pair_req_t pair={relay,0,0};check(!vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair),"delegation relay");
        unsigned token=0x1234;vex_message_attachment_req_t attachment={(unsigned)b,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};vex_message_io_req_t message={relay[0],0,&token,4,&attachment,1};
        check(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&message)==4,"ordinary IPC domain delegation");close_fd(b);
        attachment=(vex_message_attachment_req_t){0,VEX_MESSAGE_OBJECT_FD,0};message=(vex_message_io_req_t){relay[1],0,&token,4,&attachment,1};
        check(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&message)==4 && message.attachment_count==1,"queued capability keeps domain alive");b=(int)attachment.handle;close_fd(relay[0]);close_fd(relay[1]);ping(b,x.pid);
        /* A service-exported endpoint must retain its service by itself,
           even after every discovery ingress capability has been closed. */
        int reply[2];pair=(vex_message_pair_req_t){reply,0,0};check(!vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair),"export reply pair");
        ultra_probe exported={.value=ULTRA_EXPORT_PROBE};attachment=(vex_message_attachment_req_t){(unsigned)reply[1],VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
        message=(vex_message_io_req_t){b,0,&exported,sizeof(exported),&attachment,1};check(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&message)==sizeof(exported),"export request");close_fd(reply[1]);
        attachment=(vex_message_attachment_req_t){0,VEX_MESSAGE_OBJECT_FD,0};message=(vex_message_io_req_t){reply[0],0,&exported,sizeof(exported),&attachment,1};
        check(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&message)==sizeof(exported) && message.attachment_count==1,"actual exported endpoint lease");close_fd(reply[0]);close_fd(b);
        q=(vex_process_resource_req_t){.operation=VEX_PROCESS_WAIT,.flags=VEX_PROCESS_WAIT_POLL,.fd=observer};check(native(&q)==-110,"exported endpoint alone keeps service alive");
        t=now();close_fd((int)attachment.handle);q=(vex_process_resource_req_t){.operation=VEX_PROCESS_WAIT,.fd=observer,.timeout_ns=3000000000ull};check(!native(&q),"last capability terminates domain");uint64_t destroyed=now()-t;close_fd(observer);
        q=(vex_process_resource_req_t){.operation=VEX_PROCESS_ULTRA_QUERY,.fd=b,.out=&y};check(native(&q)==-9,"stale domain capability");
        wr_log("ultra-bench: cycle=");number(cycle);wr_log(" create_ns=");number(created);wr_log(" 32_rpc_ns=");number(calls);wr_log(" destroy_ns=");number(destroyed);wr_log("\n");
    }
    close_fd(image);close_fd(dir);close_fd(scope);wr_log("ultra-space: authenticated sender / no ambient paths-stdio-GUI-legacy / rights / reuse / last-reference teardown / stale handles / concurrent opens / queued IPC delegation / exported endpoint lifetime / 520 authenticated calls PASS\n");return 0;
}
