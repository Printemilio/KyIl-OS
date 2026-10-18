#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "../runtime.h"
static unsigned checks;
typedef struct {int root;uint32_t alive,tid,failed;} stress;
static void check(int ok,const char *name) {
    checks++;if(!ok){wr_log("native-fs-roots: FAIL ");wr_log(name);wr_log("\n");wr_exit(1);}
}
static int root_call(unsigned op,int fd,unsigned rights,const char *path,unsigned flags,unsigned mode,void *out) {
    vex_fs_root_req_t r={.size=sizeof(r),.operation=op,.fd=fd,.rights=rights,.path=path,.flags=flags,.mode=mode,.out=out};
    return (int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&r);
}
static int native_open(const char *path,unsigned flags) {
    vex_open_ex_req_t r={VEX_AT_FDCWD,flags,0600,0,path};return (int)(int64_t)vex_syscall1(VEX_SYS_OPEN_EX,(uintptr_t)&r);
}
static void worker(void *opaque) {
    stress *s=opaque;
    for(unsigned i=0;i<32;i++) {
        int fd=root_call(VEX_FS_ROOT_OPEN,s->root,0,"data",O_RDONLY,0,0);
        if(fd<0){s->failed=1;break;}
        char bytes[16]={0};vex_fd_io_req_t io={fd,bytes,sizeof(bytes)};
        if(vex_syscall1(VEX_SYS_READFD,(uintptr_t)&io)!=12 || !wr_equal(bytes,"rooted-data",0))s->failed=1;
        if(vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd))s->failed=1;
        wr_yield();
    }
    vex_syscall1(VEX_SYS_THREAD_EXIT,0);wr_exit(1);
}
int wr_main(uint64_t argc,char **argv) {
    (void)argc;(void)argv;
    char directory[]="/tmp/root-cap-00000000",outside[]="/tmp/root-out-00000000";
    unsigned pid=(unsigned)wr_pid();
    for(unsigned i=0;i<8;i++) {directory[14+i]="0123456789abcdef"[(pid>>(28-i*4))&15];outside[14+i]=directory[14+i];}
    vex_mkdir_req_t mkdir={directory,0700};check(!vex_syscall1(VEX_SYS_MKDIR,(uintptr_t)&mkdir),"prepare directory");
    int secret=native_open(outside,O_CREAT|O_RDWR|0x80);check(secret>=0,"prepare outside file");
    check(wr_write(secret,"outside",8)==8 && !vex_syscall1(VEX_SYS_FSYNC,(uintptr_t)secret),"prepare outside bytes");
    check(!vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)secret),"outside close");
    char link[128];memcpy(link,directory,wr_len(directory)+1);memcpy(link+wr_len(directory),"/escape-link",13);
    vex_symlink_req_t symlink={outside,link};check(!vex_syscall1(VEX_SYS_SYMLINK,(uintptr_t)&symlink),"prepare symlink");
    int root=root_call(VEX_FS_ROOT_CREATE,0,VEX_FS_ALL,directory,0,0,0);check(root>=0,"root grant");
    vex_fs_root_info_t info,ro_info;
    check(!root_call(VEX_FS_ROOT_QUERY,root,0,0,0,0,&info) && info.identity && info.rights==VEX_FS_ALL && wr_equal(info.path,directory,0),"root identity");
    int ro=root_call(VEX_FS_ROOT_DERIVE,root,VEX_FS_READ|VEX_FS_ENUM,"",0,0,0);check(ro>=0,"attenuated grant");
    check(!root_call(VEX_FS_ROOT_QUERY,ro,0,0,0,0,&ro_info) && info.identity==ro_info.identity && ro_info.rights==(VEX_FS_READ|VEX_FS_ENUM),"identity distinct from rights");
    check(root_call(VEX_FS_ROOT_DERIVE,ro,VEX_FS_WRITE,"",0,0,0)==-13,"no rights amplification");
    check(root_call(VEX_FS_ROOT_OPEN,root,0,"../root-out-00000000",O_RDONLY,0,0)<0,"dotdot refusal");
    check(root_call(VEX_FS_ROOT_OPEN,root,0,"escape-link",O_RDONLY,0,0)<0,"symlink escape refusal");
    char renamed[128];memcpy(renamed,directory,wr_len(directory)+1);memcpy(renamed+wr_len(directory),"-moved",7);
    vex_rename_req_t rename={directory,renamed};check(vex_syscall1(VEX_SYS_RENAME,(uintptr_t)&rename)!=0,"live anchor cannot be rebound");
    int file=root_call(VEX_FS_ROOT_OPEN,root,0,"data",O_CREAT|O_RDWR|0x80,0600,0);check(file>=0,"rooted open");
    check(wr_write(file,"rooted-data",12)==12 && !vex_syscall1(VEX_SYS_FSYNC,(uintptr_t)file),"rooted bytes persist");
    check(!root_call(VEX_FS_ROOT_SEAL,0,0,0,0,0,0) && root_call(VEX_FS_ROOT_POLICY,0,0,0,0,0,0)==1,"irreversible seal");
    check(native_open(outside,O_RDONLY)==-13,"unscoped open cannot bypass root");
    check(root_call(VEX_FS_ROOT_CREATE,0,VEX_FS_READ,"/tmp",0,0,0)==-13,"no post-seal ambient grant");
    check(root_call(VEX_FS_ROOT_OPEN,ro,0,"data",O_RDWR,0,0)==-13,"readonly root rejects writer");
    check(root_call(VEX_FS_ROOT_QUERY,root,0,0,0,0,(void *)0xffff800000001000ull)==-14,"kernel output pointer");
    check(root_call(VEX_FS_ROOT_REMOVE,ro,0,"data",0,0,0)==-13,"removal cannot widen rights");
    check(root_call(VEX_FS_ROOT_REMOVE,root,0,"data",0,0,0)==-16,"removal refuses live writer");
    check(root_call(VEX_FS_ROOT_REMOVE,root,0,"../escape",0,0,0)==-13,"removal cannot escape anchor");
    check(root_call(VEX_FS_ROOT_REMOVE,root,0,"escape-link",0,0,0)==-13,"removal cannot follow escaping link");
    int disposable=root_call(VEX_FS_ROOT_OPEN,root,0,"disposable",O_CREAT|O_RDWR|0x80,0600,0);
    check(disposable>=0 && wr_write(disposable,"erase",5)==5 && !vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)disposable),"prepare removal fixture");
    vex_stat_ex_t removed;
    check(!root_call(VEX_FS_ROOT_REMOVE,root,0,"disposable",0,0,0) && root_call(VEX_FS_ROOT_STAT,root,0,"disposable",0,0,&removed)==-2,"actual removal after final close");
    disposable=root_call(VEX_FS_ROOT_OPEN,root,0,"mapped",O_CREAT|O_RDWR|0x80,0600,0);
    check(disposable>=0 && wr_write(disposable,"mapping",8)==8 && !vex_syscall1(VEX_SYS_FSYNC,(uintptr_t)disposable),"prepare retained mapping");
    vex_mmap_req_t mapping={0,4096,1,2,disposable,0};
    char *mapped=(void *)(uintptr_t)vex_syscall1(VEX_SYS_MMAP,(uintptr_t)&mapping);
    check((int64_t)(uintptr_t)mapped>0 && !vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)disposable),"map outlives fd");
    check(root_call(VEX_FS_ROOT_REMOVE,root,0,"mapped",0,0,0)==-16 && mapped[0]=='m',"removal sees mapping-only reference");
    check(!wr_unmap(mapped,4096) && !root_call(VEX_FS_ROOT_REMOVE,root,0,"mapped",0,0,0),"removal after final mapping reference");
    int reader=root_call(VEX_FS_ROOT_OPEN,ro,0,"data",O_RDONLY,0,0);check(reader>=0,"post-seal rooted open");
    char bytes[32]={0};vex_fd_io_req_t read={reader,bytes,sizeof(bytes)};
    check(vex_syscall1(VEX_SYS_READFD,(uintptr_t)&read)==12 && wr_equal(bytes,"rooted-data",0),"post-seal bytes");
    vex_stat_ex_t st;vex_stat_ex_req_t stat={VEX_STAT_EX_FD,reader,0,&st};
    check(!vex_syscall1(VEX_SYS_STAT_EX,(uintptr_t)&stat) && (st.mode&0777)==0600 && st.size==12,"real rooted FD metadata");
    check(root_call(VEX_FS_ROOT_OPEN,root,0,"escape-link",O_RDONLY,0,0)<0,"sealed symlink refusal");
    stress s[4]={0};void *stacks[4];
    for(unsigned i=0;i<4;i++) {
        s[i].root=ro;s[i].alive=1;stacks[i]=wr_map(0,65536,3);
        check(stacks[i] && wr_start_thread(worker,&s[i],(char *)stacks[i]+65536,&s[i].tid,&s[i].alive)>=0,"SMP readers start");
    }
    for(unsigned i=0;i<4;i++) {
        while(__atomic_load_n(&s[i].alive,__ATOMIC_ACQUIRE))wr_futex(&s[i].alive,0,1,0);
        check(!s[i].failed && !wr_unmap(stacks[i],65536),"SMP readers join/cleanup");
    }
    int64_t child=(int64_t)vex_syscall0(VEX_SYS_PROCESS_FORK);check(child>=0,"fork");
    if(!child) {
        check(root_call(VEX_FS_ROOT_POLICY,0,0,0,0,0,0)==1 && native_open(outside,O_RDONLY)==-13,"fork cannot unseal");
        int own=root_call(VEX_FS_ROOT_OPEN,ro,0,"data",O_RDONLY,0,0);check(own>=0,"inherited root capability");
        vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)own);wr_exit(0);
    }
    int status=-1;vex_process_wait_req_t wait={(int32_t)child,&status,0};
    check(vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&wait)==(uint64_t)child && !status,"fork cleanup");
    check(!root_call(VEX_FS_ROOT_MKDIR,root,0,"producer-root",0,0700,0),"prepare delegated root");
    int payload=root_call(VEX_FS_ROOT_OPEN,root,0,"producer-root/data",O_CREAT|O_RDWR|0x80,0600,0);
    check(payload>=0 && wr_write(payload,"delegated-data",15)==15 &&
        !vex_syscall1(VEX_SYS_FSYNC,(uintptr_t)payload) && !vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)payload),"prepare delegated data");
    int endpoints[2];vex_message_pair_req_t pair={endpoints,0,0};
    check(!vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair),"capability transport");
    vex_message_attachment_req_t attachment={(uint64_t)ro,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    unsigned source_fd=0;vex_message_io_req_t message={endpoints[0],0,&source_fd,sizeof(source_fd),&attachment,1};
    check((int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&message)==-13,"non-transferable root refused");
    int64_t producer=(int64_t)vex_syscall0(VEX_SYS_PROCESS_FORK);check(producer>=0,"producer fork");
    if(!producer) {
        source_fd=(unsigned)root_call(VEX_FS_ROOT_DERIVE,root,VEX_FS_READ|VEX_FS_TRANSFER,"producer-root",0,0,0);
        check((int)source_fd>=0,"producer-owned capability");
        attachment.handle=source_fd;
        check(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&message)==sizeof(source_fd),"real capability send");
        vex_syscall1(VEX_SYS_CLOSE,source_fd);wr_exit(0);
    }
    int occupied=root_call(VEX_FS_ROOT_DERIVE,root,VEX_FS_READ,"",0,0,0);check(occupied>=0,"reserve receiver local slot");
    status=-1;wait=(vex_process_wait_req_t){(int32_t)producer,&status,0};
    check(vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&wait)==(uint64_t)producer && !status,"producer dies before receive");
    attachment=(vex_message_attachment_req_t){0};message=(vex_message_io_req_t){endpoints[1],0,&source_fd,sizeof(source_fd),&attachment,1};
    check(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&message)==sizeof(source_fd) && message.attachment_count==1 &&
        attachment.handle!=source_fd,"new receiver local handle");
    int imported=(int)attachment.handle;
    check(!root_call(VEX_FS_ROOT_QUERY,imported,0,0,0,0,&ro_info) && ro_info.identity!=info.identity &&
        ro_info.rights==(VEX_FS_READ|VEX_FS_TRANSFER),"delegated identity/rights after creator death");
    check(root_call(VEX_FS_ROOT_OPEN,imported,0,"data",O_RDWR,0,0)==-13,"transfer cannot widen rights");
    int received_file=root_call(VEX_FS_ROOT_OPEN,imported,0,"data",O_RDONLY,0,0);check(received_file>=0,"use delegated root");
    memset(bytes,0,sizeof(bytes));read=(vex_fd_io_req_t){received_file,bytes,sizeof(bytes)};
    check(vex_syscall1(VEX_SYS_READFD,(uintptr_t)&read)==15 && wr_equal(bytes,"delegated-data",0),"real delegated file bytes");
    check(!vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)received_file) && !vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)imported) &&
        !vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)occupied),"delegated references cleanup");
    vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)endpoints[0]);vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)endpoints[1]);
    check(!vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)root) && !vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)ro),"root handles close");
    check(root_call(VEX_FS_ROOT_QUERY,root,0,0,0,0,&info)==-9,"closed root handle");
    check(wr_write(file,"!",1)==1 && !vex_syscall1(VEX_SYS_FSYNC,(uintptr_t)file),"file retains authority after root close");
    check(!vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)file) && !vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)reader),"last rooted references close");
    wr_log("native-fs-roots: identity / attenuation / escapes / pin / seal / SMP / fork / IPC-creator-death / file lifetime PASS\n");return 0;
}
