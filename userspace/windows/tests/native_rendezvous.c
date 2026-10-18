#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "../runtime.h"
static void check(int ok,const char *reason){if(!ok){wr_log("rendezvous: FAIL ");wr_log(reason);wr_log("\n");wr_exit(1);}}
static int call(vex_rendezvous_req_t *r){r->size=sizeof(*r);return (int)(int64_t)vex_syscall1(VEX_SYS_RENDEZVOUS,(uintptr_t)r);}
static int root(unsigned operation,int fd,unsigned rights,const char *path) {
    vex_fs_root_req_t r={sizeof(r),operation,fd,rights,path,0,0,0,0};return (int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&r);
}
static uint32_t alive,tid,ready,done;static int lease,scope,client;
static int receiving_fd;static void *receiving_buffer;static int64_t receive_result;
static void invalidated_receive(void *unused) {
    (void)unused;vex_message_attachment_req_t attachment={0,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    vex_message_io_req_t request={receiving_fd,0,receiving_buffer,4096,&attachment,1};
    __atomic_store_n(&ready,1,__ATOMIC_RELEASE);
    receive_result=(int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&request);
    __atomic_store_n(&done,1,__ATOMIC_RELEASE);wr_futex(&done,1,1,0);
    vex_syscall1(VEX_SYS_THREAD_EXIT,0);wr_exit(125);
}
static void usercopy_rollback(void) {
    int transport[2],attached[2];vex_message_pair_req_t pair={transport,0,0};
    check(!vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair),"rollback transport");
    pair.fds=attached;check(!vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair),"rollback attachment");
    receiving_fd=transport[1];receiving_buffer=wr_map(0,4096,3);void *stack=wr_map(0,65536,3);
    check(receiving_buffer && stack,"rollback mappings");alive=1;ready=done=0;receive_result=123;
    check(wr_start_thread(invalidated_receive,0,(char *)stack+65536,&tid,&alive)>=0,"rollback waiter");
    while(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE))wr_yield();
    check(wr_futex(&done,0,0,20000000ull)==-110 && !done,"receiver blocked before unmap");
    check(!wr_unmap(receiving_buffer,4096),"unmap blocked receiver output");
    vex_message_attachment_req_t a={(uint64_t)attached[1],VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    vex_message_io_req_t send={transport[0],0,"hello",5,&a,1};
    check(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send)==5,"enqueue after receiver unmap");
    vex_syscall1(VEX_SYS_CLOSE,attached[1]);
    while(__atomic_load_n(&alive,__ATOMIC_ACQUIRE))wr_futex(&alive,0,1,0);
    check(receive_result==-14,"invalidated receive returns EFAULT");
    char b;vex_message_io_req_t eof={attached[0],MSG_DONTWAIT,&b,1,0,0};
    check(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&eof)==0,"failed copy-out releases imported attachment");
    check(!wr_unmap(stack,65536),"rollback thread stack reclaimed");
    vex_syscall1(VEX_SYS_CLOSE,transport[0]);vex_syscall1(VEX_SYS_CLOSE,transport[1]);vex_syscall1(VEX_SYS_CLOSE,attached[0]);
}
static void waiting(void *unused) {
    (void)unused;__atomic_store_n(&ready,1,__ATOMIC_RELEASE);
    vex_rendezvous_req_t r={.operation=VEX_RENDEZVOUS_WAIT,.scope_fd=scope,.label="entry",.label_size=5,.timeout_ns=5000000000ull};
    done=call(&r)==0?1:2;vex_syscall1(VEX_SYS_THREAD_EXIT,0);wr_exit(125);
}
static void creator_lifetime(void) {
    int control[2];vex_message_pair_req_t pair={control,0,0};
    check(!vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair),"creator control pair");
    int64_t producer=(int64_t)vex_syscall0(VEX_SYS_PROCESS_FORK);check(producer>=0,"creator fork");
    if(!producer) {
        vex_syscall1(VEX_SYS_CLOSE,control[0]);int owned[2];pair.fds=owned;
        check(!vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair),"creator data pair");
        vex_rendezvous_req_t pub={.operation=VEX_RENDEZVOUS_PUBLISH,.scope_fd=scope,.resource_fd=owned[1],
            .rights=VEX_ENDPOINT_SEND,.limit=1,.label="creator",.label_size=7};
        check(call(&pub)>=0,"creator publication");
        char ready='R';vex_message_attachment_req_t a={(uint64_t)owned[0],VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
        vex_message_io_req_t io={control[1],0,&ready,1,&a,1};
        check(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&io)==1,"creator transfer peer");
        io.attachments=0;io.attachment_count=0;
        check(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&io)==1 && ready=='A',"creator handoff acknowledgment");
        wr_exit(0); /* Registration and original endpoints die here. */
    }
    vex_syscall1(VEX_SYS_CLOSE,control[1]);char ready=0;
    vex_message_attachment_req_t a={0,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    vex_message_io_req_t io={control[0],0,&ready,1,&a,1};
    check(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&io)==1 && ready=='R' && io.attachment_count==1,"receive creator peer");
    int peer=(int)a.handle;
    vex_rendezvous_req_t claim={.operation=VEX_RENDEZVOUS_CLAIM,.scope_fd=scope,.label="creator",.label_size=7,.rights=VEX_ENDPOINT_SEND};
    int endpoint=call(&claim);check(endpoint>=0,"claim before creator exit");
    vex_rendezvous_req_t commit={.operation=VEX_RENDEZVOUS_COMMIT,.resource_fd=endpoint};check(!call(&commit),"creator claim commit");
    ready='A';io.attachments=0;io.attachment_count=0;check(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&io)==1,"allow creator exit");
    int status=-1;vex_process_wait_req_t wait={(int32_t)producer,&status,0};
    check((int64_t)vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&wait)==producer && !status,"creator reaped");
    claim.operation=VEX_RENDEZVOUS_QUERY;claim.rights=0;
    check(call(&claim)==-2,"creator death removes registration");
    char bytes[5];io=(vex_message_io_req_t){endpoint,0,"alive",5,0,0};
    check(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&io)==5,"claimed endpoint after creator death");
    io.fd=peer;io.buffer=bytes;check(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&io)==5 && bytes[0]=='a' && bytes[4]=='e',"exact retained channel bytes");
    vex_syscall1(VEX_SYS_CLOSE,endpoint);vex_syscall1(VEX_SYS_CLOSE,peer);vex_syscall1(VEX_SYS_CLOSE,control[0]);
}
static void reservation_generation(void) {
    int endpoints[2];vex_message_pair_req_t pair={endpoints,0,0};
    check(!vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair),"generation pair");
    vex_rendezvous_req_t pub={.operation=VEX_RENDEZVOUS_PUBLISH,.scope_fd=scope,.resource_fd=endpoints[1],
        .rights=VEX_ENDPOINT_SEND,.limit=1,.label="generation",.label_size=10};
    int registration=call(&pub);check(registration>=0,"generation publication");
    vex_rendezvous_req_t claim={.operation=VEX_RENDEZVOUS_CLAIM,.scope_fd=scope,.rights=VEX_ENDPOINT_SEND,.label="generation",.label_size=10};
    int old=call(&claim);check(old>=0,"old reservation");
    vex_rendezvous_req_t rearm={.operation=VEX_RENDEZVOUS_REARM,.resource_fd=registration};check(!call(&rearm),"rearm with old reservation retained");
    int current=call(&claim);check(current>=0,"new reservation");
    vex_rendezvous_req_t commit={.operation=VEX_RENDEZVOUS_COMMIT,.resource_fd=old};check(call(&commit)==-125,"stale reservation cannot commit");
    vex_syscall1(VEX_SYS_CLOSE,old);check(call(&claim)==-11,"old close cannot cancel new reservation");
    commit.resource_fd=current;check(!call(&commit),"new reservation commits");
    vex_syscall1(VEX_SYS_CLOSE,current);vex_syscall1(VEX_SYS_CLOSE,registration);
    vex_syscall1(VEX_SYS_CLOSE,endpoints[0]);vex_syscall1(VEX_SYS_CLOSE,endpoints[1]);
}
int wr_main(uint64_t argc,char **argv) {
    (void)argc;(void)argv;scope=root(VEX_FS_ROOT_CREATE,0,VEX_FS_ALL,"/tmp");check(scope>=0,"scope");
    int ro=root(VEX_FS_ROOT_DERIVE,scope,VEX_FS_READ|VEX_FS_ENUM,"");check(ro>=0,"readonly scope");
    int pair[2];vex_message_pair_req_t p={pair,0,0};check(!vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&p),"pair");
    vex_rendezvous_req_t publish={.operation=VEX_RENDEZVOUS_PUBLISH,.scope_fd=scope,.resource_fd=pair[1],.rights=VEX_ENDPOINT_SEND,.limit=2,.label="entry",.label_size=5,.traits_size=4,.traits={1,2,3,4}};
    lease=call(&publish);check(lease>=0,"publish");publish.scope_fd=ro;check(call(&publish)==-13,"publish from readonly refused");publish.scope_fd=scope;
    int second=call(&publish);check(second>=0,"second offer");check(call(&publish)==-11,"offer limit");
    vex_rendezvous_req_t q={.operation=VEX_RENDEZVOUS_QUERY,.scope_fd=ro,.label="entry",.label_size=5};check(call(&q)==1 && q.traits_size==4 && q.traits[3]==4,"real traits");
    q.limit=0; /* QUERY's output limit is not an input to subsequent lookups. */
    int foreign=root(VEX_FS_ROOT_CREATE,0,VEX_FS_READ|VEX_FS_ENUM,"/usr");check(foreign>=0,"distinct scope");
    q.scope_fd=foreign;check(call(&q)==-2,"same label in different root is inaccessible");
    q.scope_fd=pair[0];check(call(&q)==-9,"endpoint is not a root capability");
    q.scope_fd=ro;vex_syscall1(VEX_SYS_CLOSE,foreign);
    q.operation=VEX_RENDEZVOUS_CLAIM;q.limit=0;q.rights=VEX_ENDPOINT_SEND;client=call(&q);check(client>=0,"claim local fd");
    vex_rendezvous_req_t view={.operation=VEX_RENDEZVOUS_ATTENUATE,.resource_fd=client,.rights=7};check(call(&view)==-13,"no rights expansion");
    char bytes[8];vex_message_io_req_t io={client,0,bytes,1,0,0};check((int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&io)==-13,"receive refused on send-only view");
    io.buffer="hello";io.length=5;check(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&io)==5,"real send");io.fd=pair[0];io.buffer=bytes;io.length=sizeof(bytes);check(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&io)==5 && bytes[0]=='h',"real receive");
    /* Abort restores availability, including views retaining a reservation. */
    view.resource_fd=client;view.rights=VEX_ENDPOINT_SEND;int copy=call(&view);check(copy>=0,"view retains reservation");
    vex_syscall1(VEX_SYS_CLOSE,client);vex_syscall1(VEX_SYS_CLOSE,copy);
    client=call(&q);check(client>=0,"claim restored after abort");
    vex_rendezvous_req_t commit={.operation=VEX_RENDEZVOUS_COMMIT,.resource_fd=client};check(!call(&commit),"commit");
    vex_syscall1(VEX_SYS_CLOSE,client);client=call(&q);check(client>=0,"claim second offer");commit.resource_fd=client;check(!call(&commit),"commit second");
    q.operation=VEX_RENDEZVOUS_WAIT;q.rights=0;q.traits_size=0;q.timeout_ns=1000000;check(call(&q)==-110,"real wait timeout");
    void *stack=wr_map(0,65536,3);check(stack!=0,"stack");alive=1;
    check(wr_start_thread(waiting,0,(char *)stack+65536,&tid,&alive)>=0,"waiter thread");
    while(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE))wr_yield();
    vex_rendezvous_req_t rearm={.operation=VEX_RENDEZVOUS_REARM,.resource_fd=lease};check(!call(&rearm),"rearm");
    while(__atomic_load_n(&alive,__ATOMIC_ACQUIRE))wr_futex(&alive,0,1,0);check(done==1,"scheduler wake no lost wake");wr_unmap(stack,65536);
    /* Make both offers unavailable, then retire waiters and their stacks.
       REARM afterwards must never access the retired stack registration. */
    q.operation=VEX_RENDEZVOUS_CLAIM;q.rights=VEX_ENDPOINT_SEND;q.timeout_ns=0;q.traits_size=4;
    q.traits[0]=1;q.traits[1]=2;q.traits[2]=3;q.traits[3]=4;
    int occupied=call(&q);check(occupied>=0,"occupy rearmed offer");commit.resource_fd=occupied;check(!call(&commit),"occupy commit");vex_syscall1(VEX_SYS_CLOSE,occupied);
    for(unsigned i=0;i<8;i++) {
        stack=wr_map(0,65536,3);check(stack!=0,"stopped waiter stack");ready=done=0;alive=1;
        check(wr_start_thread(waiting,0,(char *)stack+65536,&tid,&alive)>=0,"stopped waiter start");
        while(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE))wr_yield();
        check(wr_futex(&done,0,0,20000000ull)==-110 && !done,"waiter remains blocked");
        check(!wr_stop_thread(tid,17),"stop blocked waiter");
        while(__atomic_load_n(&alive,__ATOMIC_ACQUIRE))wr_futex(&alive,0,1,0);
        check(!wr_unmap(stack,65536),"retire waiter stack");
    }
    check(!call(&rearm),"rearm after stopped waiters");
    vex_syscall1(VEX_SYS_CLOSE,lease);vex_syscall1(VEX_SYS_CLOSE,second);
    q.operation=VEX_RENDEZVOUS_QUERY;q.rights=0;q.timeout_ns=0;check(call(&q)==-2,"last lease removes discovery");
    /* Acquired view survives registration closure and original sender close. */
    vex_syscall1(VEX_SYS_CLOSE,pair[1]);io.fd=client;io.buffer="alive";io.length=5;check(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&io)==5,"backing lifetime");
    io.fd=pair[0];io.buffer=bytes;check(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&io)==5,"backing after close");
    vex_syscall1(VEX_SYS_CLOSE,client);vex_syscall1(VEX_SYS_CLOSE,pair[0]);
    creator_lifetime();reservation_generation();usercopy_rollback();vex_syscall1(VEX_SYS_CLOSE,ro);vex_syscall1(VEX_SYS_CLOSE,scope);
    wr_log("native-rendezvous: isolated scopes / two offers / traits / rights / abort-commit / wait-wake / stopped waiters / creator death / retained backing / blocked receive-unmap rollback PASS\n");return 0;
}
