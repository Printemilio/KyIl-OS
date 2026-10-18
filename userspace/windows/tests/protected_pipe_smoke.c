#include <stdint.h>
#include <stddef.h>
void *memcpy(void *dst,const void *src,size_t n){unsigned char *a=dst;const unsigned char *b=src;while(n--)*a++=*b++;return dst;}
#ifdef KYIL_NATIVE_TEST
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "../pipe_protocol.h"
#endif
typedef uint16_t W;
__declspec(dllimport) void *LoadLibraryExW(const W *,void *,unsigned),*GetProcAddress(void *,const char *),*LocalFree(void *),*CreateNamedPipeW(const W *,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,void *),*CreateFileW(const W *,unsigned,unsigned,void *,unsigned,unsigned,void *),*GetStdHandle(unsigned),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) int ConnectNamedPipe(void *,void *),DisconnectNamedPipe(void *),CloseHandle(void *),ReadFile(void *,void *,unsigned,unsigned *,void *),WriteFile(void *,const void *,unsigned,unsigned *,void *),GetExitCodeProcess(void *,unsigned *),GetExitCodeThread(void *,unsigned *),SetHandleInformation(void *,unsigned,unsigned),GetHandleInformation(void *,unsigned *),DuplicateHandle(void *,void *,void *,void **,unsigned,int,unsigned);
__declspec(dllimport) unsigned GetLastError(void),WaitForSingleObject(void *,unsigned),GetModuleFileNameW(void *,W *,unsigned),GetEnvironmentVariableW(const W *,W *,unsigned);
__declspec(dllimport) int SetEnvironmentVariableW(const W *,const W *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
typedef struct {unsigned size,pad;W *reserved,*desktop,*title;unsigned x,y,w,h,xc,yc,fill,flags;W show,bytes;unsigned pad2;void *reserved2,*input,*output,*error;} startup;
typedef struct {void *process,*thread;unsigned pid,tid;} info;
__declspec(dllimport) int CreateProcessW(const W *,W *,void *,void *,int,unsigned,void *,const W *,startup *,info *);
static void check(int ok,const char *why){if(!ok){unsigned error=GetLastError(),n=0,done;while(why[n])n++;OutputDebugStringA(why);WriteFile(GetStdHandle((unsigned)-12),why,n,&done,0);char text[]="error=0x00000000\n";for(unsigned i=0;i<8;i++)text[8+i]="0123456789abcdef"[(error>>(28-i*4))&15];WriteFile(GetStdHandle((unsigned)-12),text,sizeof(text)-1,&done,0);ExitProcess(1);}}
typedef struct {unsigned size,pad;void *sd;int inherit,pad2;} security;
typedef struct {void *multiple;unsigned operation,form,type,pad;void *name;} trustee;
typedef struct {unsigned access,mode,inherit,pad;trustee trustee;} entry;
static const W *name=L"\\\\.\\pipe\\kyil-protected-inherit";
static int (__attribute__((ms_abi)) *convert)(const W *,unsigned,void **,unsigned *);
static unsigned (__attribute__((ms_abi)) *build)(const trustee *,const trustee *,unsigned,const entry *,unsigned,const entry *,const void *,unsigned *,void **);
static unsigned reader(void *h){char b;unsigned n;return !ReadFile(h,&b,1,&n,0) && (GetLastError()==109 || GetLastError()==6)?0:1;}
static void exchange(void *h){int connected=ConnectNamedPipe(h,0);check(connected || GetLastError()==535,"protected-pipe: inherited connect FAIL\n");
    for(unsigned i=0;i<32;i++){unsigned payload[3],n;check(ReadFile(h,payload,sizeof(payload),&n,0) && n==sizeof(payload) && payload[0]==i && payload[1]==i+1,"protected-pipe: child message boundaries FAIL\n");payload[2]=i^0xabcdef;check(WriteFile(h,payload,sizeof(payload),&n,0) && n==sizeof(payload),"protected-pipe: child duplex FAIL\n");}}
void mainCRTStartup(void){W child[4];if(GetEnvironmentVariableW(L"KYIL_PROTECTED_PIPE_CHILD",child,4)){void *h=GetStdHandle((unsigned)-10);exchange(h);check(CloseHandle(h),"protected-pipe: inherited close FAIL\n");ExitProcess(0);}
    void *advapi=LoadLibraryExW(L"advapi32",0,0x800);check(advapi!=0,"protected-pipe: provider FAIL\n");
    convert=GetProcAddress(advapi,"ConvertStringSecurityDescriptorToSecurityDescriptorW");build=GetProcAddress(advapi,"BuildSecurityDescriptorW");check(convert && build,"protected-pipe: security exports FAIL\n");
    void *base=0,*sd=0;unsigned size;
    check(convert(L"D:(A;;GA;;;SY)(A;;GWGR;;;S-1-15-2-1)S:(ML;;;;;S-1-16-0)",1,&base,0),"protected-pipe: actual Crashpad base descriptor FAIL\n");
    entry access={.access=0x10000000,.mode=1,.trustee={.form=1,.name=L"CURRENT_USER"}};
    check(!build(0,0,1,&access,0,0,base,&size,&sd) && !LocalFree(base),"protected-pipe: actual merged current-user descriptor FAIL\n");security a={sizeof(a),0,sd,1,0};
    void *server=CreateNamedPipeW(name,0x80003,6,2,512,512,0,&a);check(server!=(void *)-1,"protected-pipe: protected inheritable creation FAIL\n");
#ifdef KYIL_NATIVE_TEST
    unsigned scopes=0;for(int fd=0;fd<128;fd++){vex_fs_root_info_t details;
        vex_fs_root_req_t root={.size=sizeof(root),.operation=VEX_FS_ROOT_QUERY,.fd=fd,.out=&details};
        if(vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&root) || (details.rights&(VEX_FS_READ|VEX_FS_ENUM))!=(VEX_FS_READ|VEX_FS_ENUM))continue;
        const char *label="kyil-protected-inherit";vex_rendezvous_req_t bypass={.size=sizeof(bypass),.operation=VEX_RENDEZVOUS_CLAIM,.scope_fd=fd,.rights=1,.label=label,.label_size=22};
        check((int64_t)vex_syscall1(VEX_SYS_RENDEZVOUS,(uintptr_t)&bypass)==-2,"protected-pipe: no direct native CLAIM through any held root FAIL\n");scopes++;}
    check(scopes>=3,"protected-pipe: actual prefix, application and system roots tested FAIL\n");
    int prefix=-1,system=-1;for(int fd=0;fd<128;fd++){vex_fs_root_info_t d;vex_fs_root_req_t r={.size=sizeof(r),.operation=VEX_FS_ROOT_QUERY,.fd=fd,.out=&d};if(vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&r))continue;
        unsigned n=0;while(d.path[n])n++;if(n>=11){const char *suffix="/.windows/C";unsigned same=1;for(unsigned k=0;k<11;k++)if(d.path[n-11+k]!=suffix[k])same=0;if(same)prefix=fd;}
        const char *p="/usr/lib/kyil-windows";unsigned k=0;while(p[k] && d.path[k]==p[k])k++;if(!p[k] && !d.path[k])system=fd;}
    check(prefix>=0 && system>=0,"protected-pipe: actual capabilities identified FAIL\n");
    vex_fs_root_req_t derive={.size=sizeof(derive),.operation=VEX_FS_ROOT_DERIVE,.fd=prefix,.rights=VEX_FS_READ|VEX_FS_ENUM,.path=""};
    int readonly=(int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&derive);
    derive=(vex_fs_root_req_t){.size=sizeof(derive),.operation=VEX_FS_ROOT_OPEN,.fd=system,.path="pipe-service",.flags=O_RDONLY|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE};
    int image_cap=(int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&derive);check(readonly>=0 && image_cap>=0,"protected-pipe: attenuated admission fixtures FAIL\n");
    vex_process_resource_req_t open_domain={.size=sizeof(open_domain),.operation=VEX_PROCESS_ULTRA_OPEN,.fd=image_cap,.status=readonly};
    int admission=(int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)&open_domain);check(admission>=0,"protected-pipe: read-only admission FAIL\n");
    int reply[2];vex_message_pair_req_t pair={reply,0,0};check(!vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair),"protected-pipe: admission reply FAIL\n");
    wp_packet packet={.magic=WP_MAGIC,.op=WP_CREATE,.access=3,.mode=6,.limit=1,.name="native-cannot-escalate"};
    vex_message_attachment_req_t attachment={(unsigned)reply[1],VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    vex_message_io_req_t io={admission,0,&packet,sizeof(packet),&attachment,1};check(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&io)==sizeof(packet),"protected-pipe: native mediated request FAIL\n");
    io=(vex_message_io_req_t){reply[0],0,&packet,sizeof(packet),0,0};check(vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&io)==sizeof(packet) && packet.error==5,"protected-pipe: cached service cannot widen read-only scope authority FAIL\n");
    int cleanup[]={readonly,image_cap,admission,reply[0],reply[1]};for(unsigned k=0;k<5;k++)vex_syscall1(VEX_SYS_CLOSE,cleanup[k]);

#endif
    unsigned flags=0;check(GetHandleInformation(server,&flags) && (flags&1),"protected-pipe: real inheritance flag FAIL\n");
    void *second=CreateNamedPipeW(name,3,6,2,512,512,0,0);check(second!=(void *)-1 && CreateNamedPipeW(name,3,6,2,512,512,0,0)==(void *)-1 && GetLastError()==231 && CloseHandle(second),"protected-pipe: instances and backpressure FAIL\n");
    check(SetEnvironmentVariableW(L"KYIL_PROTECTED_PIPE_CHILD",L"1"),"protected-pipe: child environment FAIL\n");W image[512];check(GetModuleFileNameW(0,image,512)>0,"protected-pipe: image FAIL\n");
    startup s={.size=sizeof(s),.flags=0x100,.input=server,.output=GetStdHandle((unsigned)-11),.error=GetStdHandle((unsigned)-12)};info pi;
    check(CreateProcessW(image,0,0,0,1,0,0,0,&s,&pi),"protected-pipe: real process handle inheritance FAIL\n");
    check(SetEnvironmentVariableW(L"KYIL_PROTECTED_PIPE_CHILD",0) && CloseHandle(server),"protected-pipe: producer closes before child use FAIL\n");
    void *client=CreateFileW(name,0xc0000000,0,0,3,0,0);check(client!=(void *)-1,"protected-pipe: authenticated open after producer close FAIL\n");
    for(unsigned i=0;i<32;i++){unsigned p[3]={i,i+1,0},n;check(WriteFile(client,p,sizeof(p),&n,0) && n==sizeof(p) && ReadFile(client,p,sizeof(p),&n,0) && n==sizeof(p) && p[2]==(i^0xabcdef),"protected-pipe: inherited duplex bytes FAIL\n");}
    unsigned status=99;check(CloseHandle(client) && !WaitForSingleObject(pi.process,10000) && GetExitCodeProcess(pi.process,&status) && !status && CloseHandle(pi.thread) && CloseHandle(pi.process),"protected-pipe: child exit and lifetime FAIL\n");
    void *denied=0;check(convert(L"D:",1,&denied,0),"protected-pipe: empty DACL FAIL\n");a.sd=denied;a.inherit=0;
    server=CreateNamedPipeW(L"\\\\.\\pipe\\kyil-denied",3,6,1,0,0,0,&a);check(server!=(void *)-1 && CreateFileW(L"\\\\.\\pipe\\kyil-denied",0xc0000000,0,0,3,0,0)==(void *)-1 && GetLastError()==5 && CloseHandle(server) && !LocalFree(denied),"protected-pipe: empty DACL really denies native subject FAIL\n");
    check(convert(L"D:(A;;GR;;;WD)",1,&denied,0),"protected-pipe: read DACL FAIL\n");a.sd=denied;
    server=CreateNamedPipeW(L"\\\\.\\pipe\\kyil-readonly",3,6,1,0,0,0,&a);check(server!=(void *)-1 && CreateFileW(L"\\\\.\\pipe\\kyil-readonly",0x40000000,0,0,3,0,0)==(void *)-1 && GetLastError()==5,"protected-pipe: requested write denied FAIL\n");
    client=CreateFileW(L"\\\\.\\pipe\\kyil-readonly",0x80000000,0,0,3,0,0);check(client!=(void *)-1,"protected-pipe: requested read granted FAIL\n");
    int connected=ConnectNamedPipe(server,0);check(connected || GetLastError()==535,"protected-pipe: read-only connection FAIL\n");
    unsigned n;check(!WriteFile(client,"bad",3,&n,0) && GetLastError()==5,"protected-pipe: endpoint rights remain attenuated FAIL\n");
    check(DisconnectNamedPipe(server) && !ReadFile(client,child,2,&n,0) && GetLastError()==233 && CloseHandle(client) && CloseHandle(server) && !LocalFree(denied),"protected-pipe: disconnect revokes all aliases FAIL\n");
    check(!LocalFree(sd),"protected-pipe: independent descriptor ownership FAIL\n");
    OutputDebugStringA("windows-protected-pipe: Crashpad SD / authenticated rights / empty-readonly DACL / native bypass refused / instances / inherited producer close / 32 duplex messages / disconnect revoke / lifetime PASS\n");ExitProcess(0);
}
