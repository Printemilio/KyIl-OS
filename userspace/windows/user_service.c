/* The Windows message-window namespace is userspace policy scoped by the
   caller's existing prefix capability. The kernel only authenticates messages
   and owns endpoints. No window procedure address crosses this service. */
#include "runtime.h"
#include "user_protocol.h"
#include "nls_data.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
typedef struct klass {struct klass *next;uint32_t atom;uint64_t instance;uint16_t name[256];} klass;
typedef struct window {struct window *next;klass *type;uint32_t id,tid;uint16_t title[512];} window;
typedef struct session {struct session *next;int fd;uint32_t pid;klass *classes;window *windows;} session;
static session *sessions;static uint32_t next_window=0x10000,next_atom=0xc000,window_count;
static uint8_t scratch[65536];
static int call(unsigned n,void *p){return (int)(int64_t)vex_syscall1(n,(uintptr_t)p);}
static void close_fd(int fd){if(fd>=0)vex_syscall1(VEX_SYS_CLOSE,fd);}
static int attenuate(int fd,unsigned rights){vex_rendezvous_req_t q={.size=sizeof(q),.operation=VEX_RENDEZVOUS_ATTENUATE,.resource_fd=fd,.rights=rights};return call(VEX_SYS_RENDEZVOUS,&q);}
static unsigned fold(unsigned c){unsigned lo=0,hi=sizeof(nls_case)/sizeof(*nls_case);while(lo<hi){unsigned at=lo+(hi-lo)/2;
    if(c<nls_case[at].source)hi=at;else if(c>nls_case[at].source)lo=at+1;else return nls_case[at].lower;}return c;}
static int same(const uint16_t *a,const uint16_t *b){for(;;a++,b++){if(fold(*a)!=fold(*b))return 0;if(!*a)return 1;}}
static klass *class_find(session *s,wu_packet *p){
    for(klass *k=s->classes;k;k=k->next){
        if((p->atom?k->atom==p->atom:same(k->name,p->name)) && k->instance==p->instance)return k;
    }return NULL;
}
static window *window_find(uint32_t id,session **owner){
    for(session *s=sessions;s;s=s->next){for(window *w=s->windows;w;w=w->next){
        if(w->id==id){if(owner)*owner=s;return w;}
    }}return NULL;
}
static void window_drop(session *s,window *w){window **p=&s->windows;while(*p && *p!=w)p=&(*p)->next;if(*p){*p=w->next;window_count--;wr_native_free(w);}}
static void destroy(session *s){session **p=&sessions;while(*p && *p!=s)p=&(*p)->next;if(*p)*p=s->next;
    while(s->windows)window_drop(s,s->windows);while(s->classes){klass *k=s->classes;s->classes=k->next;wr_native_free(k);}close_fd(s->fd);wr_native_free(s);}
static int answer(int reply,wu_packet *p,int fd){vex_message_attachment_req_t a={(unsigned)fd,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    vex_message_io_req_t q={reply,MSG_DONTWAIT,p,sizeof(*p),fd>=0?&a:NULL,fd>=0?1:0};int n=call(VEX_SYS_MESSAGE_SEND,&q);close_fd(reply);return n==(int)sizeof(*p);}
static void request(int source,session *s){vex_message_attachment_req_t a[32];for(unsigned i=0;i<32;i++)a[i]=(vex_message_attachment_req_t){UINT64_MAX,VEX_MESSAGE_OBJECT_FD,0};
    vex_message_auth_req_t r={.io={source,MSG_DONTWAIT,scratch,sizeof(scratch),a,32}};int n=call(VEX_SYS_MESSAGE_RECV_AUTH,&r);
    if(n!=(int)sizeof(wu_packet) || r.io.attachment_count!=1){if(n>=0)for(unsigned i=0;i<r.io.attachment_count;i++)close_fd((int)a[i].handle);return;}
    wu_packet p;memcpy(&p,scratch,sizeof(p));int reply=attenuate((int)a[0].handle,1);close_fd((int)a[0].handle);if(reply<0)return;
    int returned=-1;session *fresh=NULL;p.error=0;
    if(p.magic!=WU_MAGIC || !r.sender.pid || !r.sender.tid || p.name[255] || p.title[511])p.error=87;
    else if(!s){if(p.op!=WU_SESSION)p.error=87;else{
        fresh=wr_native_alloc(sizeof(*fresh));int fds[2];vex_message_pair_req_t pair={fds,0,0};
        if(!fresh || call(VEX_SYS_MESSAGE_PAIR,&pair)){if(fresh)wr_native_free(fresh);fresh=NULL;p.error=8;}
        else {returned=attenuate(fds[0],5);close_fd(fds[0]);if(returned<0){close_fd(fds[1]);wr_native_free(fresh);fresh=NULL;p.error=8;}
            else {*fresh=(session){.next=sessions,.fd=fds[1],.pid=r.sender.pid};sessions=fresh;}}}}
    else if(s->pid!=r.sender.pid)p.error=5; /* A stolen/delegated session is not an identity. */
    else switch(p.op){
    case WU_REGISTER:{if(!p.name[0]){p.error=87;break;}p.atom=0;if(class_find(s,&p)){p.error=1410;break;}
        if(next_atom>0xffff){p.error=8;break;}klass *k=wr_native_alloc(sizeof(*k));if(!k){p.error=8;break;}
        *k=(klass){.next=s->classes,.atom=next_atom++,.instance=p.instance};memcpy(k->name,p.name,sizeof(k->name));s->classes=k;p.atom=k->atom;break;}
    case WU_UNREGISTER:{klass *k=class_find(s,&p);if(!k){p.error=1411;break;}for(window *w=s->windows;w;w=w->next)if(w->type==k){p.error=1412;break;}
        if(!p.error){klass **at=&s->classes;while(*at!=k)at=&(*at)->next;*at=k->next;wr_native_free(k);}break;}
    case WU_CREATE:{klass *k=class_find(s,&p);if(!k){p.error=1407;break;}if(p.parent!=WU_MESSAGE){p.error=50;break;}
        if(window_count>=4096 || next_window>0x7ffffffb){p.error=8;break;}window *w=wr_native_alloc(sizeof(*w));if(!w){p.error=8;break;}
        *w=(window){.next=s->windows,.type=k,.tid=r.sender.tid,.id=next_window};next_window+=4;window_count++;memcpy(w->title,p.title,sizeof(w->title));s->windows=w;p.id=w->id;break;}
    case WU_FIND:{if(p.parent && p.parent!=WU_MESSAGE){if(!window_find(p.parent,NULL))p.error=1400;p.id=0;break;}
        window *best=NULL;window *after=p.after?window_find(p.after,NULL):NULL;if(p.after && !after){p.error=1400;p.id=0;break;}
        /* Message-only windows have no display Z order. Keep a deterministic
           newest-first namespace order for FindWindowEx continuation. */
        for(session *v=sessions;v;v=v->next)for(window *w=v->windows;w;w=w->next)
            if((!after || w->id<after->id) && (!p.has_class || (p.atom?w->type->atom==p.atom:same(w->type->name,p.name))) &&
               (!p.has_title || same(w->title,p.title)) && (!best || w->id>best->id))best=w;
        p.id=best?best->id:0;break;}
    case WU_QUERY:case WU_TITLE:case WU_DESTROY:{session *owner=NULL;window *w=window_find(p.id,&owner);if(!w){p.error=1400;break;}
        if(p.op!=WU_QUERY && (owner!=s || w->tid!=r.sender.tid)){p.error=5;break;}
        if(p.op==WU_DESTROY)window_drop(s,w);
        else {if(p.op==WU_TITLE)memcpy(w->title,p.title,sizeof(w->title));p.pid=owner->pid;p.tid=w->tid;p.atom=w->type->atom;
            memcpy(p.title,w->title,sizeof(p.title));memcpy(p.name,w->type->name,sizeof(p.name));}break;}
    case WU_THREAD_EXIT:{/* Cleanup may be initiated by the owning process's reaper. */
        window *w=s->windows;while(w){window *next=w->next;if(w->tid==p.tid)window_drop(s,w);w=next;}break;}
    default:p.error=87;}
    if(!answer(reply,&p,returned)){
        if(fresh)destroy(fresh);
        else if(s && !p.error && p.op==WU_CREATE){window *w=window_find(p.id,NULL);if(w)window_drop(s,w);}
        else if(s && !p.error && p.op==WU_REGISTER){klass **k=&s->classes;while(*k && (*k)->atom!=p.atom)k=&(*k)->next;
            if(*k){klass *dead=*k;*k=dead->next;wr_native_free(dead);}}
    }close_fd(returned);
}
int wr_main(uint64_t argc,char **argv){(void)argc;(void)argv;unsigned cursor=0;
    vex_fs_root_req_t policy={.size=sizeof(policy),.operation=VEX_FS_ROOT_POLICY};if(call(VEX_SYS_FS_ROOT,&policy)!=1)return 125;
    for(;;){struct pollfd fds[128];session *owner[128];unsigned count=1;fds[0]=(struct pollfd){8,POLLIN,0};owner[0]=NULL;
        for(session *s=sessions;s && count<128;s=s->next){fds[count]=(struct pollfd){s->fd,POLLIN,0};owner[count++]=s;}
        vex_poll_req_t q={fds,count,-1};if(call(VEX_SYS_POLL,&q)<0)return 125;
        int retired=0;for(unsigned i=1;i<count;i++)if(fds[i].revents&(POLLHUP|POLLERR)){destroy(owner[i]);retired=1;break;}
        if(retired)continue; /* Drop dead owners before answering discovery. */
        for(unsigned j=0;j<count;j++){unsigned i=(cursor+j)%count;if(!fds[i].revents)continue;cursor=(i+1)%count;
            if(fds[i].revents&POLLIN)request(fds[i].fd,owner[i]);
            if(fds[i].revents&(POLLHUP|POLLERR)){if(!i)return 0;destroy(owner[i]);}break;}
    }
}
