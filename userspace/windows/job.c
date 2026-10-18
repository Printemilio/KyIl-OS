/* Windows Job policy is userspace. The service owns native process capabilities;
 * a job HANDLE is a local slot holding an IPC capability, never a global ID.
 * Separate public/member ingress preserves last-public-handle semantics. */
#include "job.h"
#include "vex_syscall.h"
#define JOB_MEMBERS 48u
enum {J_QUERY=1,J_LIMIT,J_ADD,J_TERMINATE,J_REMOVE,J_WAIT,J_CANCEL,J_RELEASE,J_LAST_PUBLIC,J_LAST_MEMBER,J_EXIT};
typedef struct {uint32_t op,role,error,pid;uint64_t token;uint32_t flags,active,total,status;uint64_t values[18];} job_message;
typedef struct {uint64_t user_time,job_time;uint32_t flags,pad;uint64_t minimum,maximum;uint32_t active,pad2;uint64_t affinity;uint32_t priority,scheduling;} basic_limit;
typedef struct {basic_limit basic;uint64_t io[6],process_memory,job_memory,peak_process,peak_job;} extended_limit;
_Static_assert(sizeof(basic_limit)==64 && sizeof(extended_limit)==144,"Windows job limit layouts");
static struct {int rpc,member;uint32_t generation,flags,live;} handles[64];
static wr_mutex gate,current_gate;
unsigned wr_job_handle_count(void){wr_lock(&gate);unsigned count=0;for(unsigned i=0;i<64;i++)if(handles[i].live)count++;wr_unlock(&gate);return count;}
static int current_member=-1;
static uint32_t current_job_pid;
static int native(vex_process_resource_req_t *r){r->size=sizeof(*r);return (int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)r);}
static void close_fd(int fd){if(fd>=0)vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);}
static int duplicate(int fd){return fd<0?-1:(int)(int64_t)vex_syscall1(VEX_SYS_DUP,(uintptr_t)fd);}
static int pair(int fds[2]){vex_message_pair_req_t p={fds,0,0};return (int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&p);}
static int message(int fd,job_message *m,int fds[3],unsigned *count,int sending,unsigned flags) {
    vex_message_attachment_req_t a[3];unsigned capacity=*count;
    for(unsigned i=0;i<capacity;i++)a[i]=(vex_message_attachment_req_t){sending?(uint64_t)(uint32_t)fds[i]:0,VEX_MESSAGE_OBJECT_FD,sending?VEX_MESSAGE_ATTACHMENT_DUP:0};
    vex_message_io_req_t q={fd,flags,m,sizeof(*m),a,capacity};
    int64_t n=(int64_t)vex_syscall1(sending?VEX_SYS_MESSAGE_SEND:VEX_SYS_MESSAGE_RECV,(uintptr_t)&q);
    if(sending)return n==(int64_t)sizeof(*m)?0:n<0?(int)n:-1;
    if(n!=(int64_t)sizeof(*m)){*count=0;return n<=0?(int)n:-1;}
    *count=(unsigned)q.attachment_count;
    for(unsigned i=0;i<*count;i++)fds[i]=(int)a[i].handle;
    return 1;
}
static int request(int fd,job_message *m,int capability,int *returned,unsigned timeout) {
    int reply[2];if(pair(reply))return 8;
    int fds[3]={reply[1],capability,-1};unsigned count=capability>=0?2:1;
    if(!m->token && wr_random(&m->token,sizeof(m->token))){close_fd(reply[0]);close_fd(reply[1]);return 8;}
    int error=message(fd,m,fds,&count,1,0);close_fd(reply[1]);
    if(error){close_fd(reply[0]);return 6;}
    if(timeout!=UINT32_MAX){struct pollfd p={reply[0],POLLIN,0};vex_poll_req_t q={&p,1,(int)timeout};
        int64_t ready=(int64_t)vex_syscall1(VEX_SYS_POLL,(uintptr_t)&q);
        if(ready<=0){job_message cancel={.op=J_CANCEL,.token=m->token};unsigned zero=0;message(fd,&cancel,fds,&zero,1,0);close_fd(reply[0]);return ready?1117:258;}}
    count=returned?1:0;int result=message(reply[0],m,fds,&count,0,0);close_fd(reply[0]);
    if(result!=1)return 6;if(returned)*returned=count?fds[0]:-1;return m->error;
}
static int index(void *h){uintptr_t v=(uintptr_t)h;unsigned i=wr_handle_slot(v);
    return wr_handle_tag(v)==0x5d && i<64 && wr_handle_generation(v)==handles[i].generation && handles[i].live?(int)i:-1;}
static int borrow(void *h,int *member){wr_lock(&gate);int i=index(h),fd=-1;
    if(i>=0){fd=duplicate(handles[i].rpc);if(member)*member=duplicate(handles[i].member);}wr_unlock(&gate);return fd;}
static void *install(int rpc,int member,unsigned flags){wr_lock(&gate);unsigned i=0;while(i<64 && (handles[i].live || handles[i].generation>=WR_HANDLE_GENERATION_MAX))i++;
    if(i==64){wr_unlock(&gate);return 0;}if(!++handles[i].generation)++handles[i].generation;
    handles[i].live=1;handles[i].rpc=rpc;handles[i].member=member;handles[i].flags=flags;
    void *h=(void *)(wr_handle_pack(0x5d,handles[i].generation,i));wr_unlock(&gate);return h;}
void wr_job_quiesce(int begin){if(begin){wr_lock(&current_gate);wr_lock(&gate);}else{wr_unlock(&gate);wr_unlock(&current_gate);}}
int wr_job_flags(void *h,unsigned mask,unsigned flags,unsigned *old){if(wr_handle_tag((uintptr_t)h)!=0x5d)return -1;
    if(mask&flags&1){wr_current_teb()->last_error=50;return 0;}
    wr_lock(&gate);int i=index(h);if(i>=0){if(old)*old=handles[i].flags;handles[i].flags=(handles[i].flags&~mask)|(flags&mask);}
    wr_unlock(&gate);if(i<0)wr_current_teb()->last_error=6;return i>=0;}
int wr_job_close(void *h){if(wr_handle_tag((uintptr_t)h)!=0x5d)return -1;wr_lock(&gate);int i=index(h),rpc=-1,member=-1;
    unsigned why=6;if(i>=0){if(handles[i].flags&2)why=5;else{rpc=handles[i].rpc;member=handles[i].member;handles[i].live=0;}}
    wr_unlock(&gate);if(rpc<0){wr_current_teb()->last_error=why;return 0;}close_fd(rpc);
    job_message q={.op=J_RELEASE};unsigned error=request(member,&q,-1,0,UINT32_MAX);close_fd(member);
    if(error)wr_current_teb()->last_error=error;return !error;}
void *wr_job_duplicate(void *h,int inherit){if(inherit){wr_current_teb()->last_error=50;return 0;}
    int member=-1,rpc=borrow(h,&member);if(rpc<0 || member<0){close_fd(rpc);close_fd(member);wr_current_teb()->last_error=6;return 0;}
    void *out=install(rpc,member,0);if(!out){close_fd(rpc);close_fd(member);wr_current_teb()->last_error=8;}return out;}
int wr_job_current_fd(void){wr_lock(&current_gate);int fd=duplicate(current_member);wr_unlock(&current_gate);return fd;}
int wr_job_launch_begin(void){wr_lock(&current_gate);int fd=duplicate(current_member);return current_member>=0 && fd<0?-2:fd;}
void wr_job_launch_end(void){wr_unlock(&current_gate);}
int wr_job_adopt(void){if(!wr_boot || !wr_boot->job_member)return 0;
    job_message q={.op=J_QUERY};int error=request(13,&q,-1,0,UINT32_MAX);if(error)return -1;
    current_member=13;current_job_pid=q.pid;return 0;}
int wr_job_add_child(int process){if(current_member<0)return 0;
    job_message q={.op=J_ADD};return request(current_member,&q,process,0,UINT32_MAX);}

static void *WR_WIN CreateJobObjectW(void *security,const uint16_t *name) {
    if(security || name){wr_current_teb()->last_error=50;return 0;}
    int rpc[2]={-1,-1},member[2]={-1,-1};void *handle=0;
    if(pair(rpc) || pair(member)){wr_log("windows-job: endpoint creation failed\n");goto fail;}
    vex_process_grant_t grants[2]={{rpc[1],8,1,0},{member[1],11,1,0}};
    int process=wr_native_service("--job-service",grants,2);if(process<0){char text[]="windows-job: native service launch error=00000000\n";unsigned at=0;while(text[at] && text[at]!='=')at++;at++;for(unsigned i=0;i<8;i++)text[at+i]="0123456789abcdef"[((unsigned)-process>>(28-4*i))&15];wr_log(text);goto fail;}close_fd(process);
    close_fd(rpc[1]);rpc[1]=-1;close_fd(member[1]);member[1]=-1;
    job_message q={.op=J_QUERY};int ready=request(rpc[0],&q,-1,0,UINT32_MAX);if(ready){wr_log("windows-job: service readiness failed\n");goto fail;}
    handle=install(rpc[0],member[0],0);if(!handle)goto fail;return handle;
fail:for(unsigned i=0;i<2;i++){close_fd(rpc[i]);close_fd(member[i]);}wr_current_teb()->last_error=8;return 0;
}
static int WR_WIN SetInformationJobObject(void *h,unsigned kind,const void *data,unsigned size) {
    int fd=borrow(h,0);if(fd<0){wr_current_teb()->last_error=6;return 0;}unsigned error=0;extended_limit limit;memset(&limit,0,sizeof(limit));
    if((kind!=2 && kind!=9))error=50;
    else if(size!=(kind==2?sizeof(basic_limit):sizeof(limit)))error=87;
    else if(wr_memory_copy(&limit,data,size))error=998;
    else if(limit.basic.flags&~(8u|0x2000u))error=50;
    else if((limit.basic.flags&8) && !limit.basic.active)error=87;
    if(!error){job_message q={.op=J_LIMIT,.flags=limit.basic.flags,.active=limit.basic.active};error=request(fd,&q,-1,0,UINT32_MAX);}
    close_fd(fd);if(error)wr_current_teb()->last_error=error;return !error;
}
static int WR_WIN QueryInformationJobObject(void *h,unsigned kind,void *out,unsigned size,unsigned *used) {
    int fd=h?borrow(h,0):wr_job_current_fd();if(fd<0){wr_current_teb()->last_error=6;return 0;}
    job_message q={.op=J_QUERY};unsigned error=request(fd,&q,-1,0,UINT32_MAX);close_fd(fd);
    extended_limit result;memset(&result,0,sizeof(result));result.basic.flags=q.flags;result.basic.active=q.values[0];
    if(!error && (kind!=2 && kind!=9))error=50;
    if(!error && kind==9 && q.total)error=50; /* Peak usage is not yet observable. */
    unsigned length=kind==2?sizeof(basic_limit):sizeof(result);
    if(!error && size<length)error=24;
    if(!error && wr_memory_copy(out,&result,length))error=998;
    if(!error && used && wr_memory_copy(used,&length,4))error=998;
    if(error)wr_current_teb()->last_error=error;return !error;
}
static int WR_WIN AssignProcessToJobObject(void *h,void *process) {
    unsigned (WR_WIN *get_pid)(void *)=wr_process_exports("GetProcessId");
    unsigned pid=get_pid(process);if(!pid)return 0;
    /* Foreign assignment requires an atomic bootstrap/control handoff. Do
       not pretend a running PE acquired membership without receiving it. */
    if(pid!=wr_pid()){wr_current_teb()->last_error=50;return 0;}
    wr_lock(&current_gate);unsigned error=current_member>=0?5:0;int member=-1,fd=-1,cap=-1;
    if(!error){fd=borrow(h,&member);cap=wr_process_capability(process);if(fd<0 || member<0 || cap<0)error=6;}
    job_message q={.op=J_ADD};if(!error)error=request(fd,&q,cap,0,UINT32_MAX);
    if(!error){current_member=member;member=-1;current_job_pid=q.pid;}
    close_fd(cap);close_fd(fd);close_fd(member);wr_unlock(&current_gate);
    if(error)wr_current_teb()->last_error=error;return !error;
}
static int WR_WIN IsProcessInJob(void *process,void *job,int *out) {
    unsigned (WR_WIN *get_pid)(void *)=wr_process_exports("GetProcessId");unsigned pid=get_pid(process);
    if(!pid)return 0;if(pid!=wr_pid()){wr_current_teb()->last_error=50;return 0;}int result=current_member>=0;
    if(job){int fd=borrow(job,0);if(fd<0){wr_current_teb()->last_error=6;return 0;}job_message q={.op=J_QUERY};unsigned e=request(fd,&q,-1,0,UINT32_MAX);close_fd(fd);if(e){wr_current_teb()->last_error=e;return 0;}result=result && current_job_pid==q.pid;}
    if(wr_memory_copy(out,&result,4)){wr_current_teb()->last_error=998;return 0;}return 1;
}
static int WR_WIN TerminateJobObject(void *h,unsigned status){int fd=borrow(h,0);if(fd<0){wr_current_teb()->last_error=6;return 0;}
    job_message q={.op=J_TERMINATE,.status=status};unsigned error=request(fd,&q,-1,0,UINT32_MAX);close_fd(fd);if(error)wr_current_teb()->last_error=error;return !error;}
int wr_job_wait(void *h,unsigned timeout,unsigned *result){if(wr_handle_tag((uintptr_t)h)!=0x5d)return 0;
    /* Waiting must not retain a PUBLIC endpoint: otherwise a concurrent last
       CloseHandle could not trigger kill-on-close until this wait completes. */
    wr_lock(&gate);int i=index(h),fd=i<0?-1:duplicate(handles[i].member);wr_unlock(&gate);
    if(fd<0){wr_current_teb()->last_error=6;*result=UINT32_MAX;return 1;}job_message q={.op=timeout?J_WAIT:J_QUERY};unsigned e=request(fd,&q,-1,0,timeout?timeout:UINT32_MAX);close_fd(fd);
    if(!e && !timeout && q.active)e=258;
    *result=e==258?258:e?UINT32_MAX:0;if(e && e!=258)wr_current_teb()->last_error=e;return 1;}
void *wr_job_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(CreateJobObjectW);E(SetInformationJobObject);E(QueryInformationJobObject);E(AssignProcessToJobObject);E(IsProcessInJob);E(TerminateJobObject);
#undef E
    return 0;
}

/* Two ingress workers and membership waiters sleep on the actual native IPC /
 * process wait primitives. Only the service can write its aggregation channel.
 * Client-provided role/EXIT/CLOSE fields are never trusted. */
typedef struct {int source,aggregate;unsigned role;void *stack;} ingress;
typedef struct {int process,aggregate;unsigned index;uint32_t alive,tid,active;void *stack;} member_record;
static member_record members[JOB_MEMBERS];
static void forward(void *opaque){ingress *in=opaque;
    for(;;){job_message q;int fds[3];unsigned count=3;int n=message(in->source,&q,fds,&count,0,0);
        if(n<=0){q=(job_message){.op=in->role==1?J_LAST_PUBLIC:J_LAST_MEMBER,.role=3};count=0;message(in->aggregate,&q,fds,&count,1,0);break;}
        q.role=in->role;if(q.op>=J_LAST_PUBLIC){for(unsigned i=0;i<count;i++)close_fd(fds[i]);continue;}
        message(in->aggregate,&q,fds,&count,1,0);for(unsigned i=0;i<count;i++)close_fd(fds[i]);
    }
    wr_end_thread(in->stack,65536,0);
}
static void member_wait(void *opaque){member_record *m=opaque;vex_process_resource_req_t wait={.operation=VEX_PROCESS_WAIT,.fd=m->process};
    native(&wait);job_message q={.op=J_EXIT,.role=3,.active=m->index};int fds[3];unsigned count=0;
    message(m->aggregate,&q,fds,&count,1,0);wr_end_thread(m->stack,65536,0);
}
int wr_job_service(void) {
    int aggregate[2];if(pair(aggregate))return 125;
    ingress inputs[2]={{8,aggregate[0],1,0},{11,aggregate[0],2,0}};uint32_t alive[2]={1,1},tid[2];
    for(unsigned i=0;i<2;i++){void *stack=wr_map(0,65536,PE_R|PE_W);inputs[i].stack=stack;if(!stack || wr_start_thread(forward,&inputs[i],(char *)stack+65536,&tid[i],&alive[i])<0)return 125;}
    struct {int reply;uint64_t token;} waits[64];for(unsigned i=0;i<64;i++)waits[i].reply=-1;
    unsigned active=0,total=0,flags=0,limit=0,public_closed=0,member_closed=0;
    for(;;){job_message q;int fds[3]={-1,-1,-1};unsigned count=3;
        if(message(aggregate[1],&q,fds,&count,0,0)!=1)return 125;
        unsigned error=0;int deferred=0;
        if(q.role==3){
            if(q.op==J_LAST_PUBLIC)public_closed=1;
            else if(q.op==J_LAST_MEMBER)member_closed=1;
            else if(q.op==J_EXIT && q.active<JOB_MEMBERS && members[q.active].active){members[q.active].active=0;--active;}
        } else if(q.op==J_CANCEL){for(unsigned i=0;i<64;i++)if(waits[i].reply>=0 && waits[i].token==q.token){close_fd(waits[i].reply);waits[i].reply=-1;}}
        else if(!count)error=87;
        else if(q.op==J_QUERY){}
        else if(q.op==J_ADD) {
            if(count!=2)error=87;
            vex_process_resource_info_t info;vex_process_resource_req_t r={.operation=VEX_PROCESS_QUERY,.fd=fds[1],.out=&info};
            if(!error && native(&r))error=6;
            if(!error && info.exited)error=5;
            if(!error){r=(vex_process_resource_req_t){.operation=VEX_PROCESS_VIEW,.fd=fds[1],.rights=VEX_PROCESS_RIGHT_QUERY|VEX_PROCESS_RIGHT_WAIT|VEX_PROCESS_RIGHT_TERMINATE};
                int view=native(&r);if(view<0)error=5;else{close_fd(fds[1]);fds[1]=view;}}
            unsigned index=JOB_MEMBERS;
            if(!error){for(unsigned i=0;i<JOB_MEMBERS;i++)if(members[i].active){vex_process_resource_info_t old;r=(vex_process_resource_req_t){.operation=VEX_PROCESS_QUERY,.fd=members[i].process,.out=&old};if(!native(&r) && old.pid==info.pid)error=5;}
                if((flags&8) && active>=limit)error=1816;
                for(unsigned i=0;i<JOB_MEMBERS;i++)if(!members[i].active && !__atomic_load_n(&members[i].alive,__ATOMIC_ACQUIRE)){index=i;break;}
                if(index==JOB_MEMBERS)error=8;}
            if(!error){member_record *m=&members[index];close_fd(m->process>=0 && m->stack?m->process:-1);
                m->process=fds[1];fds[1]=-1;m->aggregate=aggregate[0];m->index=index;m->active=m->alive=1;
                m->stack=wr_map(0,65536,PE_R|PE_W);
                if(!m->stack || wr_start_thread(member_wait,m,(char *)m->stack+65536,&m->tid,&m->alive)<0){if(m->stack)wr_unmap(m->stack,65536);close_fd(m->process);m->process=-1;m->stack=0;m->active=m->alive=0;error=8;}
                else{++active;++total;}}
        } else if(q.op==J_RELEASE){vex_message_observe_req_t state={.fd=8};
            if((int64_t)vex_syscall1(VEX_SYS_MESSAGE_OBSERVE,(uintptr_t)&state)<0)error=6;else if(state.peer_closed)public_closed=1;}
        else if(q.op==J_WAIT){if(active){unsigned i=0;while(i<64 && waits[i].reply>=0)i++;if(i==64)error=8;else{waits[i].reply=fds[0];fds[0]=-1;waits[i].token=q.token;deferred=1;}}}
        else if(q.role!=1)error=5;
        else if(q.op==J_LIMIT){if((q.flags&~(8u|0x2000u)) || ((q.flags&8) && !q.active))error=50;
            else if((q.flags&8) && active>q.active)error=1816;else{flags=q.flags;limit=q.active;}}
        else if(q.op==J_TERMINATE){for(unsigned i=0;i<JOB_MEMBERS;i++)if(members[i].active){vex_process_resource_req_t r={.operation=VEX_PROCESS_TERMINATE,.fd=members[i].process,.status=(int32_t)q.status};int e=native(&r);if(e && e!=-3)error=5;}}
        else error=50;
        int kill_on_close=public_closed && (flags&0x2000);
        if(kill_on_close)for(unsigned i=0;i<JOB_MEMBERS;i++)if(members[i].active){vex_process_resource_req_t r={.operation=VEX_PROCESS_TERMINATE,.fd=members[i].process,.status=0};native(&r);}
        if(count && fds[0]>=0 && !deferred){q.error=error;q.pid=(uint32_t)wr_pid();q.flags=flags;q.active=active;q.total=total;q.values[0]=limit;unsigned zero=0;message(fds[0],&q,fds+1,&zero,1,0);}
        for(unsigned i=0;i<count;i++)close_fd(fds[i]);
        if(!active)for(unsigned i=0;i<64;i++)if(waits[i].reply>=0){job_message done={0};unsigned zero=0;message(waits[i].reply,&done,fds,&zero,1,0);close_fd(waits[i].reply);waits[i].reply=-1;}
        if(kill_on_close && member_closed)return 0;
        if(public_closed && member_closed && !active)return 0;
    }
}
