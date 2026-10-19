#include "../runtime.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
static void check(int ok,const char *s){if(!ok){wr_log("native-file-protection: FAIL ");wr_log(s);wr_log("\n");wr_exit(1);}}
static int call(unsigned n,void *p){return (int)(int64_t)vex_syscall1(n,(uintptr_t)p);}
static void close_fd(int fd){check(!vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd),"close");}
static int root(unsigned op,int fd,const char *p,unsigned flags,void *out){vex_fs_root_req_t q={.size=sizeof(q),.operation=op,.fd=fd,.path=p,.flags=flags,.mode=(flags&O_CREAT)?0600:0,.out=out};return call(VEX_SYS_FS_ROOT,&q);}
static int rename_path(const char *a,const char *b){vex_rename_req_t q={a,b};return call(VEX_SYS_RENAME,&q);}
typedef struct {int dir;const char *path;uint32_t alive,tid;} work;
static void worker(void *opaque){work *w=opaque;for(unsigned i=0;i<32;i++){
    int fd=root(VEX_FS_ROOT_OPEN,w->dir,w->path,VEX_FS_OPEN_PROTECTED|O_RDONLY,0);check(fd>=0,"concurrent protected open");
    int duplicate=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,fd);check(duplicate>=0,"concurrent dup");close_fd(fd);close_fd(duplicate);wr_yield();
    }vex_syscall1(VEX_SYS_THREAD_EXIT,0);wr_exit(125);}
int wr_main(uint64_t argc,char **argv){(void)argc;(void)argv;
    vex_user_identity_t id={.size=sizeof(id)};check(!call(VEX_SYS_USER_IDENTITY,&id) && !id.capabilities,"ordinary native subject has no admin capability");
    vex_fs_root_req_t payload={.size=sizeof(payload),.operation=VEX_FS_ROOT_CREATE,.rights=VEX_FS_READ,.path="/usr/native-tests"};
    int archive=call(VEX_SYS_FS_ROOT,&payload);check(archive>=0,"boot archive root");
    int image=root(VEX_FS_ROOT_OPEN,archive,"native_file_protection",VEX_FS_OPEN_PROTECTED,0);check(image>=0,"protected boot archive file without CD-ROM");
    unsigned magic=0;vex_fd_io_req_t read={image,&magic,4};check(call(VEX_SYS_READFD,&read)==4 && magic==0x464c457f,"real boot archive bytes");close_fd(image);close_fd(archive);
    char directory[]="/tmp/protect-00000000",path[96],other[96],target[96],moved[96];unsigned pid=(unsigned)wr_pid();
    for(unsigned i=0;i<8;i++)directory[13+i]="0123456789abcdef"[(pid>>(28-i*4))&15];
    memcpy(path,directory,wr_len(directory)+1);memcpy(path+wr_len(directory),"/data",6);
    memcpy(other,directory,wr_len(directory)+1);memcpy(other+wr_len(directory),"/other",7);
    memcpy(target,directory,wr_len(directory)+1);memcpy(target+wr_len(directory),"/target",8);
    memcpy(moved,directory,wr_len(directory)+1);memcpy(moved+wr_len(directory),"-moved",7);
    vex_mkdir_req_t mkdir={directory,0700};check(!call(VEX_SYS_MKDIR,&mkdir),"mkdir");
    vex_fs_root_req_t grant={.size=sizeof(grant),.operation=VEX_FS_ROOT_CREATE,.rights=VEX_FS_ALL,.path="/tmp"};int dir=call(VEX_SYS_FS_ROOT,&grant);check(dir>=0,"root authority");
    const char *relative=path+5;
    int fd=root(VEX_FS_ROOT_OPEN,dir,relative,O_CREAT|O_RDWR|0x80,0);check(fd>=0,"create");
    static char bytes[4096];bytes[0]='K';check(wr_write(fd,bytes,sizeof(bytes))==sizeof(bytes) && !vex_syscall1(VEX_SYS_FSYNC,fd),"real bytes");close_fd(fd);
    fd=root(VEX_FS_ROOT_OPEN,dir,other+5,O_CREAT|O_RDWR|0x80,0);check(fd>=0,"replacement source");close_fd(fd);
    check(root(VEX_FS_ROOT_OPEN,dir,relative,VEX_FS_OPEN_PROTECTED|O_RDWR,0)==-22,"protected writer forbidden");
    check(root(VEX_FS_ROOT_OPEN,dir,relative,VEX_FS_OPEN_PROTECTED|O_CREAT,0)==-22,"protected create forbidden");
    check(root(VEX_FS_ROOT_OPEN,dir,(void *)0xffff800000001000ull,VEX_FS_OPEN_PROTECTED,0)==-14,"kernel pathname rejected");
    int protected=root(VEX_FS_ROOT_OPEN,dir,relative,VEX_FS_OPEN_PROTECTED|O_CLOEXEC,0);check(protected>=0,"native protected open");
    check(call(VEX_SYS_UNLINK,path)<0 && vex_syscall0(VEX_SYS_LAST_ERROR)==13,"direct native unlink denied");
    check(rename_path(path,target)<0 && rename_path(other,path)<0 && rename_path(directory,moved)<0,"direct rename/source/replace/ancestor denied");
    vex_symlink_req_t link={"/tmp",directory};check(call(VEX_SYS_SYMLINK,&link)<0,"ancestor symlink rebinding denied");
    check(root(VEX_FS_ROOT_REMOVE,dir,relative,0,0)==-13,"root capability cannot bypass");
    vex_fs_root_names_t names={target+5,0};check(root(VEX_FS_ROOT_MOVE,dir,relative,0,&names)==-13,"root move cannot bypass");
    names=(vex_fs_root_names_t){relative,0};check(root(VEX_FS_ROOT_REPLACE,dir,other+5,0,&names)==-13,"root replace cannot bypass");
    fd=root(VEX_FS_ROOT_OPEN,dir,relative,O_RDWR,0);check(fd>=0,"authorized writes remain possible; not an immutable-content lease");close_fd(fd);
    work workers[4]={0};void *stacks[4];for(unsigned i=0;i<4;i++){workers[i]=(work){dir,relative,1,0};stacks[i]=wr_map(0,65536,3);
        check(stacks[i] && wr_start_thread(worker,&workers[i],(char *)stacks[i]+65536,&workers[i].tid,&workers[i].alive)>=0,"concurrent start");}
    for(unsigned i=0;i<4;i++){while(__atomic_load_n(&workers[i].alive,__ATOMIC_ACQUIRE))wr_futex(&workers[i].alive,0,1,0);check(!wr_unmap(stacks[i],65536),"concurrent join");}
    int duplicate=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,protected);check(duplicate>=0,"dup retains protection");close_fd(protected);
    vex_mmap_req_t map={0,4096,1,2,duplicate,0};char *mapped=(void *)(uintptr_t)vex_syscall1(VEX_SYS_MMAP,(uintptr_t)&map);
    check((int64_t)(uintptr_t)mapped>0 && mapped[0]=='K',"real lazy mapping");close_fd(duplicate);
    check(call(VEX_SYS_UNLINK,path)<0,"mapping-only reference protects name");check(!wr_unmap(mapped,4096),"unmap final mapping");
    protected=root(VEX_FS_ROOT_OPEN,dir,relative,VEX_FS_OPEN_PROTECTED,0);check(protected>=0,"IPC protection");
    int endpoints[2];vex_message_pair_req_t pair={endpoints,0,0};check(!call(VEX_SYS_MESSAGE_PAIR,&pair),"IPC pair");
    char byte='x';vex_message_attachment_req_t a={(unsigned)protected,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    vex_message_io_req_t msg={endpoints[0],0,&byte,1,&a,1};check(call(VEX_SYS_MESSAGE_SEND,&msg)==1,"send retained file capability");close_fd(protected);
    check(call(VEX_SYS_UNLINK,path)<0,"queued attachment protects without owner FD");
    a=(vex_message_attachment_req_t){0};msg=(vex_message_io_req_t){endpoints[1],0,&byte,1,&a,1};check(call(VEX_SYS_MESSAGE_RECV,&msg)==1 && msg.attachment_count==1,"receive local FD");
    protected=(int)a.handle;close_fd(endpoints[0]);close_fd(endpoints[1]);
    check(root(VEX_FS_ROOT_OPEN,dir,relative,VEX_FS_OPEN_PROTECTED|0x10000u,0)==-22,"invalid flags");
    /* Inheritance retains the same object. Killing its final owner must drop
       the guard through normal process/FD cleanup, not a creator-PID table. */
    pair=(vex_message_pair_req_t){endpoints,0,0};check(!call(VEX_SYS_MESSAGE_PAIR,&pair),"fork channel");
    int64_t child=(int64_t)vex_syscall0(VEX_SYS_PROCESS_FORK);check(child>=0,"fork");
    if(!child){close_fd(endpoints[0]);vex_message_io_req_t m={endpoints[1],0,&byte,1,0,0};check(call(VEX_SYS_MESSAGE_SEND,&m)==1,"child readiness");
        call(VEX_SYS_MESSAGE_RECV,&m);wr_exit(0);}
    close_fd(endpoints[1]);close_fd(protected);msg=(vex_message_io_req_t){endpoints[0],0,&byte,1,0,0};check(call(VEX_SYS_MESSAGE_RECV,&msg)==1,"inherited owner ready");
    check(call(VEX_SYS_UNLINK,path)<0,"remote inherited reference protects");
    check(!vex_syscall1(VEX_SYS_PROCESS_KILL,(uintptr_t)child),"kill holder");int status=-1;vex_process_wait_req_t wait={(int32_t)child,&status,0};
    check(vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&wait)==(uint64_t)child && status!=0,"remote cleanup completed");close_fd(endpoints[0]);
    check(!call(VEX_SYS_UNLINK,path),"last reference removes protection");
    check(!call(VEX_SYS_UNLINK,other) && !call(VEX_SYS_RMDIR,directory),"cleanup");close_fd(dir);
    wr_log("native-file-protection: direct native/root bypass denied / SMP opens / mapping / queued transfer / fork+kill / last-reference PASS\n");return 0;
}
