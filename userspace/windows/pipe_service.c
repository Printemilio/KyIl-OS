/* Windows pipe policy/protocol executes exclusively in Ultra-Space userspace.
 * There are NO rendezvous registrations or globally claimable data endpoints. */
#include "runtime.h"
#include "pipe_protocol.h"
#include "pipe_policy.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
typedef struct instance {struct instance *next;wp_packet spec;uint32_t owner;
    uint8_t *sd;int control,data,waiting,connected,delivered;} instance;
typedef struct wait_request {struct wait_request *next;int reply;uint64_t deadline;wp_packet q;} wait_request;
static uint8_t *scratch;
static instance *instances;static wait_request *waits;
static int call(uint64_t n,void *p){return (int)(int64_t)vex_syscall1(n,(uintptr_t)p);}
static void close_fd(int fd){if(fd>=0)vex_syscall1(VEX_SYS_CLOSE,fd);}
static int pair(int fds[2]){vex_message_pair_req_t q={fds,0,0};return call(VEX_SYS_MESSAGE_PAIR,&q);}
static int attenuate(int fd,unsigned rights){vex_rendezvous_req_t q={.operation=VEX_RENDEZVOUS_ATTENUATE,.resource_fd=fd,.rights=rights,.size=sizeof(q)};return call(VEX_SYS_RENDEZVOUS,&q);}
static void revoke(int fd){if(fd>=0){vex_rendezvous_req_t q={.size=sizeof(q),.operation=VEX_RENDEZVOUS_REVOKE_ENDPOINT,.resource_fd=fd};(void)call(VEX_SYS_RENDEZVOUS,&q);}}
static int answer(int reply,wp_packet *q,int fd){vex_message_attachment_req_t a={(unsigned)fd,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    vex_message_io_req_t r={reply,MSG_DONTWAIT,q,sizeof(*q),fd>=0?&a:0,fd>=0?1:0};int n=call(VEX_SYS_MESSAGE_SEND,&r);close_fd(reply);return n==(int)sizeof(*q);}
static instance *first(const char *name){for(instance *i=instances;i;i=i->next)if(wr_equal(i->spec.name,name,0))return i;return 0;}
static instance *available(const char *name){for(instance *i=instances;i;i=i->next)if(!i->connected && wr_equal(i->spec.name,name,0))return i;return 0;}
static void notify_connect(instance *i){if(i->waiting<0 || !i->connected)return;wp_packet q=i->spec;q.error=0;
    int view=attenuate(i->data,((i->spec.access&1)?2:0)|((i->spec.access&2)?1:0)|4);
    if(view<0)q.error=8;else i->delivered=1;answer(i->waiting,&q,view);close_fd(view);i->waiting=-1;}
static void destroy(instance *i){instance **p=&instances;while(*p && *p!=i)p=&(*p)->next;if(*p)*p=i->next;
    if(i->waiting>=0){wp_packet q=i->spec;q.error=109;answer(i->waiting,&q,-1);}close_fd(i->data);close_fd(i->control);if(i->sd)wr_native_free(i->sd);wr_native_free(i);}
static unsigned create(wp_packet *q,const uint8_t *sd,uint32_t uid,int *returned){
    if((q->access&~0x80003u) || (q->mode&~7u))return 50;
    if(!(q->access&3) || !q->limit || q->limit>255 || ((q->mode&2) && !(q->mode&4)))return 87;
    unsigned e=wp_policy_valid(q->sd_size?sd:0,q->sd_size);if(e)return e;
    instance *base=first(q->name);unsigned count=0;
    for(instance *i=instances;i;i=i->next)if(wr_equal(i->spec.name,q->name,0))count++;
    if(base){if(q->access&0x80000)return 5;if((base->spec.access&3)!=(q->access&3) || (base->spec.mode&4)!=(q->mode&4) || base->spec.limit!=q->limit)return 5;
        if((e=wp_authorize(base->sd,base->spec.sd_size,base->owner,uid,4)))return e;
        if(count>=q->limit)return 231;
        if(q->sd_size)return 50; /* Replacing existing security is a separate operation. */
        sd=base->sd;q->sd_size=base->spec.sd_size;
    }
    instance *i=wr_native_alloc(sizeof(*i));if(!i)return 8;memset(i,0,sizeof(*i));i->control=i->data=i->waiting=-1;i->owner=base?base->owner:uid;i->spec=*q;
    if(q->sd_size){i->sd=wr_native_alloc(q->sd_size);if(!i->sd){wr_native_free(i);return 8;}memcpy(i->sd,sd,q->sd_size);}
    int fds[2];if(pair(fds)){destroy(i);return 8;}i->control=fds[1];*returned=attenuate(fds[0],5);close_fd(fds[0]);if(*returned<0){destroy(i);return 8;}
    i->next=instances;instances=i;return 0;
}
static unsigned open_pipe(wp_packet *q,uint32_t uid,int *returned){instance *i=available(q->name),*base=first(q->name);if(!base)return 2;
    if(!q->access || (q->access&~0xc0000000u))return 87;
    unsigned e=wp_authorize(base->sd,base->spec.sd_size,base->owner,uid,q->access);if(e)return e;if(!i)return 231;
    unsigned wanted=((q->access&0x80000000u)?1:0)|((q->access&0x40000000u)?2:0);
    unsigned allowed=((i->spec.access&1)?2:0)|((i->spec.access&2)?1:0);if(wanted&~allowed)return 5;
    int fds[2];if(pair(fds))return 8;*returned=attenuate(fds[0],((wanted&1)?2:0)|((wanted&2)?1:0)|4);close_fd(fds[0]);
    if(*returned<0){close_fd(fds[1]);return 8;}i->data=fds[1];i->connected=1;q->mode=i->spec.mode;q->access=wanted;notify_connect(i);return 0;
}
static void request(int source,instance *object){
    uint8_t *frame=scratch;
    vex_message_attachment_req_t a={0,VEX_MESSAGE_OBJECT_FD,0};vex_message_auth_req_t r={.io={source,MSG_DONTWAIT,frame,65536,&a,1}};
    int n=call(VEX_SYS_MESSAGE_RECV_AUTH,&r);if(n<=0){return;}int reply=r.io.attachment_count==1?(int)a.handle:-1;
    if(n<(int)sizeof(wp_packet) || reply<0){close_fd(reply);return;}
    int confined_reply=attenuate(reply,1);close_fd(reply);reply=confined_reply;if(reply<0)return;
    wp_packet *q=(void *)frame;int returned=-1,deferred=0;unsigned error=0;
    if(q->magic!=WP_MAGIC || !r.sender.pid || !r.sender.tid || q->sd_size!=(unsigned)n-sizeof(*q) || q->name[255])error=87;
    else if(object){
        switch(q->op){case WP_CONNECT:
            if(object->connected){returned=attenuate(object->data,((object->spec.access&1)?2:0)|((object->spec.access&2)?1:0)|4);error=returned<0?8:535;object->delivered=returned>=0;}
            else if(object->waiting>=0)error=231;
            else if(q->mode&~3u)error=87;
            else if(q->mode&1)error=536;
            else {object->waiting=reply;deferred=1;}break;
        case WP_DISCONNECT:revoke(object->data);close_fd(object->data);object->data=-1;object->connected=object->delivered=0;break;
        case WP_STATE:q->access=object->connected;break;
        default:error=87;}
    }else switch(q->op){
    case WP_CREATE:if((r.sender.scope_rights&(VEX_FS_WRITE|VEX_FS_CREATE))!=(VEX_FS_WRITE|VEX_FS_CREATE))error=5;else error=create(q,frame+sizeof(*q),r.sender.uid,&returned);break;
    case WP_OPEN:error=open_pipe(q,r.sender.uid,&returned);break;
    case WP_PING:q->access=r.sender.uid;q->mode=r.sender.pid;q->limit=(unsigned)wr_pid();break;
    case WP_WAIT:{instance *base=first(q->name);if(!base)error=2;else if(available(q->name)){}
        else {wait_request *w=wr_native_alloc(sizeof(*w));if(!w)error=8;else{unsigned timeout=q->timeout?q->timeout:(base->spec.timeout?base->spec.timeout:50);w->deadline=timeout==UINT32_MAX?0:wr_ticks()+timeout;w->reply=reply;w->q=*q;w->next=waits;waits=w;deferred=1;}}break;}
    default:error=87;}
    if(!deferred){q->error=error;answer(reply,q,returned);}close_fd(returned);
}
int wr_main(uint64_t argc,char **argv){(void)argc;(void)argv;
    /* Prove the entry was confined before publication, not by cooperative seal. */
    vex_fs_root_req_t policy={.size=sizeof(policy),.operation=VEX_FS_ROOT_POLICY};if(call(VEX_SYS_FS_ROOT,&policy)!=1)return 125;
    scratch=wr_native_alloc(65536);if(!scratch)return 125;
    for(;;){struct pollfd poll[128];instance *objects[128];wait_request *waiting[128];unsigned kinds[128]={0};unsigned count=1;int timeout=-1;
        poll[0]=(struct pollfd){8,POLLIN,0};objects[0]=0;waiting[0]=0;
        for(instance *i=instances;i && count<128;i=i->next){poll[count]=(struct pollfd){i->control,POLLIN,0};objects[count]=i;waiting[count++]=0;
            if(i->waiting>=0 && count<128){poll[count]=(struct pollfd){i->waiting,0,0};objects[count]=i;waiting[count]=0;kinds[count++]=1;}}
        for(wait_request *w=waits;w && count<128;w=w->next){poll[count]=(struct pollfd){w->reply,0,0};objects[count]=0;waiting[count++]=w;}
        uint64_t now=wr_ticks();wait_request **p=&waits;
        while(*p){wait_request *w=*p;unsigned e=0;if(!first(w->q.name))e=2;else if(w->deadline && now>=w->deadline)e=121;
            if(e || available(w->q.name)){*p=w->next;w->q.error=e;answer(w->reply,&w->q,-1);wr_native_free(w);count=0;continue;}
            if(w->deadline){uint64_t left=w->deadline-now;int ms=left>INT32_MAX?INT32_MAX:(int)left;if(timeout<0 || timeout>ms)timeout=ms;}p=&w->next;}
        if(!count)continue;
        vex_poll_req_t q={poll,count,timeout};if(call(VEX_SYS_POLL,&q)<0)return 125;
        /* Process ingress before old objects; a newly created object is picked
           up next iteration. Destruction only removes its own list element. */
        for(unsigned n=0;n<count;n++){
            if(kinds[n]==2)continue;
            if(kinds[n]){if(poll[n].revents&(POLLHUP|POLLERR)){close_fd(objects[n]->waiting);objects[n]->waiting=-1;}continue;}
            if(waiting[n]){if(poll[n].revents&(POLLHUP|POLLERR)){wait_request **p=&waits;while(*p && *p!=waiting[n])p=&(*p)->next;if(*p){wait_request *w=*p;*p=w->next;close_fd(w->reply);wr_native_free(w);}}continue;}
            if(poll[n].revents&POLLIN)request(poll[n].fd,objects[n]);
            if(poll[n].revents&(POLLHUP|POLLERR)){if(!n)return 0;for(unsigned j=n+1;j<count;j++)if(objects[j]==objects[n]){objects[j]=0;kinds[j]=2;}destroy(objects[n]);}
        }
    }
}
