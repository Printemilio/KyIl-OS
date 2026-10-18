#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "../runtime.h"
static void check(int ok,const char *why){if(!ok){wr_log("native-process-resource: FAIL ");wr_log(why);wr_log("\n");wr_exit(1);}}
static int resource(vex_process_resource_req_t *r){r->size=sizeof(*r);return (int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)r);}
static int root(unsigned operation,int fd,unsigned rights,const char *path,unsigned flags) {
    vex_fs_root_req_t r={.size=sizeof(r),.operation=operation,.fd=fd,.rights=rights,.path=path,.flags=flags};
    return (int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&r);
}
static uint32_t alive,tid,started;static int waiting_fd;
static void waiter(void *unused) {
    (void)unused;started=1;vex_process_resource_req_t r={.operation=VEX_PROCESS_WAIT,.fd=waiting_fd};
    resource(&r);vex_syscall1(VEX_SYS_THREAD_EXIT,0);wr_exit(1);
}
int wr_main(uint64_t argc,char **argv) {
    if(argc>1 && wr_equal(argv[1],"child",0)) {
        check(argc==3 && wr_equal(argv[2],"two words",0),"actual native argv");
        char **env=argv+argc+1;check(env[0] && wr_equal(env[0],"TOKEN=actual",0) && !env[1],"actual native environment");
        check(root(VEX_FS_ROOT_POLICY,0,0,0,0)==1,"child inherits seal");
        vex_open_ex_req_t ambient={VEX_AT_FDCWD,O_RDONLY,0,0,"/usr/bin/kyil-windows"};
        check((int64_t)vex_syscall1(VEX_SYS_OPEN_EX,(uintptr_t)&ambient)==-13,"no ambient child escape");
        check(root(VEX_FS_ROOT_OPEN,10,0,"native-child-cannot-create",O_CREAT|O_WRONLY)==-13,"delegated root rights unchanged");
        char token[4]={0};vex_message_io_req_t io={11,0,token,3,0,0};
        check(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&io)==3 && wr_equal(token,"run",0),"actual capability bytes");
        void *memory=wr_map(0,64u*1024u*1024u,3);check(memory!=0,"lazy child mapping");
        for(unsigned i=0;i<64;i++)((volatile char *)memory)[i*4096]=(char)i;
        return 42;
    }
    int directory=root(VEX_FS_ROOT_CREATE,0,VEX_FS_READ|VEX_FS_EXEC|VEX_FS_TRANSFER,"/usr/native-tests",0);
    int scope=root(VEX_FS_ROOT_CREATE,0,VEX_FS_ALL,"/tmp",0);check(directory>=0 && scope>=0,"launch authorities");
    int image=root(VEX_FS_ROOT_OPEN,directory,0,"native_process_resource_test",O_RDONLY|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE);
    int plain=root(VEX_FS_ROOT_OPEN,directory,0,"native_process_resource_test",O_RDONLY|VEX_FS_OPEN_REGULAR);
    check(image>=0 && plain>=0,"readonly native image references");
    int ro=root(VEX_FS_ROOT_DERIVE,scope,VEX_FS_READ|VEX_FS_ENUM|VEX_FS_TRANSFER,"",0);
    int no_transfer=root(VEX_FS_ROOT_DERIVE,scope,VEX_FS_READ|VEX_FS_ENUM,"",0);
    check(ro>=0 && no_transfer>=0 && !root(VEX_FS_ROOT_SEAL,0,0,0,0),"seal before launch");
    int channel[2];vex_message_pair_req_t pair={channel,0,0};check(!vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair),"channel");
    const char *args[]={"native-resource-child","child","two words",0},*env[]={"TOKEN=actual",0};
    vex_process_grant_t grants[]={{ro,10,1,0},{channel[1],11,1,0}};
    vex_process_resource_req_t launch={.operation=VEX_PROCESS_LAUNCH,.fd=plain,.argv=args,.envp=env,.grants=grants,.grant_count=2,.flags=VEX_PROCESS_START_SUSPENDED};
    check(resource(&launch)==-13,"plain READ does not grant EXEC");launch.fd=image;
    grants[0].fd=no_transfer;check(resource(&launch)==-13,"nontransferable grant refused");grants[0].fd=ro;
    grants[1].slot=10;check(resource(&launch)==-22,"duplicate child slots refused");grants[1].slot=11;
    launch.argv=(const char *const *)0xffff800000000000ull;check(resource(&launch)==-14,"kernel argv refused");launch.argv=args;
    int fd=resource(&launch);check(fd>=0,"source-backed native launch under seal");
    vex_process_resource_info_t info;vex_process_resource_req_t q={.operation=VEX_PROCESS_QUERY,.fd=fd,.out=&info};
    check(!resource(&q) && !info.exited && info.suspended && info.pid,"suspended publication");
    q.out=(void *)0xffff800000000000ull;check(resource(&q)==-14,"kernel query output refused");q.out=&info;
    vex_process_resource_req_t view={.operation=VEX_PROCESS_VIEW,.fd=fd,.rights=VEX_PROCESS_RIGHT_QUERY|VEX_PROCESS_RIGHT_WAIT};int readonly=resource(&view);
    check(readonly>=0,"process view");view.fd=readonly;view.rights=VEX_PROCESS_RIGHT_ALL;check(resource(&view)==-13,"no rights amplification");
    vex_process_resource_req_t kill={.operation=VEX_PROCESS_TERMINATE,.fd=readonly,.status=99};check(resource(&kill)==-13,"no terminate without right");
    vex_process_resource_req_t resume={.operation=VEX_PROCESS_RESUME,.fd=fd};check(resource(&resume)==1 && resource(&resume)==0,"actual resume");
    vex_process_resource_req_t wait={.operation=VEX_PROCESS_WAIT,.fd=fd,.timeout_ns=20000000};check(resource(&wait)==-110,"real blocked child deadline");
    vex_message_io_req_t send={channel[0],0,"run",3,0,0};check(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send)==3,"release child via actual IPC");
    wait.timeout_ns=5000000000ull;check(!resource(&wait) && !resource(&q) && info.exited && info.status==42 && info.primary_exited,"group completion status");
    vex_syscall1(VEX_SYS_CLOSE,fd);q.fd=readonly;check(!resource(&q) && info.status==42,"metadata survives backing/task teardown");
    vex_syscall1(VEX_SYS_CLOSE,readonly);check(resource(&q)==-9,"closed local capability refused");
    /* Retire a WAIT registration, then terminate the target remotely. */
    launch.flags=0;fd=resource(&launch);check(fd>=0,"second child");waiting_fd=fd;alive=1;
    void *stack=wr_map(0,65536,3);check(stack && wr_start_thread(waiter,0,(char *)stack+65536,&tid,&alive)>=0,"waiter thread");
    while(!__atomic_load_n(&started,__ATOMIC_ACQUIRE))wr_yield();wr_futex(&alive,0,1,20000000);
    check(!wr_stop_thread(tid,17),"kill registered wait");while(__atomic_load_n(&alive,__ATOMIC_ACQUIRE))wr_futex(&alive,0,1,0);
    check(!wr_unmap(stack,65536),"retired waiter stack");kill.fd=fd;check(!resource(&kill),"capability-authorized kill");
    wait.fd=fd;check(!resource(&wait),"real kill completion");q.fd=fd;check(!resource(&q) && info.status==99,"kill status");
    vex_syscall1(VEX_SYS_CLOSE,fd);vex_syscall1(VEX_SYS_CLOSE,image);vex_syscall1(VEX_SYS_CLOSE,plain);
    vex_syscall1(VEX_SYS_CLOSE,ro);vex_syscall1(VEX_SYS_CLOSE,no_transfer);vex_syscall1(VEX_SYS_CLOSE,scope);vex_syscall1(VEX_SYS_CLOSE,directory);
    vex_syscall1(VEX_SYS_CLOSE,channel[0]);vex_syscall1(VEX_SYS_CLOSE,channel[1]);
    wr_log("native-process-resource: snapshot / source EXEC / grants / inherited seal / argv-env / suspend / capabilities / completion metadata / stopped WAIT / terminate PASS\n");return 0;
}
