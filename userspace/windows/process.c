/* Native executable/capability launch; all Windows policy stays userspace. */
#include "process.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "job.h"
wr_bootstrap *wr_boot;
static int runtime_fd=-1;
static struct {int fd;uint32_t generation,kind,flags,access;} handles[256];
static wr_mutex gate;
typedef struct {uint32_t size,pad;void *descriptor;int inherit,pad2;} security_attributes;
typedef struct {uint64_t magic;unsigned capacity,count;const void *handles;size_t bytes;} attribute_list;
#define ATTRIBUTE_MAGIC 0x5750415454524942ull
static wr_mutex attribute_gate;
static int native(vex_process_resource_req_t *r){r->size=sizeof(*r);return (int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)r);}
static void close_fd(int fd){if(fd>=0)vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);}
static unsigned failure(int e) {
    switch(-e){case 2:return 2;case 8:return 193;case 9:return 6;case 12:case 24:return 8;
    case 13:case 1:return 5;case 14:return 998;case 22:return 87;case 110:return 258;case 3:return 6;default:return 50;}
}
static int index(void *h) {
    uintptr_t v=(uintptr_t)h;unsigned i=(v>>2)&255;
    if(wr_handle_tag(v)!=0x54 || (v&3) || wr_handle_generation(v)!=handles[i].generation || !handles[i].kind)return -1;
    return (int)i;
}
static void *install_access(int fd,unsigned kind,unsigned flags,unsigned access) {
    wr_lock(&gate);unsigned i=0;while(i<256 && (handles[i].kind || handles[i].generation>=WR_HANDLE_GENERATION_MAX))i++;
    if(i==256){wr_unlock(&gate);return 0;}
    if(!++handles[i].generation)++handles[i].generation;
    handles[i].fd=fd;handles[i].kind=kind;handles[i].flags=flags;handles[i].access=access;
    void *h=(void *)(wr_handle_pack(0x54,handles[i].generation,i));wr_unlock(&gate);return h;
}
static void *install(int fd,unsigned kind,unsigned flags){return install_access(fd,kind,flags,0x1fffff);}
static int acquire(void *h,unsigned expected,unsigned *kind) {
    if(h==(void *)-1 && expected!=2){vex_process_resource_req_t r={.operation=VEX_PROCESS_SELF};if(kind)*kind=1;return native(&r);}
    wr_lock(&gate);int i=index(h),fd=-9;
    if(i>=0 && (!expected || handles[i].kind==expected)){fd=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,handles[i].fd);if(kind)*kind=handles[i].kind;}
    wr_unlock(&gate);return fd;
}
int wr_process_capability(void *h){return acquire(h,1,0);}
int wr_process_self_access(void *h,unsigned access){
    if(h==(void *)-1)return 1;
    wr_lock(&gate);int i=index(h),fd=-1;unsigned mask=0;
    if(i>=0 && handles[i].kind==1){mask=handles[i].access;fd=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,handles[i].fd);}
    wr_unlock(&gate);if(fd<0){wr_current_teb()->last_error=6;return 0;}
    if((mask&access)!=access){close_fd(fd);wr_current_teb()->last_error=50;return 0;}
    vex_process_resource_info_t info;vex_process_resource_req_t q={.operation=VEX_PROCESS_QUERY,.fd=fd,.out=&info};int e=native(&q);close_fd(fd);
    if(e || info.pid!=wr_current_teb()->pid){wr_current_teb()->last_error=e?failure(e):50;return 0;}return 1;
}
unsigned wr_process_handle_kind(void *h){
    if(h==(void *)-1)return 1;
    wr_lock(&gate);int i=index(h);unsigned kind=i>=0?handles[i].kind:0;wr_unlock(&gate);return kind;
}
int wr_native_service(const char *mode,const vex_process_grant_t *extra,unsigned count) {
    if(count>8)return -22;
    vex_process_grant_t grants[12]={{runtime_fd,4,1,0},{wr_prefix_root(),5,1,0},{wr_application_root(),6,1,0},{wr_system_root(),9,1,0}};
    for(unsigned i=0;i<count;i++)grants[i+4]=extra[i];
    const char *argv[]={"/usr/bin/kyil-windows",mode,0};
    vex_process_resource_req_t r={.operation=VEX_PROCESS_LAUNCH,.fd=runtime_fd,.argv=argv,.grants=grants,.grant_count=count+4};return native(&r);
}
int wr_process_adopt(const wr_inherited_file *r) {
    uintptr_t value=r->handle;unsigned i=(value>>2)&255;
    if(wr_handle_tag(value)!=0x54 || (value&3) || r->fd<0 || !r->access || r->access>2 || handles[i].kind)return -1;
    vex_process_resource_info_t info;vex_process_resource_req_t q={.operation=VEX_PROCESS_QUERY,.fd=r->fd,.out=&info};if(native(&q))return -1;
    if(r->overlapped&~0x1fffffu)return -1;
    handles[i].fd=r->fd;handles[i].kind=r->access;handles[i].flags=r->flags;handles[i].access=r->overlapped;handles[i].generation=wr_handle_generation(value);return 0;
}
static int export_handle(void *h,wr_inherited_file *out,int child_slot,int require_inherit) {
    if(wr_handle_tag((uintptr_t)h)==0x59)return wr_object_export(h,out,child_slot);
    if(wr_handle_tag((uintptr_t)h)==0x57) {
        unsigned flags;if(require_inherit && (!wr_file_flags(h,0,0,&flags) || !(flags&1)))return -1;
        return wr_file_export(h,out,child_slot);
    }
    wr_lock(&gate);int i=index(h),fd=-1;
    if(i>=0 && (!require_inherit || (handles[i].flags&1))) {
        fd=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,handles[i].fd);
        if(fd>=0)*out=(wr_inherited_file){(uintptr_t)h,child_slot,handles[i].kind,handles[i].flags,handles[i].access};
    }wr_unlock(&gate);return fd;
}
static int collect_processes(wr_bootstrap *b,int *temporary,unsigned *count) {
    wr_lock(&gate);
    for(unsigned i=0;i<256;i++)if(handles[i].kind && (handles[i].flags&1)) {
        if(*count>=WR_BOOT_MAX_FILES-6){wr_unlock(&gate);return -1;}
        int fd=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,handles[i].fd);if(fd<0){wr_unlock(&gate);return -1;}
        uintptr_t h=wr_handle_pack(0x54,handles[i].generation,i);
        b->files[*count]=(wr_inherited_file){h,16+(int)*count,handles[i].kind,handles[i].flags,handles[i].access};temporary[(*count)++]=fd;
    }
    wr_unlock(&gate);return 0;
}
void wr_process_quiesce(int begin){if(begin)wr_lock(&gate);else wr_unlock(&gate);}
int wr_process_close(void *h) {
    if(wr_handle_tag((uintptr_t)h)!=0x54)return -1;
    wr_lock(&gate);int i=index(h),fd=-1;unsigned why=6;
    if(i>=0){if(handles[i].flags&2)why=5;else{fd=handles[i].fd;handles[i].kind=0;}}
    wr_unlock(&gate);if(fd<0){wr_current_teb()->last_error=why;return 0;}close_fd(fd);return 1;
}
static int query(void *h,unsigned kind,vex_process_resource_info_t *info) {
    if(kind==1 && h!=(void *)-1){wr_lock(&gate);int i=index(h);unsigned rights=i>=0?handles[i].access:0;wr_unlock(&gate);
        if(i<0)return -9;if(!(rights&(0x400u|0x1000u)))return -13;}
    int fd=acquire(h,kind,0);if(fd<0)return fd;
    vex_process_resource_req_t r={.operation=VEX_PROCESS_QUERY,.fd=fd,.out=info};int result=native(&r);close_fd(fd);return result;
}
int wr_process_wait(void *h,unsigned timeout,unsigned *result) {
    if(wr_handle_tag((uintptr_t)h)!=0x54 && h!=(void *)-1)return 0;
    unsigned kind;int fd=acquire(h,0,&kind);
    if(fd<0){wr_current_teb()->last_error=6;*result=UINT32_MAX;return 1;}
    /* Waiting is independently authorized from querying. Zero timeout checks
     * completion atomically under WAIT authority without creating a waiter. */
    vex_process_resource_req_t r={.operation=VEX_PROCESS_WAIT,.fd=fd,
        .flags=(kind==2?VEX_PROCESS_WAIT_THREAD:0)|(!timeout?VEX_PROCESS_WAIT_POLL:0),
        .timeout_ns=timeout==UINT32_MAX?0:(uint64_t)timeout*1000000};
    int e=native(&r);
    close_fd(fd);*result=e==-110?258:(e<0?UINT32_MAX:0);if(e<0 && e!=-110)wr_current_teb()->last_error=failure(e);return 1;
}
int wr_primary_resume(void *h,unsigned *result) {
    if(wr_handle_tag((uintptr_t)h)!=0x54)return 0;
    int fd=acquire(h,2,0);vex_process_resource_req_t r={.operation=VEX_PROCESS_RESUME,.fd=fd};int e=fd<0?fd:native(&r);close_fd(fd);
    *result=e<0?UINT32_MAX:(unsigned)e;if(e<0)wr_current_teb()->last_error=failure(e);return 1;
}
int wr_primary_exit_code(void *h,unsigned *code) {
    if(wr_handle_tag((uintptr_t)h)!=0x54)return -1;
    vex_process_resource_info_t info;int e=query(h,2,&info);if(!e)*code=info.primary_exited?(unsigned)info.primary_status:259;
    if(e)wr_current_teb()->last_error=failure(e);return !e;
}
int wr_primary_id(void *h,unsigned *tid) {
    if(wr_handle_tag((uintptr_t)h)!=0x54)return -1;
    vex_process_resource_info_t info;int e=query(h,2,&info);
    if(e)wr_current_teb()->last_error=failure(e);else *tid=info.tid;return !e;
}
int wr_process_init(void) {
    if(runtime_fd>=0)return 0;
    if(wr_boot){runtime_fd=4;return 0;}
    vex_fs_root_req_t r={.size=sizeof(r),.operation=VEX_FS_ROOT_CREATE,.rights=VEX_FS_READ|VEX_FS_EXEC|VEX_FS_TRANSFER,.path="/usr/bin"};
    int root=(int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&r);if(root<0)return -1;
    r=(vex_fs_root_req_t){.size=sizeof(r),.operation=VEX_FS_ROOT_OPEN,.fd=root,.path="kyil-windows",.flags=O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE};
    runtime_fd=(int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&r);close_fd(root);return runtime_fd<0?-1:0;
}
static unsigned wlen(const uint16_t *s,unsigned limit){unsigned n=0;while(n<limit && s[n])n++;return n;}
static int send_blob(int fd,const void *blob,size_t size) {
    const uint8_t *p=blob;while(size){size_t n=size<65536?size:65536;vex_message_io_req_t m={fd,0,(void *)p,n,0,0};
        if(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&m)!=n)return -1;p+=n;size-=n;}return 0;
}
int wr_child_bootstrap(int fd) {
    wr_boot=wr_native_alloc(sizeof(*wr_boot));if(!wr_boot)return -1;
    size_t at=0;while(at<sizeof(*wr_boot)){size_t n=sizeof(*wr_boot)-at;if(n>65536)n=65536;
        vex_message_io_req_t r={fd,0,(uint8_t *)wr_boot+at,n,0,0};int64_t got=(int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&r);
        if(got<=0 || (size_t)got!=n)return -1;at+=(size_t)got;}
    close_fd(fd);
    if(wr_boot->magic!=0x57505243 || wr_boot->size!=sizeof(*wr_boot) || (wr_boot->reserved&~1u) || wr_boot->job_reserved || wr_boot->job_member>1 || wr_boot->file_count>WR_BOOT_MAX_FILES ||
       !wr_boot->command_length || wr_boot->command_length>32767 || wr_boot->environment_length<2 || wr_boot->environment_length>32768 ||
       !wr_boot->image_length || wr_boot->image_length>512 || !wr_boot->cwd_length || wr_boot->cwd_length>512 ||
       wr_boot->image[wr_boot->image_length-1] || wr_boot->cwd[wr_boot->cwd_length-1] || wr_boot->command[wr_boot->command_length-1] ||
       wr_boot->environment[wr_boot->environment_length-1] || wr_boot->environment[wr_boot->environment_length-2])return -1;
    return wr_namespace_adopt(5,6,wr_boot->image,wr_boot->cwd);
}
/* Raw command line is preserved; only executable selection is parsed here. */
static int executable(const uint16_t *app,const uint16_t *command,uint16_t out[512]) {
    if(app){unsigned n=wlen(app,512);if(!n || n==512)return 87;memcpy(out,app,(n+1)*2);return 0;}
    if(!command)return 87;unsigned at=0,n=0;while(command[at]==' ' || command[at]=='\t')at++;
    unsigned quoted=command[at]=='"';if(quoted)at++;
    while(command[at] && (quoted?command[at]!='"':(command[at]!=' ' && command[at]!='\t'))) {
        if(n>=507)return 206;out[n++]=command[at++];
    }
    if(!n || (quoted && command[at]!='"'))return 87;out[n]=0;
    unsigned dot=0;for(unsigned i=0;i<n;i++)if(out[i]=='.')dot=1;
    if(!dot){out[n++]='.';out[n++]='e';out[n++]='x';out[n++]='e';out[n]=0;}return 0;
}
static int environment(const void *source,unsigned unicode,wr_bootstrap *b) {
    extern const uint16_t *wr_environment_block(void);
    if(!source){int n=wr_environment_snapshot(b->environment,32768);if(n<0)return 8;b->environment_length=(unsigned)n;return 0;}
    if(unicode){const uint16_t *s=source;unsigned n=0;while(n+1<32768){b->environment[n]=s[n];if(n && !s[n] && !s[n-1]){b->environment_length=n+1;return 0;}n++;}return 203;}
    const char *s=source;unsigned at=0;
    while(*s){int n=wr_utf16(s,b->environment+at,32768-at);if(n<0)return 1113;at+=(unsigned)n+1;s+=wr_len(s)+1;if(at>=32767)return 8;}
    if(!at)b->environment[at++]=0;b->environment[at++]=0;b->environment_length=at;return 0;
}
static int WR_WIN CreateProcessW(const uint16_t *app,uint16_t *command,void *process_security,void *thread_security,int inherit,
    unsigned flags,void *env,const uint16_t *cwd,wr_startup *startup,wr_process_info *info) {
    if(info)memset(info,0,sizeof(*info));
    if(!startup || !info || startup->size!=sizeof(*startup)+((flags&0x80000u)?8:0)){wr_current_teb()->last_error=87;return 0;}
    unsigned process_flags=0,thread_flags=0;
    if(process_security){security_attributes *a=process_security;if(a->size!=sizeof(*a) || a->descriptor){wr_current_teb()->last_error=50;return 0;}process_flags=a->inherit!=0;}
    if(thread_security){security_attributes *a=thread_security;if(a->size!=sizeof(*a) || a->descriptor){wr_current_teb()->last_error=50;return 0;}thread_flags=a->inherit!=0;}
    /* FORCEOFFFEEDBACK is honored: native launch has no Windows startup
       feedback cursor. Preserve it in the child's real STARTUPINFO. */
    if((flags&~0xc0004u) || (startup->flags&~0x180u) || startup->reserved || startup->desktop || startup->title || startup->reserved_bytes || startup->reserved2) {
        wr_trace_contract("CreateProcessW guard: flags startup extras site",flags,startup->flags,
            !!startup->reserved|((uintptr_t)!!startup->desktop<<1)|((uintptr_t)!!startup->title<<2)|((uintptr_t)!!startup->reserved_bytes<<3)|((uintptr_t)!!startup->reserved2<<4),(uintptr_t)__builtin_return_address(0));
        wr_current_teb()->last_error=50;return 0;
    }
    wr_bootstrap *b=wr_native_alloc(sizeof(*b));if(!b){wr_current_teb()->last_error=8;return 0;}
    memset(b,0,sizeof(*b));b->magic=0x57505243;b->size=sizeof(*b);b->startup=*startup;b->startup.size=sizeof(b->startup);
    unsigned why=executable(app,command,b->image);int image=-1,channel[2]={-1,-1},process=-1,thread=-1,job_fd=-1,job_held=0,directory=-1;
    int temporary[WR_BOOT_MAX_FILES];for(unsigned i=0;i<WR_BOOT_MAX_FILES;i++)temporary[i]=-1;
    void *mapped=0;size_t mapped_size=0;
    if(why)goto fail;
    image=wr_namespace_image(b->image,b->image,512,0,0);
    if(image<0){why=failure(image);goto fail;}
    pe_image parsed;const char *error;
    if(wr_read_fd(image,(uint8_t **)&mapped,&mapped_size) || (error=pe_parse(mapped,mapped_size,&parsed)) || parsed.dll){why=193;goto fail;}
    wr_unmap(mapped,mapped_size);mapped=0;
    const uint16_t *cmd=command?command:b->image;unsigned length=wlen(cmd,32767);if(length>=32767){why=206;goto fail;}
    b->command_length=length+1;memcpy(b->command,cmd,(length+1)*2);b->image_length=wlen(b->image,512)+1;
    directory=wr_cwd_resource(cwd,b->cwd,512);if(directory<0){why=directory==-20?267:failure(directory);goto fail;}
    b->cwd_length=wlen(b->cwd,512)+1;b->reserved=1;
    if((why=environment(env,(flags&0x40000u)!=0,b)))goto fail;
    unsigned count=0,selected_only=0;
    if(flags&0x80000u) {
        attribute_list *list=*(attribute_list **)((uint8_t *)startup+sizeof(*startup));wr_lock(&attribute_gate);
        if(!list || list->magic!=ATTRIBUTE_MAGIC || (list->count && !inherit)){wr_unlock(&attribute_gate);why=87;goto fail;}
        if(list->count) {
            selected_only=1;
            const void *const *selected=list->handles;size_t n=list->bytes/sizeof(void *);
            for(size_t i=0;i<n;i++) {
                if(count>=WR_BOOT_MAX_FILES-6){why=8;break;}
                unsigned duplicate=0;for(unsigned j=0;j<count;j++)if(b->files[j].handle==(uintptr_t)selected[i])duplicate=1;
                if(duplicate)continue;
                if(wr_handle_tag((uintptr_t)selected[i])==0x55){unsigned handle_flags=0,start=count;
                    if(!wr_pipe_flags((void *)selected[i],0,0,&handle_flags) || !(handle_flags&1) || wr_pipe_export((void *)selected[i],b->files,&count)){why=50;for(unsigned j=start;j<count;j++)temporary[j]=b->files[j].fd;break;}
                    for(unsigned j=start;j<count;j++){temporary[j]=b->files[j].fd;b->files[j].fd=16+(int)j;}continue;}
                int fd=export_handle((void *)selected[i],&b->files[count],16+(int)count,1);
                if(fd<0){wr_trace_contract("CreateProcessW grant: index handle type site",i,(uintptr_t)selected[i],wr_handle_tag((uintptr_t)selected[i]),(uintptr_t)__builtin_return_address(0));why=6;break;}temporary[count++]=fd;
            }
        }
        wr_unlock(&attribute_gate);b->file_count=count;if(why)goto fail;
    }
    if(!selected_only && inherit) {
        if(wr_files_inherit(b->files,&count,16)){why=50;b->file_count=count;for(unsigned i=0;i<count;i++)temporary[i]=b->files[i].fd;goto fail;}
        if(wr_objects_inherit(b->files,&count)){why=50;b->file_count=count;for(unsigned i=0;i<count;i++)temporary[i]=b->files[i].fd;goto fail;}
        if(wr_pipes_inherit(b->files,&count)){why=50;b->file_count=count;for(unsigned i=0;i<count;i++)temporary[i]=b->files[i].fd;goto fail;}
        b->file_count=count;for(unsigned i=0;i<count;i++){temporary[i]=b->files[i].fd;b->files[i].fd=16+(int)i;}
        if(collect_processes(b,temporary,&count)){b->file_count=count;why=8;goto fail;}b->file_count=count;
    }
    void *std[3]={startup->input,startup->output,startup->error};
    if(!(startup->flags&0x100)){for(unsigned i=0;i<3;i++)std[i]=wr_file_stdio(i);}
    else if(!inherit){why=87;goto fail;}
    for(unsigned i=0;i<3;i++) {
        unsigned j=0;while(j<count && b->files[j].handle!=(uintptr_t)std[i])j++;
        if(j==count && selected_only) {
            /* An explicit capability selection is never widened by stdio. */
            if(startup->flags&0x100){unsigned console_access=0;int console=wr_file_console_kind(std[i],&console_access);wr_trace_contract("CreateProcessW std: index handle console site",i,(uintptr_t)std[i],console,(uintptr_t)__builtin_return_address(0));why=50;goto fail;}
            std[i]=0;continue;
        }
        if(j==count){if(count>=WR_BOOT_MAX_FILES-6){why=8;goto fail;}
            int fd=wr_file_export(std[i],&b->files[count],16+(int)count);if(fd<0){why=6;goto fail;}temporary[count++]=fd;b->file_count=count;}
    }
    b->file_count=count;b->startup.input=std[0];b->startup.output=std[1];b->startup.error=std[2];
    job_fd=wr_job_launch_begin();job_held=1;
    if(job_fd< -1 || count>WR_BOOT_MAX_FILES-7-(job_fd>=0)){why=8;goto fail;}
    b->job_member=job_fd>=0;
    vex_message_pair_req_t pair={channel,0,0};if(vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair)){why=8;goto fail;}
    if(send_blob(channel[0],b,sizeof(*b))){why=8;goto fail;}
    vex_process_grant_t grants[WR_BOOT_MAX_FILES+8]={{runtime_fd,4,1,0},{wr_prefix_root(),5,1,0},{wr_application_root(),6,1,0},{image,7,1,0},{channel[1],8,1,0},{wr_system_root(),9,1,0},{directory,14,1,0}};
    for(unsigned i=0;i<count;i++)grants[i+7]=(vex_process_grant_t){temporary[i],b->files[i].fd,1,0};
    if(job_fd>=0)grants[count+7]=(vex_process_grant_t){job_fd,13,1,0};
    const char *args[]={"/usr/bin/kyil-windows","--native-child","8",0};
    vex_process_resource_req_t r={.operation=VEX_PROCESS_LAUNCH,.fd=runtime_fd,.argv=args,.grants=grants,.grant_count=count+7+(job_fd>=0),
        .flags=((flags&4) || job_fd>=0)?VEX_PROCESS_START_SUSPENDED:0};
    process=native(&r);int job_error=0;
    if(process>=0 && job_fd>=0){job_error=wr_job_add_child(process);
        if(!job_error && !(flags&4)){r=(vex_process_resource_req_t){.operation=VEX_PROCESS_RESUME,.fd=process};int previous=native(&r);if(previous<0)job_error=failure(previous);}}
    wr_job_launch_end();job_held=0;close_fd(job_fd);job_fd=-1;
    if(process<0 || job_error){why=job_error?job_error:failure(process);goto fail;}
    thread=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,process);if(thread<0){why=8;goto fail;}
    vex_process_resource_info_t state;r=(vex_process_resource_req_t){.operation=VEX_PROCESS_QUERY,.fd=process,.out=&state};
    if(native(&r)){why=6;goto fail;}
    info->process=install(process,1,process_flags);if(info->process)process=-1;
    info->thread=install(thread,2,thread_flags);if(info->thread)thread=-1;
    if(!info->process || !info->thread){why=8;goto fail;}
    info->pid=state.pid;info->tid=state.tid;
    for(unsigned i=0;i<count;i++)close_fd(temporary[i]);close_fd(image);close_fd(directory);close_fd(channel[0]);close_fd(channel[1]);wr_native_free(b);return 1;
fail:
    wr_trace_contract("CreateProcessW: error flags inherit site",why,flags,inherit,(uintptr_t)__builtin_return_address(0));
    if(job_held)wr_job_launch_end();close_fd(job_fd);
    if(mapped)wr_unmap(mapped,mapped_size);
    int control=process>=0?process:(info->process?acquire(info->process,1,0):-1);
    if(control>=0){vex_process_resource_req_t stop={.operation=VEX_PROCESS_TERMINATE,.fd=control,.status=(int)0xc0000142u};native(&stop);if(control!=process)close_fd(control);}
    if(info->process)wr_process_close(info->process);if(info->thread)wr_process_close(info->thread);
    close_fd(process);close_fd(thread);close_fd(image);close_fd(directory);close_fd(channel[0]);close_fd(channel[1]);
    for(unsigned i=0;i<b->file_count;i++)close_fd(temporary[i]);wr_native_free(b);memset(info,0,sizeof(*info));wr_current_teb()->last_error=why;return 0;
}
static void *WR_WIN GetCurrentProcess(void){return (void *)-1;}
static int WR_WIN CreateProcessA(const char *app,char *command,void *process_security,void *thread_security,int inherit,
    unsigned flags,void *env,const char *cwd,wr_startup *startup,wr_process_info *info) {
    uint16_t image[1024],directory[1024];uint16_t *line=0;int result=0;
    if(info)memset(info,0,sizeof(*info));
    if((app && wr_utf16(app,image,1024)<0) || (cwd && wr_utf16(cwd,directory,1024)<0)) {
        wr_current_teb()->last_error=1113;return 0;
    }
    if(command) {
        line=wr_native_alloc(32768*2);if(!line){wr_current_teb()->last_error=8;return 0;}
        if(wr_utf16(command,line,32768)<0){wr_current_teb()->last_error=1113;wr_native_free(line);return 0;}
    }
    /* ACP is UTF-8; the environment format still follows the explicit flag. */
    result=CreateProcessW(app?image:0,line,process_security,thread_security,inherit,flags,env,cwd?directory:0,startup,info);
    unsigned error=wr_current_teb()->last_error;wr_native_free(line);wr_current_teb()->last_error=error;return result;
}
static void *WR_WIN GetCurrentThread(void){return (void *)-2;}
static void *WR_WIN CreateRemoteThread(void *process,void *security,uint64_t stack,void *entry,void *arg,unsigned flags,unsigned *tid) {
    if(process!=(void *)-1){wr_current_teb()->last_error=50;return 0;}
    void *(WR_WIN *start)(void *,uint64_t,void *,void *,unsigned,unsigned *)=wr_thread_exports("CreateThread");
    return start(security,stack,entry,arg,flags,tid);
}
static unsigned WR_WIN GetProcessId(void *h){vex_process_resource_info_t info;int e=query(h,1,&info);if(e)wr_current_teb()->last_error=failure(e);return e?0:info.pid;}
static int WR_WIN GetProcessTimes(void *process,uint64_t *birth,uint64_t *end,uint64_t *kernel,uint64_t *user){
    int fd=wr_process_capability(process);if(fd<0){wr_current_teb()->last_error=6;return 0;}
    vex_process_usage_t usage;vex_process_resource_req_t r={.size=sizeof(r),.operation=VEX_PROCESS_USAGE,.fd=fd,.out=&usage};
    int error=(int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)&r);vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);
    if(error){wr_current_teb()->last_error=error==-13?5:(error==-95?50:6);return 0;}
    uint64_t created=usage.birth_ns/100+116444736000000000ull,exited=usage.flags&2?usage.exit_ns/100+116444736000000000ull:0;
    uint64_t k=usage.kernel_ns/100,u=usage.user_ns/100;
    if(wr_memory_copy(birth,&created,8) || wr_memory_copy(end,&exited,8) || wr_memory_copy(kernel,&k,8) || wr_memory_copy(user,&u,8)){wr_current_teb()->last_error=998;return 0;}return 1;
}
static int WR_WIN SetProcessShutdownParameters(unsigned level,unsigned flags){
    if(level>0x4ff || (flags&~1u)){wr_current_teb()->last_error=87;return 0;}
    if(flags){wr_current_teb()->last_error=50;return 0;} /* no retry-dialog contract */
    int fd=wr_process_capability((void *)-1);if(fd<0){wr_current_teb()->last_error=6;return 0;}
    vex_process_resource_req_t r={.size=sizeof(r),.operation=VEX_PROCESS_EXIT_ORDER,.fd=fd,.timeout_ns=(uint64_t)(level+0x580u)<<20};
    int e=(int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)&r);close_fd(fd);
    if(e)wr_current_teb()->last_error=failure(e);return !e;
}
static int WR_WIN GetProcessShutdownParameters(unsigned *level,unsigned *flags){
    int fd=wr_process_capability((void *)-1);if(fd<0){wr_current_teb()->last_error=6;return 0;}
    vex_process_resource_req_t r={.size=sizeof(r),.operation=VEX_PROCESS_EXIT_ORDER,.fd=fd,.flags=1};
    int64_t rank=(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)&r);close_fd(fd);
    if(rank<0){wr_current_teb()->last_error=failure((int)rank);return 0;}
    if((rank&0xfffff) || (rank>>20)<0x580 || (rank>>20)>0xa7f){wr_current_teb()->last_error=50;return 0;}
    unsigned value=(unsigned)(rank>>20)-0x580u,zero=0;
    if(wr_memory_copy(level,&value,4) || wr_memory_copy(flags,&zero,4)){wr_current_teb()->last_error=998;return 0;}return 1;
}
static int WR_WIN GetProcessHandleCount(void *process,unsigned *out){
    vex_process_resource_info_t info;int error=query(process,1,&info);
    if(error){wr_current_teb()->last_error=failure(error);return 0;}
    if(info.pid!=wr_current_teb()->pid){wr_current_teb()->last_error=50;return 0;}
    /* Windows-visible object handles, not hidden native transport/root FDs.
       Pseudohandles, heap objects and wait-registration tokens are not entries
       in the Windows object handle table. Each provider counts live slots,
       excluding references retained after a public handle has been closed.
       Concurrent opens/closes may naturally change the sampled total. */
    unsigned count=wr_thread_handle_count()+wr_object_handle_count()+wr_file_handle_count()+wr_pipe_handle_count()+
        wr_completion_handle_count()+wr_mapping_handle_count()+wr_registry_handle_count()+wr_job_handle_count()+wr_wer_handle_count();
    wr_lock(&gate);for(unsigned i=0;i<256;i++)if(handles[i].kind)count++;wr_unlock(&gate);
    if(wr_memory_copy(out,&count,4)){wr_current_teb()->last_error=998;return 0;}return 1;
}
static void *WR_WIN OpenProcess(unsigned access,int inherit,unsigned pid) {
    const unsigned allowed=0x1fffffu;
    if(!pid || !access){wr_current_teb()->last_error=87;return 0;}
    if(access&~allowed){static unsigned traced;
        if(__atomic_fetch_add(&traced,1,__ATOMIC_RELAXED)<8){wr_trace_contract("OpenProcess: access inherit pid site",access,inherit,pid,(uintptr_t)__builtin_return_address(0));}
        wr_current_teb()->last_error=50;return 0;}
    /* A PID is a selector, never authority. Only self or an already possessed
     * process capability can be opened, and its rights are attenuated. */
    unsigned rights=VEX_PROCESS_RIGHT_TRANSFER;
    if(access&1)rights|=VEX_PROCESS_RIGHT_TERMINATE;
    if(access&(0x400u|0x1000u|0x38u))rights|=VEX_PROCESS_RIGHT_QUERY;
    if(access&0x100000u)rights|=VEX_PROCESS_RIGHT_WAIT;
    if(access&0x800u)rights|=VEX_PROCESS_RIGHT_RESUME;
    int fd=-1;
    if(pid==wr_current_teb()->pid) {
        vex_process_resource_req_t self={.operation=VEX_PROCESS_SELF};int source=native(&self);
        if(source>=0){vex_process_resource_req_t view={.operation=VEX_PROCESS_VIEW,.fd=source,.rights=rights};fd=native(&view);close_fd(source);}
    }
    else {
        wr_lock(&gate);
        for(unsigned i=0;i<256;i++)if(handles[i].kind==1) {
            vex_process_resource_info_t info;vex_process_resource_req_t q={.operation=VEX_PROCESS_QUERY,.fd=handles[i].fd,.out=&info};
            if(!native(&q) && info.pid==pid) {
                vex_process_resource_req_t view={.operation=VEX_PROCESS_VIEW,.fd=handles[i].fd,.rights=rights};fd=native(&view);
                if(fd>=0)break; /* A weaker view must not hide another grant. */
            }
        }
        wr_unlock(&gate);
    }
    if(fd<0){wr_current_teb()->last_error=failure(fd);return 0;}
    /* Retain the requested Windows access mask, including across inheritance.
       Opening self does not imply that foreign VM/thread/token operations
       exist: those APIs must still enforce and implement their own contract. */
    void *h=install_access(fd,1,inherit?1:0,access);if(!h){close_fd(fd);wr_current_teb()->last_error=8;}return h;
}
static int WR_WIN GetExitCodeProcess(void *h,unsigned *code){if(!code){wr_current_teb()->last_error=87;return 0;}vex_process_resource_info_t i;int e=query(h,1,&i);if(e)wr_current_teb()->last_error=failure(e);else *code=i.exited?(unsigned)i.status:259;return !e;}
static int WR_WIN TerminateProcess(void *h,unsigned status){if(h==(void *)-1)wr_exit(status);int fd=acquire(h,1,0);vex_process_resource_req_t r={.operation=VEX_PROCESS_TERMINATE,.fd=fd,.status=(int)status};int e=fd<0?fd:native(&r);close_fd(fd);if(e)wr_current_teb()->last_error=failure(e);return !e;}
static void WR_WIN GetStartupInfoW(wr_startup *out){if(wr_boot)*out=wr_boot->startup;else{memset(out,0,sizeof(*out));out->size=sizeof(*out);out->input=wr_file_stdio(0);out->output=wr_file_stdio(1);out->error=wr_file_stdio(2);}}
static int WR_WIN GetHandleInformation(void *h,unsigned *flags) {
    if(!flags){wr_current_teb()->last_error=87;return 0;}int job=wr_job_flags(h,0,0,flags);if(job>=0)return job;
    int object=wr_object_flags(h,0,0,flags);if(object>=0)return object;
    int pipe=wr_pipe_flags(h,0,0,flags);if(pipe>=0)return pipe;
    int file=wr_file_flags(h,0,0,flags);if(file>=0)return file;
    wr_lock(&gate);int i=index(h);if(i>=0)*flags=handles[i].flags;wr_unlock(&gate);if(i<0)wr_current_teb()->last_error=6;return i>=0;
}
static int WR_WIN SetHandleInformation(void *h,unsigned mask,unsigned flags) {
    if(mask&~3u){wr_current_teb()->last_error=87;return 0;}int job=wr_job_flags(h,mask,flags,0);if(job>=0)return job;
    int object=wr_object_flags(h,mask,flags,0);if(object>=0)return object;
    int pipe=wr_pipe_flags(h,mask,flags,0);if(pipe>=0)return pipe;
    int file=wr_file_flags(h,mask,flags,0);if(file>=0)return file;
    wr_lock(&gate);int i=index(h);
    if(i>=0)handles[i].flags=(handles[i].flags&~mask)|(flags&mask);wr_unlock(&gate);if(i<0)wr_current_teb()->last_error=6;return i>=0;
}
static int WR_WIN DuplicateHandle(void *source_process,void *h,void *target_process,void **out,unsigned access,int inherit,unsigned options) {
    if(!out){wr_current_teb()->last_error=87;return 0;}*out=0;
    (void)access; /* DUPLICATE_SAME_ACCESS explicitly ignores desired access. */
    if(source_process!=(void *)-1 || target_process!=(void *)-1 || options&~3u || !(options&2)){wr_current_teb()->last_error=50;return 0;}
    wr_current_teb()->last_error=0;
    if(wr_handle_tag((uintptr_t)h)==0x5d)*out=wr_job_duplicate(h,inherit);
    else if(wr_handle_tag((uintptr_t)h)==0x59)*out=wr_object_duplicate(h,inherit);
    else if(wr_handle_tag((uintptr_t)h)==0x55)*out=wr_pipe_duplicate(h,inherit);
    else if(wr_handle_tag((uintptr_t)h)==0x57)*out=wr_file_duplicate(h,inherit);
    else {wr_lock(&gate);int i=index(h),fd=-1;unsigned kind=1,mask=0x1fffff;
        if(h==(void *)-1){vex_process_resource_req_t q={.operation=VEX_PROCESS_SELF};fd=native(&q);}
        else if(i>=0){kind=handles[i].kind;mask=handles[i].access;fd=(int)(int64_t)vex_syscall1(VEX_SYS_DUP,handles[i].fd);}wr_unlock(&gate);
        if(fd>=0){*out=install_access(fd,kind,inherit?1:0,mask);if(!*out)close_fd(fd);}}
    if(!*out && !wr_current_teb()->last_error)wr_current_teb()->last_error=6;
    if(options&1){int (WR_WIN *close)(void *)=wr_object_exports("CloseHandle");close(h);}return *out!=0;
}
static int WR_WIN InitializeProcThreadAttributeList(attribute_list *list,unsigned count,unsigned flags,size_t *bytes) {
    if(!bytes || !count || count>8 || flags){wr_current_teb()->last_error=87;return 0;}
    size_t capacity=*bytes;*bytes=sizeof(*list);
    if(!list || capacity<sizeof(*list)){wr_current_teb()->last_error=122;return 0;}
    memset(list,0,sizeof(*list));list->magic=ATTRIBUTE_MAGIC;list->capacity=count;return 1;
}
static int WR_WIN UpdateProcThreadAttribute(attribute_list *list,unsigned flags,uintptr_t attribute,void *value,size_t size,void *previous,size_t *returned) {
    if(!list || list->magic!=ATTRIBUTE_MAGIC || flags || previous || returned){wr_current_teb()->last_error=87;return 0;}
    if(attribute!=0x20002u){wr_current_teb()->last_error=50;return 0;}
    if(!value || !size || size%sizeof(void *) || size/sizeof(void *)>WR_BOOT_MAX_FILES-6){wr_current_teb()->last_error=87;return 0;}
    wr_lock(&attribute_gate);if(list->count){wr_unlock(&attribute_gate);wr_current_teb()->last_error=87;return 0;}
    list->handles=value;list->bytes=size;list->count=1;wr_unlock(&attribute_gate);return 1;
}
static void WR_WIN DeleteProcThreadAttributeList(attribute_list *list){wr_lock(&attribute_gate);if(list && list->magic==ATTRIBUTE_MAGIC)memset(list,0,sizeof(*list));wr_unlock(&attribute_gate);}
void *wr_process_exports(const char *name) {
    void *job=wr_job_exports(name);if(job)return job;
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(CreateProcessW);E(GetCurrentProcess);E(GetCurrentThread);E(GetProcessId);E(GetExitCodeProcess);E(TerminateProcess);E(GetStartupInfoW);E(GetHandleInformation);E(SetHandleInformation);
    E(DuplicateHandle);E(InitializeProcThreadAttributeList);E(UpdateProcThreadAttribute);E(DeleteProcThreadAttributeList);
    E(CreateRemoteThread);
    E(CreateProcessA);
    E(OpenProcess);E(GetProcessHandleCount);
    E(GetProcessTimes);
    E(SetProcessShutdownParameters);E(GetProcessShutdownParameters);
#undef E
    return 0;
}
