/* WER registration and the out-of-process reporting host are compatibility
 * policy, entirely userspace. No provider executes in the crashing process.
 * Registry authorization is checked before loading any provider code. */
#include "wer.h"
#include "job.h"
#include "vex_syscall.h"
#define WER_LIMIT 16u
#define WER_THREAD ((void *)wr_handle_pack(0x5e,1,0))
typedef struct registration {struct registration *next,*retired;uint32_t active;void *context;uint16_t path[512];} registration;
static registration *registrations,*retired;
static wr_mutex gate;
static uint32_t readers,registered,reporting;
typedef struct {uint32_t size,pid,tid,reserved;void *opaque;exception_record record;win_context context;uint16_t path[512];} report_request;
typedef struct {uint32_t status,claimed,count,reserved;uint16_t event[260],names[10][260],values[10][260],path[512];} report_result;
static report_request *incoming;
static report_result *result;
static uint32_t snapshot_live;
unsigned wr_wer_handle_count(void){return __atomic_load_n(&snapshot_live,__ATOMIC_ACQUIRE)!=0;}
static void close_fd(int fd){if(fd>=0)vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);}
static int native(vex_process_resource_req_t *r){r->size=sizeof(*r);return (int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)r);}
static uint32_t hr(unsigned error){return 0x80070000u|error;}
static int equal(const uint16_t *a,const uint16_t *b){for(;;a++,b++){unsigned x=*a,y=*b;if(x>='A' && x<='Z')x+=32;if(y>='A' && y<='Z')y+=32;if(x!=y)return 0;if(!x)return 1;}}
static int copy_path(uint16_t out[512],const uint16_t *path){if(!path)return 87;
    for(unsigned i=0;i<512;i++){if(wr_memory_copy(out+i,path+i,2))return 998;if(!out[i])return i?0:87;}return 206;}
static void reclaim(void){if(__atomic_load_n(&readers,__ATOMIC_SEQ_CST))return;
    while(retired){registration *n=retired;retired=n->retired;wr_native_free(n);}}
static uint32_t WR_WIN WerRegisterRuntimeExceptionModule(const uint16_t *path,void *context) {
    registration *n=wr_native_alloc(sizeof(*n));if(!n)return hr(8);memset(n,0,sizeof(*n));unsigned error=copy_path(n->path,path);
    if(error){wr_native_free(n);return hr(error);}n->context=context;n->active=1;
    wr_lock(&gate);reclaim();
    for(registration *p=registrations;p;p=p->next)if(p->active && p->context==context && equal(p->path,n->path)){wr_unlock(&gate);wr_native_free(n);return 0;}
    if(registered==WER_LIMIT){wr_unlock(&gate);wr_native_free(n);return hr(122);}
    n->next=registrations;__atomic_store_n(&registrations,n,__ATOMIC_SEQ_CST);++registered;wr_unlock(&gate);return 0;
}
static uint32_t WR_WIN WerUnregisterRuntimeExceptionModule(const uint16_t *path,void *context) {
    uint16_t name[512];unsigned error=copy_path(name,path);if(error)return hr(error);
    wr_lock(&gate);registration **link=&registrations,*p;
    while((p=*link) && (p->context!=context || !equal(p->path,name)))link=&p->next;
    if(p){__atomic_store_n(&p->active,0,__ATOMIC_SEQ_CST);__atomic_store_n(link,p->next,__ATOMIC_SEQ_CST);--registered;p->retired=retired;retired=p;}
    reclaim();wr_unlock(&gate);return p?0:hr(2);
}
static int packet(int fd,void *buffer,size_t bytes,int sending){vex_message_io_req_t r={fd,0,buffer,bytes,0,0};
    return (int64_t)vex_syscall1(sending?VEX_SYS_MESSAGE_SEND:VEX_SYS_MESSAGE_RECV,(uintptr_t)&r)==(int64_t)bytes?0:-1;}
static int boot_packet(int fd,wr_bootstrap *b){uint8_t *p=(void *)b;size_t left=sizeof(*b);
    while(left){size_t n=left>65536?65536:left;if(packet(fd,p,n,1))return -1;p+=n;left-=n;}return 0;}
static int report_one(const report_request *report,report_result *out) {
    wr_bootstrap *b=wr_native_alloc(sizeof(*b));if(!b)return -1;memset(b,0,sizeof(*b));b->magic=0x57505243;b->size=sizeof(*b);
    unsigned n=0;while(report->path[n]){b->image[n]=report->path[n];n++;}b->image_length=n+1;
    int cwd=wr_cwd_resource(0,b->cwd,512);if(cwd<0){wr_native_free(b);return -1;}b->reserved=1;
    n=0;while(b->cwd[n])n++;b->cwd_length=n+1;b->command[0]='w';b->command_length=2;b->environment_length=2;b->startup.size=sizeof(b->startup);
    int image=wr_namespace_call(VEX_FS_ROOT_OPEN,report->path,O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE,0,0,0);
    int bootstrap[2]={-1,-1},protocol[2]={-1,-1},source=-1,view=-1,host=-1,ok=-1;
    if(image<0)goto done;
    vex_process_resource_req_t q={.operation=VEX_PROCESS_SELF};source=native(&q);if(source<0)goto done;
    q=(vex_process_resource_req_t){.operation=VEX_PROCESS_VIEW,.fd=source,.rights=VEX_PROCESS_RIGHT_QUERY|VEX_PROCESS_RIGHT_WAIT|VEX_PROCESS_RIGHT_TRANSFER};view=native(&q);if(view<0)goto done;
    b->files[0]=(wr_inherited_file){wr_handle_pack(0x54,1,0),10,1,0,0x101000};b->file_count=1;
    vex_message_pair_req_t pair={bootstrap,0,0};if(vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair))goto done;
    pair.fds=protocol;if(vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair))goto done;
    if(boot_packet(bootstrap[0],b) || packet(protocol[0],(void *)report,sizeof(*report),1))goto done;
    vex_process_grant_t grants[5]={{image,7,1,0},{bootstrap[1],8,1,0},{view,10,1,0},{protocol[1],11,1,0},{cwd,14,1,0}};
    host=wr_native_service("--wer-service",grants,5);if(host<0)goto done;
    close_fd(bootstrap[1]);bootstrap[1]=-1;close_fd(protocol[1]);protocol[1]=-1;
    if(packet(protocol[0],out,sizeof(*out),0))goto done;
    q=(vex_process_resource_req_t){.operation=VEX_PROCESS_WAIT,.fd=host};if(!native(&q))ok=0;
done:
    if(ok && host>=0){vex_process_resource_req_t stop={.operation=VEX_PROCESS_TERMINATE,.fd=host,.status=125};native(&stop);}
    for(unsigned i=0;i<2;i++){close_fd(bootstrap[i]);close_fd(protocol[i]);}close_fd(image);close_fd(cwd);close_fd(source);close_fd(view);close_fd(host);wr_native_free(b);return ok;
}
void wr_wer_report(exception_record *record,win_context *context) {
    if(!__atomic_load_n(&registrations,__ATOMIC_SEQ_CST))return;
    if(__atomic_exchange_n(&reporting,1,__ATOMIC_ACQ_REL))return;
    report_request *copy=wr_native_alloc(sizeof(*copy)*WER_LIMIT);report_result *reply=wr_native_alloc(sizeof(*reply));
    if(!copy || !reply){if(copy)wr_native_free(copy);if(reply)wr_native_free(reply);return;}
    __atomic_add_fetch(&readers,1,__ATOMIC_SEQ_CST);unsigned count=0;
    for(registration *p=__atomic_load_n(&registrations,__ATOMIC_SEQ_CST);p && count<WER_LIMIT;p=__atomic_load_n(&p->next,__ATOMIC_SEQ_CST))
        if(__atomic_load_n(&p->active,__ATOMIC_SEQ_CST)) {
            report_request *r=&copy[count++];memset(r,0,sizeof(*r));r->size=sizeof(*r);r->pid=(uint32_t)wr_pid();r->tid=wr_tid();r->opaque=p->context;r->record=*record;r->context=*context;memcpy(r->path,p->path,sizeof(r->path));
        }
    __atomic_sub_fetch(&readers,1,__ATOMIC_SEQ_CST);
    for(unsigned i=0;i<count;i++){if(!report_one(&copy[i],reply) && !reply->status && reply->claimed){wr_log("windows-wer: out-of-process provider claimed crash; local report persisted\n");break;}
        wr_log("windows-wer: provider unavailable, refused or failed; retaining fatal exception\n");}
    wr_native_free(copy);wr_native_free(reply);
}
/* The isolated prefix's MACHINE view is userspace policy, not the host
 * machine's registry. A named REG_DWORD authorizes the complete DLL path;
 * Microsoft specifies that the DWORD contents themselves are ignored. */
static int authorized(const uint16_t *path) {
    const uint16_t *key=(const uint16_t *)u"\\Registry\\Machine\\Software\\Microsoft\\Windows\\Windows Error Reporting\\RuntimeExceptionHelperModules";
    unsigned n=0;while(key[n])n++;wr_unicode name={(uint16_t)(n*2),(uint16_t)(n*2+2),0,(uint16_t *)key};
    struct {unsigned size,pad;void *root;wr_unicode *name;unsigned flags,pad2;void *security,*qos;} a={48,0,0,&name,0x40,0,0,0};
    uint32_t (WR_WIN *open)(void **,unsigned,void *,unsigned)=wr_registry_exports("NtOpenKeyEx");
    uint32_t (WR_WIN *query)(void *,wr_unicode *,unsigned,void *,unsigned,unsigned *)=wr_registry_exports("NtQueryValueKey");
    void *handle=0;if(open(&handle,1,&a,0))return 0;
    n=0;while(path[n])n++;name=(wr_unicode){(uint16_t)(n*2),(uint16_t)(n*2+2),0,(uint16_t *)path};
    struct {unsigned title,type,length,data;} value;unsigned size;unsigned status=query(handle,&name,2,&value,sizeof(value),&size);
    wr_registry_close(handle);return !status && value.type==4 && value.length==4;
}
int wr_wer_prepare(void) {
    incoming=wr_native_alloc(sizeof(*incoming));result=wr_native_alloc(sizeof(*result));
    if(!incoming || !result || packet(11,incoming,sizeof(*incoming),0) || incoming->size!=sizeof(*incoming) || incoming->reserved ||
       !incoming->path[0] || incoming->path[511])return -1;
    memset(result,0,sizeof(*result));result->status=hr(5);
    vex_process_resource_info_t info;vex_process_resource_req_t q={.operation=VEX_PROCESS_QUERY,.fd=10,.out=&info};
    if(native(&q) || info.pid!=incoming->pid || info.exited || !authorized(incoming->path)){packet(11,result,sizeof(*result),1);return -1;}
    return 0;
}
int wr_wer_thread(void *handle,win_context *out,unsigned *tid){if(handle!=WER_THREAD)return -1;
    if(!__atomic_load_n(&snapshot_live,__ATOMIC_ACQUIRE)){wr_current_teb()->last_error=6;return 0;}if(out)*out=incoming->context;if(tid)*tid=incoming->tid;return 1;}
int wr_wer_thread_close(void *handle){if(handle!=WER_THREAD)return -1;int old=__atomic_exchange_n(&snapshot_live,0,__ATOMIC_ACQ_REL);if(!old)wr_current_teb()->last_error=6;return old!=0;}
static int terminated(const uint16_t *p,unsigned n){for(unsigned i=0;i<n;i++)if(!p[i])return 1;return 0;}
static int persist(wer_information *info) {
    unsigned (WR_WIN *temp)(unsigned,uint16_t *)=wr_path_exports("GetTempPathW");
    void *(WR_WIN *create)(const uint16_t *,unsigned,unsigned,void *,unsigned,unsigned,void *)=wr_file_exports("CreateFileW");
    int (WR_WIN *write)(void *,const void *,unsigned,unsigned *,void *)=wr_file_exports("WriteFile");
    int (WR_WIN *flush)(void *)=wr_file_exports("FlushFileBuffers");
    unsigned n=temp(512,result->path);if(!n || n>450)return -1;
    const uint16_t *prefix=(const uint16_t *)u"kyil-wer-";for(unsigned i=0;prefix[i];i++)result->path[n++]=prefix[i];
    for(unsigned i=0;info->report_id[i];i++)result->path[n++]=info->report_id[i];
    const uint16_t *suffix=(const uint16_t *)u".kyilwer";for(unsigned i=0;suffix[i];i++)result->path[n++]=suffix[i];result->path[n]=0;
    void *file=create(result->path,0x40000000u,7,0,1,0x80,0);if(file==(void *)-1)return -1;
    struct {uint64_t magic;uint32_t version,size;} header={0x315245574c49594bull,1,sizeof(header)+sizeof(*incoming)+37*2+sizeof(*result)};
    unsigned bytes;int ok=write(file,&header,sizeof(header),&bytes,0) && bytes==sizeof(header) &&
        write(file,incoming,sizeof(*incoming),&bytes,0) && bytes==sizeof(*incoming) &&
        write(file,info->report_id,37*2,&bytes,0) && bytes==37*2 &&
        write(file,result,sizeof(*result),&bytes,0) && bytes==sizeof(*result) && flush(file);
    int (WR_WIN *close)(void *)=wr_object_exports("CloseHandle");if(!close(file))ok=0;return ok?0:-1;
}
int wr_wer_execute(module *provider) {
    void *(WR_WIN *get)(void *,const char *)=wr_module_exports("GetProcAddress");
    uint32_t (WR_WIN *event)(void *,const wer_information *,int *,uint16_t *,unsigned *,unsigned *)=get(provider->base,"OutOfProcessExceptionEventCallback");
    uint32_t (WR_WIN *signature)(void *,const wer_information *,unsigned,uint16_t *,unsigned *,uint16_t *,unsigned *)=get(provider->base,"OutOfProcessExceptionEventSignatureCallback");
    uint32_t (WR_WIN *debugger)(void *,const wer_information *,int *,uint16_t *,unsigned *,int *)=get(provider->base,"OutOfProcessExceptionEventDebuggerLaunchCallback");
    result->status=hr(127);if(!event || !signature || !debugger)goto done;
    wer_information *info=wr_native_alloc(sizeof(*info));if(!info){result->status=hr(8);goto done;}memset(info,0,sizeof(*info));
    uint16_t id[37];uint8_t random[16];if(wr_random(random,16)){wr_native_free(info);result->status=hr(8);goto done;}
    random[6]=(random[6]&15)|64;random[8]=(random[8]&63)|128;unsigned at=0;
    for(unsigned i=0;i<16;i++){if(i==4 || i==6 || i==8 || i==10)id[at++]='-';id[at++]="0123456789abcdef"[random[i]>>4];id[at++]="0123456789abcdef"[random[i]&15];}id[at]=0;
    info->size=sizeof(*info);info->process=(void *)wr_handle_pack(0x54,1,0);info->thread=WER_THREAD;info->record=incoming->record;info->context=incoming->context;info->report_id=id;info->fatal=1;__atomic_store_n(&snapshot_live,1,__ATOMIC_RELEASE);
    unsigned size=260;int claimed=0;result->status=event(incoming->opaque,info,&claimed,result->event,&size,&result->count);
    if(!result->status && claimed){
        if(!terminated(result->event,260) || !result->event[0] || !result->count){result->status=hr(87);goto cleanup;}
        if(result->count>10)result->count=10;
        for(unsigned i=0;i<result->count;i++){unsigned name=260,value=260;result->status=signature(incoming->opaque,info,i,result->names[i],&name,result->values[i],&value);
            if(result->status)goto cleanup;if(!terminated(result->names[i],260) || !terminated(result->values[i],260)){result->status=hr(87);goto cleanup;}}
        int custom=0,automatic=0;uint16_t launch[260];size=260;result->status=debugger(incoming->opaque,info,&custom,launch,&size,&automatic);
        if(result->status)goto cleanup;if(custom || automatic){result->status=hr(50);goto cleanup;}
        result->claimed=1;if(persist(info)){result->status=hr(1117);result->claimed=0;goto cleanup;}
    }
cleanup:__atomic_store_n(&snapshot_live,0,__ATOMIC_RELEASE);wr_native_free(info);
done:packet(11,result,sizeof(*result),1);return result->status?1:0;
}
void *wr_wer_exports(const char *name){
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(WerRegisterRuntimeExceptionModule);E(WerUnregisterRuntimeExceptionModule);
#undef E
    return 0;
}
