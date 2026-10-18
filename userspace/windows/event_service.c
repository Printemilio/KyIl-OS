/* Anonymous Windows event state, in a capability-confined userspace service.
   No Windows synchronization rules or object IDs enter the native kernel. */
#include "runtime.h"
#include "event_protocol.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
typedef struct pending {struct pending *next;int reply;uint64_t deadline;} pending;
typedef struct object {struct object *next;int control;unsigned manual,signaled;pending *first,*last;} object;
static object *objects;
static uint8_t request_bytes[65536];
static int call(unsigned n,void *p){return (int)(int64_t)vex_syscall1(n,(uintptr_t)p);}
static void close_fd(int fd){if(fd>=0)vex_syscall1(VEX_SYS_CLOSE,fd);}
static int attenuate(int fd,unsigned rights){vex_rendezvous_req_t q={.size=sizeof(q),.operation=VEX_RENDEZVOUS_ATTENUATE,.resource_fd=fd,.rights=rights};return call(VEX_SYS_RENDEZVOUS,&q);}
static int answer(int reply,we_packet *p,int fd){vex_message_attachment_req_t a={(unsigned)fd,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    vex_message_io_req_t q={reply,MSG_DONTWAIT,p,sizeof(*p),fd>=0?&a:NULL,fd>=0?1:0};int n=call(VEX_SYS_MESSAGE_SEND,&q);close_fd(reply);return n==(int)sizeof(*p);}
static void remove_wait(object *o,pending *w){pending **p=&o->first,*previous=NULL;while(*p && *p!=w){previous=*p;p=&(*p)->next;}
    if(*p){*p=w->next;if(o->last==w)o->last=previous;}close_fd(w->reply);wr_native_free(w);}
static void destroy(object *o){object **p=&objects;while(*p && *p!=o)p=&(*p)->next;if(*p)*p=o->next;
    while(o->first){pending *w=o->first;o->first=w->next;we_packet q={.error=6};answer(w->reply,&q,-1);wr_native_free(w);}close_fd(o->control);wr_native_free(o);}
static void dispatch(object *o){while(o->signaled && o->first){pending *w=o->first;o->first=w->next;if(!o->first)o->last=NULL;
    we_packet q={.operation=WE_WAIT};int delivered=answer(w->reply,&q,-1);wr_native_free(w);if(delivered && !o->manual)o->signaled=0;}}
static void request(int source,object *o){we_packet p;vex_message_attachment_req_t a[32];
    for(unsigned i=0;i<32;i++)a[i]=(vex_message_attachment_req_t){UINT64_MAX,VEX_MESSAGE_OBJECT_FD,0};
    /* Native receive never truncates. Drain the bounded maximum message and
       all attachments even on malformed input, avoiding an EMSGSIZE spin. */
    vex_message_io_req_t r={source,MSG_DONTWAIT,request_bytes,sizeof(request_bytes),a,32};int n=call(VEX_SYS_MESSAGE_RECV,&r);
    if(n!=(int)sizeof(p) || r.attachment_count!=1){if(n>=0)for(unsigned i=0;i<r.attachment_count;i++)close_fd((int)a[i].handle);return;}
    memcpy(&p,request_bytes,sizeof(p));int reply=attenuate((int)a[0].handle,1);close_fd((int)a[0].handle);if(reply<0)return;
    p.error=p.result=0;int returned=-1,deferred=0;
    if(!o){if(p.operation!=WE_CREATE || p.manual>1 || p.initial>1 || p.timeout)p.error=87;
        else {object *fresh=wr_native_alloc(sizeof(*fresh));if(!fresh)p.error=8;else{
            int fds[2];vex_message_pair_req_t pair={fds,0,0};if(call(VEX_SYS_MESSAGE_PAIR,&pair)){wr_native_free(fresh);p.error=8;}
            else {returned=attenuate(fds[0],5);close_fd(fds[0]);if(returned<0){close_fd(fds[1]);wr_native_free(fresh);p.error=8;}
                else {*fresh=(object){.next=objects,.control=fds[1],.manual=p.manual,.signaled=p.initial};objects=fresh;}}}}
    }else if(p.manual || p.initial || (p.operation!=WE_WAIT && p.timeout))p.error=87;
    else switch(p.operation){case WE_SET:o->signaled=1;dispatch(o);break;
    case WE_RESET:o->signaled=0;break;
    case WE_WAIT:if(o->signaled){if(!o->manual)o->signaled=0;}
        else if(!p.timeout)p.result=258;
        else {pending *w=wr_native_alloc(sizeof(*w));if(!w)p.error=8;else{uint64_t now=wr_ticks();
            *w=(pending){.reply=reply,.deadline=p.timeout==UINT32_MAX?0:now>UINT64_MAX-p.timeout?UINT64_MAX:now+p.timeout};
            if(o->last)o->last->next=w;else o->first=w;o->last=w;deferred=1;}}break;
    default:p.error=87;}
    if(!deferred)answer(reply,&p,returned);close_fd(returned);
}
int wr_main(uint64_t argc,char **argv){(void)argc;(void)argv;unsigned cursor=0;
    for(;;){uint64_t now=wr_ticks();int timeout=-1;
        for(object *o=objects;o;o=o->next){pending *w=o->first;while(w){pending *next=w->next;
            if(w->deadline && now>=w->deadline){we_packet p={.operation=WE_WAIT,.result=258};answer(w->reply,&p,-1);w->reply=-1;remove_wait(o,w);}
            else if(w->deadline){uint64_t left=w->deadline-now;int ms=left>INT32_MAX?INT32_MAX:(int)left;if(timeout<0 || ms<timeout)timeout=ms;}w=next;}}
        struct pollfd fds[128];object *owner[128];pending *waiter[128];unsigned count=1;
        fds[0]=(struct pollfd){8,POLLIN,0};owner[0]=NULL;waiter[0]=NULL;
        for(object *o=objects;o && count<128;o=o->next){fds[count]=(struct pollfd){o->control,POLLIN,0};owner[count]=o;waiter[count++]=NULL;
            for(pending *w=o->first;w && count<128;w=w->next){fds[count]=(struct pollfd){w->reply,0,0};owner[count]=o;waiter[count++]=w;}}
        vex_poll_req_t q={fds,count,timeout};if(call(VEX_SYS_POLL,&q)<0)return 125;
        for(unsigned step=0;step<count;step++){unsigned i=(cursor+step)%count;if(!fds[i].revents)continue;cursor=(i+1)%count;
            if(waiter[i]){if(fds[i].revents&(POLLHUP|POLLERR))remove_wait(owner[i],waiter[i]);}
            else {if(fds[i].revents&POLLIN)request(fds[i].fd,owner[i]);
                if(fds[i].revents&(POLLHUP|POLLERR)){if(!i)return 0;destroy(owner[i]);}}
            /* Requests can free wait records. Rebuild the poll snapshot before
               inspecting another pointer, rather than dereferencing stale nodes. */
            break;
        }
    }
}
