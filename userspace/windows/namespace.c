#include "windows.h"
#include "process.h"
#include "resource_protocol.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
static char prefix[512],application[512],username[32],guest_image[512],guest_cwd[512];
static char drive_cwd[2][512];
static int cwd_anchor[2]={-1,-1};
static unsigned current_drive;
static wr_mutex cwd_gate;
static int c_root=-1,a_root=-1,system_root=-1;
static int resource_service=-1; /* serialized by cwd_gate; native FD teardown owns it */
static int prefix_matches(const char *,const char *);
static int path_native_locked(const uint16_t *,char *,size_t,int);
static int set_cwd_locked(const uint16_t *,int);
static int native_root(unsigned op,int fd,unsigned rights,const char *path,unsigned flags,unsigned mode,void *out) {
    vex_fs_root_req_t r={.size=sizeof(r),.operation=op,.fd=fd,.rights=rights,.path=path,.flags=flags,.mode=mode,.out=out};
    return (int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&r);
}
static void close_fd(int fd){if(fd>=0)vex_syscall1(VEX_SYS_CLOSE,fd);}
static int protected_open(int root,const char *path,unsigned flags) {
    if(wr_len(path)>=sizeof(((kr_open *)0)->path))return -36;
    if(resource_service<0) {
        int image=native_root(VEX_FS_ROOT_OPEN,system_root,0,"resource-service",O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE,0,0);
        if(image<0)return image;
        vex_process_resource_req_t launch={.size=sizeof(launch),.operation=VEX_PROCESS_ULTRA_OPEN,.fd=image,.status=c_root};
        resource_service=(int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)&launch);close_fd(image);
        if(resource_service<0)return resource_service;
    }
    int grant=native_root(VEX_FS_ROOT_DERIVE,root,VEX_FS_READ|VEX_FS_TRANSFER,"",0,0,0);if(grant<0)return grant;
    int fds[2];vex_message_pair_req_t pair={fds,0,0};int n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair);
    if(n){close_fd(grant);return n;}
    kr_open p={.magic=KR_MAGIC,.flags=flags};memcpy(p.path,path,wr_len(path)+1);
    vex_message_attachment_req_t a[2]={{(unsigned)grant,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP},{(unsigned)fds[1],VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP}};
    vex_message_io_req_t q={resource_service,0,&p,sizeof(p),a,2};
    n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&q);close_fd(grant);close_fd(fds[1]);
    if(n!=sizeof(p)){close_fd(fds[0]);return n<0?n:-5;}
    a[0]=(vex_message_attachment_req_t){UINT64_MAX,VEX_MESSAGE_OBJECT_FD,0};q=(vex_message_io_req_t){fds[0],0,&p,sizeof(p),a,1};
    n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&q);close_fd(fds[0]);
    if(n!=sizeof(p) || p.magic!=KR_MAGIC || p.result>0 || (!!q.attachment_count)!=!p.result) {
        if(n>=0 && q.attachment_count)close_fd((int)a[0].handle);return n<0?n:-5;
    }
    return p.result?p.result:(int)a[0].handle;
}
static int grant_root(const char *path,unsigned rights) {
    int fd=native_root(VEX_FS_ROOT_CREATE,0,rights,path,0,0,0);if(fd<0)return -1;
    vex_fs_root_info_t info;
    if(native_root(VEX_FS_ROOT_QUERY,fd,0,0,0,0,&info) || !wr_equal(path,info.path,0) || info.rights!=rights) {
        vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);return -1;
    }
    return fd;
}
static int append(char *p,size_t cap,const char *s) {
    size_t n=wr_len(p),m=wr_len(s);if(n>=cap || m>=cap-n)return -1;memcpy(p+n,s,m+1);return 0;
}
static int directory(const char *p,unsigned mode) {
    vex_stat_ex_t st;vex_stat_ex_req_t q={VEX_STAT_EX_LPATH,-1,p,&st};
    if(!vex_syscall1(VEX_SYS_STAT_EX,(uintptr_t)&q))return (st.mode&0170000)==0040000?0:-1;
    vex_mkdir_req_t m={p,mode};return (int)vex_syscall1(VEX_SYS_MKDIR,(uintptr_t)&m);
}
int wr_namespace_init(const char *exe) {
    if(c_root>=0 && a_root>=0 && system_root>=0)return 0;
    vex_user_identity_t id={.size=sizeof(id)};
    if(vex_syscall1(VEX_SYS_USER_IDENTITY,(uintptr_t)&id))return -1;
    memcpy(username,id.name,sizeof(username));memcpy(prefix,id.home,wr_len(id.home)+1);
    if(append(prefix,sizeof(prefix),"/.windows") || directory(prefix,0700))return -1;
    if(append(prefix,sizeof(prefix),"/C") || directory(prefix,0700))return -1;
    char path[512];memcpy(path,prefix,wr_len(prefix)+1);
    if(append(path,sizeof(path),"/Windows") || directory(path,0700))return -1;
    if(append(path,sizeof(path),"/System32") || directory(path,0700))return -1;
    memcpy(path,prefix,wr_len(prefix)+1);
    if(append(path,sizeof(path),"/Users") || directory(path,0700) || append(path,sizeof(path),"/") ||
        append(path,sizeof(path),username) || directory(path,0700))return -1;
    const char *dirs[]={"/AppData","/AppData/Local","/AppData/Roaming","/Desktop","/Documents","/Downloads","/Temp"};
    for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++) {
        char child[512];memcpy(child,path,wr_len(path)+1);if(append(child,sizeof(child),dirs[i]) || directory(child,0700))return -1;
    }
    /* A: is the explicitly launched package directory, read-only. No default
       mapping exposes the native root/home. Ordinary native permissions remain
       authoritative; Windows spelling never grants a capability. */
    size_t n=wr_len(exe),slash=0;for(size_t i=0;i<n;i++)if(exe[i]=='/')slash=i;
    if(!slash || slash>=sizeof(application))return -1;
    memcpy(application,exe,slash);application[slash]=0;
    memcpy(guest_image,"A:\\",4);if(append(guest_image,sizeof(guest_image),exe+slash+1))return -1;
    memcpy(guest_cwd,"C:\\Users\\",10);if(append(guest_cwd,sizeof(guest_cwd),username))return -1;
    c_root=grant_root(prefix,VEX_FS_ALL);
    a_root=grant_root(application,VEX_FS_READ|VEX_FS_ENUM|VEX_FS_EXEC|VEX_FS_TRANSFER);
    system_root=grant_root("/usr/lib/kyil-windows",VEX_FS_READ|VEX_FS_ENUM|VEX_FS_EXEC|VEX_FS_TRANSFER);
    if(c_root<0 || a_root<0 || system_root<0)return -1;
    memcpy(drive_cwd[0],guest_cwd,wr_len(guest_cwd)+1);memcpy(drive_cwd[1],"A:\\",4);
    uint16_t initial[512];if(wr_utf16(guest_cwd,initial,512)<0 || wr_namespace_set_cwd(initial))return -1;
    if(native_root(VEX_FS_ROOT_SEAL,0,0,0,0,0,0))return -1;
    wr_log("windows: C/A root capabilities bound; native pathname access sealed\n");return 0;
}
const char *wr_guest_image(void) {return guest_image;}
int wr_prefix_root(void) {return c_root;}
int wr_application_root(void) {return a_root;}
int wr_system_root(void) {return system_root;}
int wr_cwd_wide(uint16_t *out,size_t capacity){wr_lock(&cwd_gate);int n=wr_utf16(guest_cwd,out,capacity);wr_unlock(&cwd_gate);return n;}
void wr_namespace_quiesce(int begin){if(begin)wr_lock(&cwd_gate);else wr_unlock(&cwd_gate);}
int wr_native_mapped_name(const char *path,uint16_t *out,size_t capacity) {
    const char *root=0,*device=0;size_t n;
    if(prefix_matches(path,prefix)){root=prefix;device="\\Device\\KyIlPrefix";}
    if(!root && prefix_matches(path,application)){root=application;device="\\Device\\KyIlPackage";}
    const char *system="/usr/lib/kyil-windows";
    if(!root && prefix_matches(path,system)){root=system;device="\\Device\\KyIlSystem";}
    if(!root)return -1;char text[1024];n=wr_len(device);memcpy(text,device,n);
    const char *tail=path+wr_len(root);while(*tail){if(n+1>=sizeof(text))return -1;text[n++]=*tail=='/'?'\\':*tail;tail++;}text[n]=0;
    return wr_utf16(text,out,capacity);
}
typedef struct {uint16_t units[64];size_t first,count;} wide_reader;
static int wide_read(const uint16_t *source,size_t index,uint16_t *unit,wide_reader *reader){
    if(index<reader->first || index-reader->first>=reader->count){
        if(index>UINTPTR_MAX/2 || (uintptr_t)source>UINTPTR_MAX-index*2)return -1;
        uintptr_t address=(uintptr_t)source+index*2;size_t count=(4096-(address&4095))/2;
        if(!count)count=1;if(count>64)count=64;
        if(wr_memory_copy(reader->units,(const void *)address,count*2))return -1;
        reader->first=index;reader->count=count;
    }
    *unit=reader->units[index-reader->first];return 0;
}
static int from_utf16(const uint16_t *s,char *out,size_t cap) {
    size_t n=0;
    wide_reader reader={.first=SIZE_MAX};
    for(size_t i=0;;i++) {
        uint16_t unit;if(wide_read(s,i,&unit,&reader))return -1;if(!unit)break;
        uint32_t c=unit;if(c>=0xd800 && c<=0xdbff) {uint16_t low;if(wide_read(s,++i,&low,&reader) || low<0xdc00 || low>0xdfff)return -1;c=0x10000+((c-0xd800)<<10)+(low-0xdc00);}
        else if(c>=0xdc00 && c<=0xdfff)return -1;
        unsigned count=c<128?1:(c<2048?2:(c<65536?3:4));if(n+count>=cap)return -1;
        if(count==1)out[n++]=(char)c;
        else {
            out[n++]=(char)((count==2?0xc0:(count==3?0xe0:0xf0))|(c>>(6*(count-1))));
            for(unsigned j=count-1;j;j--)out[n++]=(char)(0x80|((c>>(6*(j-1)))&63));
        }
    }
    out[n]=0;return 0;
}
int wr_utf8(const uint16_t *s,char *out,size_t cap){return from_utf16(s,out,cap);}
static int path_native_locked(const uint16_t *source,char *out,size_t cap,int writing) {
    char path[1024];if(!source || from_utf16(source,path,sizeof(path)))return -1;
    char *s=path;for(size_t i=0;s[i];i++)if(s[i]=='\\')s[i]='/';
    if(s[0]=='/' && s[1] && s[2] && s[3] &&
       ((s[1]=='/' && s[2]=='?' && s[3]=='/') || (s[1]=='?' && s[2]=='?' && s[3]=='/')))s+=4;
    int app=(int)current_drive;
    if(s[0] && s[1]==':') {
        if(s[0]=='A'||s[0]=='a')app=1;
        else if(s[0]=='C'||s[0]=='c')app=0;else return -1;
        s+=2;
    }
    if(app && writing)return -1;
    if(s[0]=='/' && s[1]=='/')return -1; /* UNC has no configured mapping. */
    if(wr_len(app?application:prefix)>=cap)return -1;
    memcpy(out,app?application:prefix,wr_len(app?application:prefix)+1);
    size_t root=wr_len(out);
    if(*s!='/') {
        const char *cwd=drive_cwd[app];
        if(cwd[1]!=':' || (cwd[2]!='\\' && cwd[2]!='/'))return -1;
        for(size_t i=2;cwd[i];i++) {size_t at=wr_len(out);if(at+1>=cap)return -1;out[at]=cwd[i]=='\\'?'/':cwd[i];out[at+1]=0;}
    }
    while(*s) {
        while(*s=='/')s++;if(!*s)break;
        char part[256];size_t n=0;
        while(*s && *s!='/') {if(n>=sizeof(part)-1 || *s==':' || (unsigned char)*s<32)return -1;part[n++]=*s++;}
        part[n]=0;
        if(wr_equal(part,".",0))continue;
        if(wr_equal(part,"..",0)) {
            size_t at=wr_len(out);while(at>root && out[at-1]!='/')at--;
            if(at<=root)return -1;out[at-1]=0;continue;
        }
        if(append(out,cap,"/") || append(out,cap,part))return -1;
    }
    return 0;
}
int wr_path_native(const uint16_t *source,char *out,size_t cap,int writing){
    wr_lock(&cwd_gate);int result=path_native_locked(source,out,cap,writing);wr_unlock(&cwd_gate);return result;
}
static int prefix_matches(const char *path,const char *root) {
    size_t i=0;while(root[i]) {if(path[i]!=root[i])return 0;i++;}return !path[i] || path[i]=='/';
}
/* Normalize with the same confined path policy as actual opens. Never expose
 * native prefix spelling through a Windows pathname query. */
static int guest_from_native(const char *native,uint16_t *out,size_t capacity) {
    char guest[1024];
    const char *suffix;char drive;
    if(prefix_matches(native,prefix)){suffix=native+wr_len(prefix);drive='C';}
    else if(prefix_matches(native,application)){suffix=native+wr_len(application);drive='A';}
    else return -1;
    guest[0]=drive;guest[1]=':';guest[2]='\\';unsigned n=3;
    while(*suffix=='/')suffix++;
    while(*suffix){if(n+1>=sizeof(guest))return -1;guest[n++]=*suffix=='/'?'\\':*suffix;suffix++;}
    guest[n]=0;return wr_utf16(guest,out,capacity);
}
int wr_path_guest(const uint16_t *source,uint16_t *out,size_t capacity){
    char native[1024];if(wr_path_native(source,native,sizeof(native),0))return -1;return guest_from_native(native,out,capacity);
}
int wr_namespace_project_path(const char *path,uint16_t *out,size_t capacity){return guest_from_native(path,out,capacity);}
static int directory_locked(const uint16_t *source,unsigned rights) {
    char native[1024];if(path_native_locked(source,native,sizeof(native),0))return -13;
    int root;size_t length;
    if(prefix_matches(native,prefix)){root=c_root;length=wr_len(prefix);}
    else if(prefix_matches(native,application)){root=a_root;length=wr_len(application);}
    else return -13;
    const char *relative=native+length;while(*relative=='/')relative++;
    /* A derived root pins the directory against rename/removal. Enumeration
     * and child metadata therefore use one authority/anchor throughout. */
    return native_root(VEX_FS_ROOT_DERIVE,root,rights,relative,0,0,0);
}
int wr_namespace_directory(const uint16_t *source){wr_lock(&cwd_gate);int fd=directory_locked(source,VEX_FS_READ|VEX_FS_ENUM);wr_unlock(&cwd_gate);return fd;}
static int cwd_resource_locked(const uint16_t *source,uint16_t *canonical,size_t capacity){
    uint16_t current[512];if(!source){if(wr_utf16(guest_cwd,current,512)<0)return -36;source=current;}
    int fd=directory_locked(source,VEX_FS_READ|VEX_FS_ENUM|VEX_FS_TRANSFER);if(fd<0)return fd;
    vex_fs_root_info_t info;int result=native_root(VEX_FS_ROOT_QUERY,fd,0,0,0,0,&info);
    const char *suffix=0;char text[1024];
    if(!result){if(prefix_matches(info.path,prefix)){text[0]='C';suffix=info.path+wr_len(prefix);}
        else if(prefix_matches(info.path,application)){text[0]='A';suffix=info.path+wr_len(application);}else result=-13;}
    if(!result){text[1]=':';text[2]='\\';unsigned at=3;while(*suffix=='/')suffix++;
        while(*suffix){if(at+1>=sizeof(text)){result=-36;break;}text[at++]=*suffix=='/'?'\\':*suffix;suffix++;}text[at]=0;
        if(!result && wr_utf16(text,canonical,capacity)<0)result=-36;}
    if(result){vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);return result;}return fd;
}
int wr_cwd_resource(const uint16_t *source,uint16_t *canonical,size_t capacity){
    wr_lock(&cwd_gate);int fd=cwd_resource_locked(source,canonical,capacity);wr_unlock(&cwd_gate);return fd;
}
int wr_namespace_open_native(const char *path,unsigned flags) {
    const char *system="/usr/lib/kyil-windows";
    if(prefix_matches(path,system)) {const char *relative=path+wr_len(system);while(*relative=='/')relative++;return native_root(VEX_FS_ROOT_OPEN,system_root,0,relative,flags,0,0);}
    int fd;size_t n=wr_len(prefix);
    if(prefix_matches(path,prefix))fd=c_root;
    else {n=wr_len(application);if(!prefix_matches(path,application))return -13;fd=a_root;}
    const char *relative=path+n;while(*relative=='/')relative++;
    return native_root(VEX_FS_ROOT_OPEN,fd,0,relative,flags,0,0);
}
int wr_namespace_adopt(int c,int a,const uint16_t *image,const uint16_t *cwd) {
    vex_fs_root_info_t ci,ai;vex_user_identity_t id={.size=sizeof(id)};
    if(native_root(VEX_FS_ROOT_QUERY,c,0,0,0,0,&ci) || native_root(VEX_FS_ROOT_QUERY,a,0,0,0,0,&ai) ||
       vex_syscall1(VEX_SYS_USER_IDENTITY,(uintptr_t)&id) ||
       (ci.rights&(VEX_FS_READ|VEX_FS_WRITE|VEX_FS_CREATE|VEX_FS_ENUM))!=(VEX_FS_READ|VEX_FS_WRITE|VEX_FS_CREATE|VEX_FS_ENUM) ||
       (ai.rights&(VEX_FS_READ|VEX_FS_ENUM))!=(VEX_FS_READ|VEX_FS_ENUM))return -1;
    memcpy(prefix,ci.path,wr_len(ci.path)+1);memcpy(application,ai.path,wr_len(ai.path)+1);memcpy(username,id.name,sizeof(username));
    c_root=c;a_root=a;
    system_root=9;vex_fs_root_info_t si;if(native_root(VEX_FS_ROOT_QUERY,system_root,0,0,0,0,&si) || !wr_equal(si.path,"/usr/lib/kyil-windows",0))return -1;
    if(from_utf16(image,guest_image,sizeof(guest_image)))return -1;
    memcpy(guest_cwd,"C:\\Users\\",10);if(append(guest_cwd,sizeof(guest_cwd),username))return -1;
    memcpy(drive_cwd[0],guest_cwd,wr_len(guest_cwd)+1);memcpy(drive_cwd[1],"A:\\",4);
    /* New bootstraps transfer a directory anchor, so it cannot be renamed
       away between parent validation and child environment initialization. */
    if(set_cwd_locked(cwd,wr_boot && (wr_boot->reserved&1)?14:-1))return -1;
    return native_root(VEX_FS_ROOT_SEAL,0,0,0,0,0,0);
}
int wr_namespace_set_cwd(const uint16_t *cwd) {
    wr_lock(&cwd_gate);int result=set_cwd_locked(cwd,-1);wr_unlock(&cwd_gate);return result;
}
static int set_cwd_locked(const uint16_t *cwd,int adopted){
    uint16_t first;if(!cwd || wr_memory_copy(&first,cwd,2) || !first)return -22;
    uint16_t canonical[512];char text[512];int fd=adopted;
    if(fd<0)fd=cwd_resource_locked(cwd,canonical,512);
    else{
        vex_fs_root_info_t info;char expected[1024];
        if(path_native_locked(cwd,expected,sizeof(expected),0) || native_root(VEX_FS_ROOT_QUERY,fd,0,0,0,0,&info) ||
            !wr_equal(info.path,expected,0) || (info.rights&(VEX_FS_READ|VEX_FS_ENUM))!=(VEX_FS_READ|VEX_FS_ENUM))return -13;
        size_t n=0;while(cwd[n]){if(n==511)return -36;canonical[n]=cwd[n];n++;}canonical[n]=0;
    }
    if(fd<0)return fd;
    if(from_utf16(canonical,text,sizeof(text))){if(adopted<0)vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);return -36;}
    unsigned drive=text[0]=='A';int old=cwd_anchor[drive];cwd_anchor[drive]=fd;current_drive=drive;
    memcpy(drive_cwd[drive],text,wr_len(text)+1);memcpy(guest_cwd,text,wr_len(text)+1);wr_parameters_cwd(text);
    if(old>=0)vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)old);return 0;
}
static int namespace_call_locked(unsigned operation,const uint16_t *name,unsigned flags,unsigned mode,void *out,int writing) {
    char path[1024];if(path_native_locked(name,path,sizeof(path),writing))return -13;
    int fd;size_t n;
    n=wr_len(prefix);
    if(prefix_matches(path,prefix))fd=c_root;
    else {n=wr_len(application);if(!prefix_matches(path,application))return -13;fd=a_root;}
    const char *relative=path+n;while(*relative=='/')relative++;
    const char *system="Windows/System32/";
    size_t sys=wr_len(system);int match=1;for(size_t i=0;i<sys;i++){if(!relative[i]){match=0;break;}unsigned a=(unsigned char)relative[i],b=(unsigned char)system[i];if(a>='A' && a<='Z')a+=32;if(b>='A' && b<='Z')b+=32;if(a!=b){match=0;break;}}
    if(fd==c_root && match && wr_native_module_name(relative+sys)) {
        if(operation==VEX_FS_ROOT_OPEN && (flags&VEX_FS_OPEN_PROTECTED))return protected_open(system_root,relative+sys,flags);
        return native_root(operation,system_root,0,relative+sys,flags,mode,out);
    }
    if(operation==VEX_FS_ROOT_OPEN && (flags&VEX_FS_OPEN_PROTECTED))return protected_open(fd,relative,flags);
    return native_root(operation,fd,0,relative,flags,mode,out);
}
int wr_namespace_call(unsigned operation,const uint16_t *name,unsigned flags,unsigned mode,void *out,int writing){
    wr_lock(&cwd_gate);int result=namespace_call_locked(operation,name,flags,mode,out,writing);wr_unlock(&cwd_gate);return result;
}
int wr_namespace_move(const uint16_t *source,const uint16_t *destination,const uint16_t *backup,int replace){
    char paths[3][1024];const uint16_t *input[3]={source,destination,backup};int result=0;
    wr_lock(&cwd_gate);
    for(unsigned i=0;i<3;i++)if(input[i]){
        if(path_native_locked(input[i],paths[i],sizeof(paths[i]),1) || !prefix_matches(paths[i],prefix)){result=-13;break;}
    }
    if(!source || !destination)result=-22;
    if(!result){size_t n=wr_len(prefix);const char *relative[3]={0};
        for(unsigned i=0;i<3;i++)if(input[i]){relative[i]=paths[i]+n;while(*relative[i]=='/')relative[i]++;}
        vex_fs_root_names_t names={relative[1],relative[2]};
        result=native_root(replace?VEX_FS_ROOT_REPLACE:VEX_FS_ROOT_MOVE,c_root,0,relative[0],0,0,&names);
    }
    wr_unlock(&cwd_gate);return result;
}
int wr_namespace_image(const uint16_t *source,uint16_t *guest,size_t units,char *native,size_t bytes){
    char path[1024];uint16_t canonical[512];int result;
    wr_lock(&cwd_gate);
    if(path_native_locked(source,path,sizeof(path),0))result=-13;
    else{int n=guest_from_native(path,canonical,512);
        if(n<0 || (guest && (size_t)n>=units) || (native && wr_len(path)>=bytes))result=-36;
        else{result=namespace_call_locked(VEX_FS_ROOT_OPEN,canonical,O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE,0,0,0);
            if(result>=0){if(guest)memcpy(guest,canonical,(n+1)*2);if(native)memcpy(native,path,wr_len(path)+1);}}
    }
    wr_unlock(&cwd_gate);return result;
}
static unsigned copy_w(const char *s,uint16_t *out,unsigned size) {
    uint16_t text[512];int n=wr_utf16(s,text,512);if(n<0) {wr_current_teb()->last_error=87;return 0;}
    if((unsigned)n>=size)return (unsigned)n+1;
    if(wr_memory_copy(out,text,((size_t)n+1)*2)){wr_current_teb()->last_error=998;return 0;}return (unsigned)n;
}
static unsigned WR_WIN GetSystemDirectoryW(uint16_t *out,unsigned size) {return copy_w("C:\\Windows\\System32",out,size);}
int wr_profile_folder(unsigned id,uint16_t *out,size_t capacity){
    const char *suffix=NULL,*fixed=NULL;switch(id){
    case 0:case 0x10:suffix="\\Desktop";break;case 5:suffix="\\Documents";break;
    case 0x1a:suffix="\\AppData\\Roaming";break;case 0x1c:suffix="\\AppData\\Local";break;
    case 0x28:suffix="";break;case 0x24:fixed="C:\\Windows";break;
    case 0x25:fixed="C:\\Windows\\System32";break;default:return 50;}
    char path[512]="C:\\Users\\";
    if(fixed)memcpy(path,fixed,wr_len(fixed)+1);else if(append(path,sizeof(path),username) || append(path,sizeof(path),suffix))return 206;
    return wr_utf16(path,out,capacity)<0?206:0;
}
static unsigned WR_WIN GetWindowsDirectoryW(uint16_t *out,unsigned size) {return copy_w("C:\\Windows",out,size);}
static unsigned WR_WIN GetCurrentDirectoryW(unsigned size,uint16_t *out) {wr_lock(&cwd_gate);unsigned n=copy_w(guest_cwd,out,size);wr_unlock(&cwd_gate);return n;}
static int WR_WIN SetCurrentDirectoryW(const uint16_t *path){int e=wr_namespace_set_cwd(path);if(e)wr_current_teb()->last_error=e==-20?267:wr_file_error(-e);return !e;}
static unsigned WR_WIN GetTempPathW(unsigned size,uint16_t *out) {
    char path[128]="C:\\Users\\";append(path,sizeof(path),username);append(path,sizeof(path),"\\Temp\\");return copy_w(path,out,size);
}
static int WR_WIN CreateDirectoryW(const uint16_t *name,void *security) {
    if(security) {wr_current_teb()->last_error=50;return 0;}
    int result=wr_namespace_call(VEX_FS_ROOT_MKDIR,name,0,0700,0,1);
    static unsigned traced;if(__atomic_fetch_add(&traced,1,__ATOMIC_RELAXED)<48){char path[1024];
        if(!wr_utf8(name,path,sizeof(path))){wr_log("windows-directory: ");wr_log(path);wr_log("\n");}
        wr_trace_contract("CreateDirectoryW: native-result security reserved site",(uintptr_t)(intptr_t)result,(uintptr_t)security,0,(uintptr_t)__builtin_return_address(0));}
    if(!result)return 1;
    unsigned error=(unsigned)-result;
    wr_current_teb()->last_error=error==17?183:(error==2?3:(error==12?8:5));return 0;
}
void *wr_path_exports(const char *name) {
    void *search=wr_search_exports(name);if(search)return search;
    void *metadata=wr_path_metadata_exports(name);if(metadata)return metadata;
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(GetSystemDirectoryW);E(GetWindowsDirectoryW);E(GetCurrentDirectoryW);E(SetCurrentDirectoryW);E(GetTempPathW);E(CreateDirectoryW);
#undef E
    return 0;
}
