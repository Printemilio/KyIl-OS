/* Native browser supervisor. Chrome is Vex; pages/rendering belong to WebKit.
 * One reliable control endpoint, one capability-bearing renderer endpoint per
 * page. No framebuffer snapshots or command overwrite/deduplication. */
#include <stddef.h>
#include <stdint.h>
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "vex_browser_protocol.h"
#define SHM_MAGIC 0x56455842u
#define MSG_NOWAIT 0x40u
static int control=-1,child_control=-1,diagnostics=-1;
static char diagnostic_line[512];
static size_t diagnostic_line_length;
static uint8_t diagnostic_line_overflow, diagnostic_shader_dump_state;
typedef struct {uint32_t magic,width,height,stride,frame_seq,cmd_seq,cmd_seq_ack,reserved;} browser_bootstrap;
static struct page {uint32_t id;int fd;uint64_t surface;vex_browser_frame frame;vex_browser_input input;int input_pending;} pages[VEX_BROWSER_MAX_TABS];
static void log_text(const char *s){vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)s);}
static size_t length(const char *s){size_t n=0;while(s[n])n++;return n;}
static void copy(char *d,const char *s,size_t capacity){size_t i=0;while(s[i]&&i+1<capacity){d[i]=s[i];i++;}d[i]=0;}
static int starts_with_text(const char *text,const char *prefix){size_t i=0;while(prefix[i]&&text[i]==prefix[i])i++;return prefix[i]==0;}
static void diagnostics_emit_line(void){
    diagnostic_line[diagnostic_line_length]=0;
    if(diagnostic_shader_dump_state==0){
        if(starts_with_text(diagnostic_line,"Vex GLES: link failed")){
            log_text(diagnostic_line);log_text(diagnostic_line_overflow?" [details elided]\n":"\n");
            diagnostic_shader_dump_state=1;
        }else{log_text(diagnostic_line);if(diagnostic_line_overflow)log_text(" [line elided]");log_text("\n");}
    }else if(diagnostic_shader_dump_state==1){
        if(starts_with_text(diagnostic_line,"vertex:"))diagnostic_shader_dump_state=2;
        else if(starts_with_text(diagnostic_line,"fragment:"))diagnostic_shader_dump_state=3;
        else{diagnostic_shader_dump_state=0;log_text(diagnostic_line);log_text("\n");}
    }else if(diagnostic_shader_dump_state==2){
        if(starts_with_text(diagnostic_line,"fragment:"))diagnostic_shader_dump_state=3;
    }else{
        /* glLinkProgram's diagnostic payload is one generated GLSL line. */
        diagnostic_shader_dump_state=0;
    }
    diagnostic_line_length=0;diagnostic_line_overflow=0;
}
static void diagnostics_forward(const char *text,size_t count){
    for(size_t i=0;i<count;i++){
        if(text[i]=='\n'){diagnostics_emit_line();continue;}
        if(diagnostic_line_length+1<sizeof(diagnostic_line))diagnostic_line[diagnostic_line_length++]=text[i];
        else diagnostic_line_overflow=1;
    }
}
static struct page *find_page(uint32_t id){for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)if(pages[i].id==id)return &pages[i];return NULL;}
#include "webkit_session.h"
static void close_page(struct page *p){if(p->surface)vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,p->surface);if(p->fd>=0)vex_syscall1(VEX_SYS_CLOSE,p->fd);*p=(struct page){.fd=-1};}
static void diagnostics_pump(void){char text[513];for(unsigned i=0;i<8 && diagnostics>=0;i++){vex_fd_io_req_t r={diagnostics,text,512};int64_t n=vex_syscall1(VEX_SYS_READFD,(uintptr_t)&r);if(n>0)diagnostics_forward(text,(size_t)n);else{if(!n){if(diagnostic_line_length||diagnostic_line_overflow)diagnostics_emit_line();vex_syscall1(VEX_SYS_CLOSE,diagnostics);diagnostics=-1;}break;}}}
static int control_receive(void){for(unsigned iteration=0;iteration<16;iteration++){
    vex_browser_control message={0};vex_message_attachment_req_t a={0};vex_message_io_req_t r={control,MSG_NOWAIT,&message,sizeof(message),&a,1};
    int64_t n=vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&r);if(n==-11)return 0;if(n<=0)return -1;
    if(n!=sizeof(message)||message.magic!=VEX_BROWSER_CONTROL){if(r.attachment_count && a.type==VEX_MESSAGE_OBJECT_FD)vex_syscall1(VEX_SYS_CLOSE,a.handle);return -1;}
    if(message.operation==VEX_BROWSER_READY){
        struct page *p=find_page(message.id);
        if(!p)for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)if(!pages[i].id){p=&pages[i];break;}
        if(!p || r.attachment_count!=1 || a.type!=VEX_MESSAGE_OBJECT_FD){if(r.attachment_count && a.type==VEX_MESSAGE_OBJECT_FD)vex_syscall1(VEX_SYS_CLOSE,a.handle);return -1;}
        if(p->id)close_page(p);*p=(struct page){.id=message.id,.fd=(int)a.handle};log_text("browser: page endpoint received\n");
        vex_viewport_req_t query={.operation=VEX_VIEWPORT_QUERY};vex_syscall1(VEX_SYS_BROWSER_VIEWPORT,(uintptr_t)&query);
        if(query.tab_id!=message.id)query.event.width=query.event.height=0;
        p->input=(vex_browser_input){VEX_BROWSER_INPUT,query.event};p->input_pending=1;
    }else if(message.operation==VEX_BROWSER_REPORT){
        vex_viewport_req_t state={.operation=VEX_VIEWPORT_STATE,.state=&message.state};vex_syscall1(VEX_SYS_BROWSER_VIEWPORT,(uintptr_t)&state);
    }else if(message.operation==VEX_BROWSER_DOWNLOAD_REPORT){
        /* The chrome owns the download table; a refused report (stale identity,
         * malformed text) is dropped rather than treated as a channel failure. */
        vex_viewport_req_t report={.operation=VEX_VIEWPORT_DOWNLOAD,.download=&message.download};
        vex_syscall1(VEX_SYS_BROWSER_VIEWPORT,(uintptr_t)&report);
    }else return -1;
}return 0;}
static int page_pump(struct page *p){if(!p->id)return 0;
    if(p->input_pending){vex_message_io_req_t send={p->fd,MSG_NOWAIT,&p->input,sizeof(p->input),0,0};int64_t n=vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send);if(n==sizeof(p->input))p->input_pending=0;else if(n!=-11)return -1;}
    if(!p->surface){vex_message_attachment_req_t a={0};vex_message_io_req_t r={p->fd,MSG_NOWAIT,&p->frame,sizeof(p->frame),&a,1};int64_t n=vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&r);
        if(n>0){if(n!=sizeof(p->frame)||p->frame.magic!=VEX_BROWSER_FRAME||r.attachment_count!=1||a.type!=VEX_MESSAGE_OBJECT_SURFACE){if(r.attachment_count&&a.type==VEX_MESSAGE_OBJECT_SURFACE)vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,a.handle);return -1;}p->surface=a.handle;}
        else if(n!=-11)return -1;
    }
    if(p->surface){vex_viewport_req_t present={.operation=VEX_VIEWPORT_PRESENT,.surface=p->surface,.tab_id=p->id};int64_t result=vex_syscall1(VEX_SYS_BROWSER_VIEWPORT,(uintptr_t)&present);
        if(!result){/* Native composition/cache completed before allowing reuse. */
            vex_message_io_req_t ack={p->fd,MSG_NOWAIT,&p->frame,sizeof(p->frame),0,0};int64_t n=vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&ack);
            if(n==sizeof(p->frame)){vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,p->surface);p->surface=0;}
            else if(n!=-11)return -1;
        }else if(result!=-11)return -1;
    }return 0;
}
static int parse_command(const char *s,uint32_t active,vex_browser_control *c){
    *c=(vex_browser_control){.magic=VEX_BROWSER_CONTROL,.operation=VEX_BROWSER_NAVIGATE,.id=active};
    if(s[0]=='b'&&s[1]=='r'&&s[2]=='o'&&s[3]=='w'&&s[4]=='s'&&s[5]=='e'&&s[6]=='r'&&s[7]==':'){
        s+=8;static const char *names[]={"create","select","close","navigate","back","forward","reload","stop","probe","shutdown","download-cancel"};
        static const uint32_t ops[]={VEX_BROWSER_CREATE,VEX_BROWSER_SELECT,VEX_BROWSER_CLOSE,VEX_BROWSER_NAVIGATE,VEX_BROWSER_BACK,VEX_BROWSER_FORWARD,VEX_BROWSER_RELOAD,VEX_BROWSER_STOP,VEX_BROWSER_PROBE,VEX_BROWSER_SHUTDOWN,VEX_BROWSER_DOWNLOAD_CANCEL};
        const unsigned count=sizeof(names)/sizeof(names[0]);
        unsigned i;for(i=0;i<count;i++){size_t j=0;while(names[i][j]&&s[j]==names[i][j])j++;if(!names[i][j]&&s[j]==':'){c->operation=ops[i];s+=j+1;break;}}if(i==count)return -1;
        uint64_t id=0;if(*s<'0'||*s>'9')return -1;while(*s>='0'&&*s<='9'){id=id*10+*s++-'0';if(id>UINT32_MAX)return -1;}if(*s++!=':'||!id)return -1;c->id=(uint32_t)id;
    }
    if(length(s)>=sizeof(c->state.uri))return -1;copy(c->state.uri,s,sizeof(c->state.uri));return 0;
}
int main(void){for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)pages[i].fd=-1;
    if(prepare_webkit_runtime())return 1;
    if((int64_t)vex_syscall2(VEX_SYS_DISPLAY_OPEN,1280,720))return 1;
    int pair[2];vex_message_pair_req_t pr={pair,0,0};if((int64_t)vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pr))return 1;control=pair[0];child_control=pair[1];
    vex_shm_create_req_t create={.name="vex_webkit_fb",.size=sizeof(browser_bootstrap),.prot=PROT_READ|PROT_WRITE};if((int64_t)vex_syscall1(VEX_SYS_SHM_CREATE,(uintptr_t)&create))return 1;
    vex_shm_map_req_t map={.handle=create.handle,.length=sizeof(browser_bootstrap),.prot=PROT_READ|PROT_WRITE};if((int64_t)vex_syscall1(VEX_SYS_SHM_MAP,(uintptr_t)&map))return 1;
    *(browser_bootstrap *)map.mapped_addr=(browser_bootstrap){SHM_MAGIC,1280,720,1280,0,0,0,(uint32_t)child_control};
    int child=webkit_session_spawn(control,&child_control,&diagnostics);if(child<0)return 1;
    char command[VEX_BROWSER_URI_SIZE+48]={0};vex_browser_control pending;int pending_valid=0,closing=0,child_exited=0;uint32_t selected=0;
    for(;;){diagnostics_pump();
        if(closing){int status;vex_process_wait_req_t wait={child,&status,VEX_PROCESS_WAIT_NOHANG};
            int64_t n=vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&wait);
            if(n>0){child_exited=1;log_text("browser: graceful shutdown complete\n");break;}
            vex_syscall1(VEX_SYS_USLEEP,16000);continue;}
        if(control_receive())break;
        vex_viewport_req_t query={.operation=VEX_VIEWPORT_QUERY};if((int64_t)vex_syscall1(VEX_SYS_BROWSER_VIEWPORT,(uintptr_t)&query))break;
        selected=query.tab_id;
        if(!pending_valid){vex_browser_command_req_t read={command,sizeof(command)};int64_t n=vex_syscall1(VEX_SYS_BROWSER_COMMAND_READ,(uintptr_t)&read);if(n>0){if(parse_command(command,selected,&pending)){log_text("browser: malformed native command\n");break;}pending_valid=1;}}
        if(pending_valid){vex_message_io_req_t send={control,MSG_NOWAIT,&pending,sizeof(pending),0,0};int64_t n=vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send);if(n==sizeof(pending)){if(pending.operation==VEX_BROWSER_SHUTDOWN)closing=1;if(pending.operation==VEX_BROWSER_CLOSE){struct page *p=find_page(pending.id);if(p)close_page(p);}pending_valid=0;}else if(n!=-11)break;}
        for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)if(page_pump(&pages[i])){log_text("browser: page channel closed\n");close_page(&pages[i]);}
        struct page *active=find_page(selected);
        if(active&&!active->input_pending){vex_viewport_req_t poll={.operation=VEX_VIEWPORT_POLL};for(unsigned i=0;i<32;i++){int64_t n=vex_syscall1(VEX_SYS_BROWSER_VIEWPORT,(uintptr_t)&poll);if(n<=0){if(n<0)log_text("browser: input queue error\n");break;}struct page *p=find_page(poll.tab_id);if(!p)continue;
                p->input=(vex_browser_input){VEX_BROWSER_INPUT,poll.event};p->input_pending=1;if(page_pump(p))close_page(p);if(p->input_pending)break;}}
        int status;vex_process_wait_req_t wait={child,&status,VEX_PROCESS_WAIT_NOHANG};int64_t n=vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&wait);if(n>0){child_exited=1;log_text("browser: WebKit UI exited\n");break;}
        vex_syscall1(VEX_SYS_USLEEP,16000);
    }
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)close_page(&pages[i]);
    vex_syscall1(VEX_SYS_CLOSE,control);if(child>0&&!child_exited)vex_syscall1(VEX_SYS_PROCESS_KILL,child);
    vex_syscall1(VEX_SYS_SHM_CLOSE,create.handle);vex_syscall0(VEX_SYS_DISPLAY_CLOSE);return child_exited?0:1;
}
