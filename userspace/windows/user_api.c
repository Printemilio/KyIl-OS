/* USER32 message-only windows. Window procedures execute in their owning PE
   thread; only authenticated namespace metadata lives in the shared service.
   Visible windows and cross-thread message dispatch are not claimed here. */
#include "windows.h"
#include "process.h"
#include "user_protocol.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
typedef intptr_t (WR_WIN *procedure)(void *,unsigned,uintptr_t,intptr_t);
typedef struct {uint32_t size,style;procedure proc;int32_t class_extra,window_extra;void *instance,*icon,*cursor,*brush;const uint16_t *menu,*name;void *small_icon;} class_spec;
typedef struct {void *parameter,*instance,*menu,*parent;int32_t height,width,y,x;uint32_t style,pad;const uint16_t *title,*name;uint32_t ex_style,pad2;} create_spec;
_Static_assert(sizeof(class_spec)==80 && sizeof(create_spec)==80,"AMD64 USER32 layouts");
typedef struct local_class {struct local_class *next;class_spec spec;uint32_t atom,windows;uint16_t name[256];} local_class;
typedef struct local_window {struct local_window *next;local_class *type;uint32_t id,tid,refs,closing,created;intptr_t userdata;uint8_t extra[];} local_window;
static wr_mutex gate;static int service=-1;static local_class *classes;static local_window *windows;
static int error(unsigned code){wr_current_teb()->last_error=code;return 0;}
static void close_fd(int fd){if(fd>=0)vex_syscall1(VEX_SYS_CLOSE,fd);}
static unsigned native_error(int n){switch(-n){case 12:case 24:return 8;case 13:return 5;case 14:return 998;default:return 6;}}
static unsigned exchange(int fd,wu_packet *p,int *received){if(received)*received=-1;int fds[2];vex_message_pair_req_t pair={fds,0,0};
    int n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair);if(n)return native_error(n);
    vex_message_attachment_req_t a={(unsigned)fds[1],VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    vex_message_io_req_t q={fd,0,p,sizeof(*p),&a,1};n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&q);close_fd(fds[1]);
    if(n!=(int)sizeof(*p)){close_fd(fds[0]);return n<0?native_error(n):6;}
    a=(vex_message_attachment_req_t){UINT64_MAX,VEX_MESSAGE_OBJECT_FD,0};q=(vex_message_io_req_t){fds[0],0,p,sizeof(*p),&a,1};
    n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&q);close_fd(fds[0]);
    if(n!=(int)sizeof(*p) || p->magic!=WU_MAGIC){if(n>=0 && q.attachment_count)close_fd((int)a.handle);return n<0?native_error(n):6;}
    if(q.attachment_count){if(received)*received=(int)a.handle;else close_fd((int)a.handle);}return p->error;
}
static unsigned connect_service(void){if(service>=0)return 0;
    vex_fs_root_req_t open={.size=sizeof(open),.operation=VEX_FS_ROOT_OPEN,.fd=wr_system_root(),.path="user-service",.flags=O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE};
    int image=(int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&open);if(image<0)return native_error(image);
    vex_process_resource_req_t launch={.size=sizeof(launch),.operation=VEX_PROCESS_ULTRA_OPEN,.fd=image,.status=wr_prefix_root()};
    int ingress=(int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)&launch);close_fd(image);if(ingress<0)return native_error(ingress);
    wu_packet p={.magic=WU_MAGIC,.op=WU_SESSION};int fd=-1;unsigned why=exchange(ingress,&p,&fd);close_fd(ingress);
    if(why || fd<0){close_fd(fd);return why?why:6;}service=fd;return 0;
}
static unsigned rpc(wu_packet *p){unsigned why=connect_service();if(why)return why;p->magic=WU_MAGIC;return exchange(service,p,NULL);}
/* Snapshot only through the current NUL; an inaccessible following page is
   irrelevant. No raw PE pointer enters the service or another process. */
static unsigned wide(const uint16_t *in,uint16_t *out,unsigned cap){if(!in){out[0]=0;return 0;}
    for(unsigned i=0;i<cap;i++){uintptr_t base=(uintptr_t)in;if(base>UINTPTR_MAX-i*2u || wr_memory_copy(out+i,(void *)(base+i*2u),2))return 998;
        if(!out[i])return 0;}out[cap-1]=0;return 206;}
static unsigned class_key(wu_packet *p,const uint16_t *name){if((uintptr_t)name<=0xffff){p->atom=(uint32_t)(uintptr_t)name;return p->atom?0:87;}return wide(name,p->name,256);}
static local_class *find_class(const wu_packet *p){for(local_class *c=classes;c;c=c->next){if(c->spec.instance!=(void *)(uintptr_t)p->instance)continue;
    if(p->atom){if(p->atom==c->atom)return c;}else{unsigned i=0;while(wr_unicode_case(c->name[i],0)==wr_unicode_case(p->name[i],0)){if(!c->name[i])return c;i++;}}}return NULL;}
static local_window *local(uintptr_t id){if(id>INT32_MAX)return NULL;for(local_window *w=windows;w;w=w->next)if(w->id==id)return w;return NULL;}
static void release(local_window *w){if(--w->refs)return;local_window **p=&windows;while(*p && *p!=w)p=&(*p)->next;
    if(*p)*p=w->next;w->type->windows--;wr_native_free(w);}
void wr_user_quiesce(int begin){if(begin)wr_lock(&gate);else wr_unlock(&gate);}
void wr_user_thread_exit(uint32_t tid){wr_lock(&gate);if(service>=0){wu_packet p={.op=WU_THREAD_EXIT,.tid=tid};(void)rpc(&p);}
    local_window *w=windows;while(w){local_window *next=w->next;if(w->tid==tid && !w->closing){w->closing=1;release(w);}w=next;}wr_unlock(&gate);}
static uint16_t WR_WIN RegisterClassExW(const class_spec *input){class_spec c;if(!input || wr_memory_copy(&c,input,sizeof(c)))return (uint16_t)error(998);
    if(c.size!=sizeof(c) || !c.proc || c.class_extra<0 || c.window_extra<0)return (uint16_t)error(87);
    if(c.class_extra || c.window_extra>65536 || (c.style&0x4000))return (uint16_t)error(50);
    vex_memory_view_info_t region;if(wr_memory_query((uintptr_t)c.proc,&region) || !(region.permissions&PE_X))return (uint16_t)error(998);
    wu_packet p={.op=WU_REGISTER,.instance=(uintptr_t)c.instance};unsigned why=wide(c.name,p.name,256);if(why || !p.name[0])return (uint16_t)error(why?why:87);
    local_class *fresh=wr_native_alloc(sizeof(*fresh));if(!fresh)return (uint16_t)error(8);memset(fresh,0,sizeof(*fresh));fresh->spec=c;memcpy(fresh->name,p.name,sizeof(fresh->name));
    wr_lock(&gate);why=rpc(&p);if(!why){fresh->atom=p.atom;fresh->next=classes;classes=fresh;}wr_unlock(&gate);
    if(why){wr_native_free(fresh);return (uint16_t)error(why);}return (uint16_t)p.atom;
}
static uint16_t WR_WIN RegisterClassW(const void *input){class_spec c={.size=sizeof(c)};
    if(!input || wr_memory_copy((uint8_t *)&c+8,(const uint8_t *)input+8,64) || wr_memory_copy(&c.style,input,4))return (uint16_t)error(998);
    return RegisterClassExW(&c);
}
static int WR_WIN UnregisterClassW(const uint16_t *name,void *instance){wu_packet p={.op=WU_UNREGISTER,.instance=(uintptr_t)instance};unsigned why=class_key(&p,name);if(why)return error(why);
    wr_lock(&gate);local_class *c=find_class(&p);if(!c)why=1411;else if(c->windows)why=1412;else{why=rpc(&p);if(!why){local_class **link=&classes;while(*link!=c)link=&(*link)->next;*link=c->next;wr_native_free(c);}}wr_unlock(&gate);return why?error(why):1;
}
static int WR_WIN DestroyWindow(void *handle);
static void *WR_WIN CreateWindowExW(unsigned ex,const uint16_t *name,const uint16_t *title,unsigned style,int x,int y,int width,int height,void *parent,void *menu,void *instance,void *parameter){
    if((intptr_t)parent!=-3)return (void *)(uintptr_t)error(50);
    if(menu)return (void *)(uintptr_t)error(50);
    wu_packet p={.op=WU_CREATE,.parent=WU_MESSAGE,.instance=(uintptr_t)instance};unsigned why=class_key(&p,name);if(!why)why=wide(title,p.title,512);if(why)return (void *)(uintptr_t)error(why);
    wr_lock(&gate);local_class *c=find_class(&p);local_window *w=NULL;if(!c)why=1407;
    else{w=wr_native_alloc(sizeof(*w)+(size_t)c->spec.window_extra);if(!w)why=8;
        else {memset(w,0,sizeof(*w)+(size_t)c->spec.window_extra);why=rpc(&p);if(!why){w->id=p.id;w->tid=wr_tid();w->type=c;w->refs=2;w->next=windows;windows=w;c->windows++;}}}
    wr_unlock(&gate);if(why){if(w)wr_native_free(w);return (void *)(uintptr_t)error(why);}
    void *h=(void *)(uintptr_t)w->id;create_spec spec={parameter,instance,menu,parent,height,width,y,x,style,0,title,name,ex,0};
    intptr_t nc=c->spec.proc(h,0x81,0,(intptr_t)&spec);int create=0;
    wr_lock(&gate);if(!w->closing && nc){create=1;w->created=1;}wr_unlock(&gate);
    intptr_t result=create?c->spec.proc(h,1,0,(intptr_t)&spec):-1;
    wr_lock(&gate);int alive=!w->closing;wr_unlock(&gate);
    if(alive && (!nc || result==-1)){DestroyWindow(h);alive=0;}
    wr_lock(&gate);release(w);wr_unlock(&gate);return alive?h:NULL;
}
static int WR_WIN DestroyWindow(void *handle){wr_lock(&gate);local_window *w=local((uintptr_t)handle);unsigned why=0;
    if(!w){wu_packet p={.op=WU_QUERY,.id=(uint32_t)(uintptr_t)handle};why=(uintptr_t)handle>INT32_MAX?1400:rpc(&p);if(!why)why=5;}
    else if(w->closing)why=1400;else if(w->tid!=wr_tid())why=5;else{w->closing=1;w->refs++;}
    wr_unlock(&gate);if(why)return error(why);
    if(w->created)w->type->spec.proc(handle,2,0,0);w->type->spec.proc(handle,0x82,0,0);
    wr_lock(&gate);wu_packet p={.op=WU_DESTROY,.id=w->id};why=rpc(&p);release(w);release(w);wr_unlock(&gate);return why?error(why):1;
}
static void *WR_WIN FindWindowExW(void *parent,void *after,const uint16_t *name,const uint16_t *title){
    if((uintptr_t)after>INT32_MAX || ((uintptr_t)parent>INT32_MAX && (intptr_t)parent!=-3))return (void *)(uintptr_t)error(1400);
    wu_packet p={.op=WU_FIND,.parent=(intptr_t)parent==-3?WU_MESSAGE:(uint32_t)(uintptr_t)parent,.after=(uint32_t)(uintptr_t)after,.has_class=name!=NULL,.has_title=title!=NULL};
    unsigned why=name?class_key(&p,name):0;if(!why && title)why=wide(title,p.title,512);
    if(!why){wr_lock(&gate);why=rpc(&p);wr_unlock(&gate);}
    static unsigned traced;if(__atomic_fetch_add(&traced,1,__ATOMIC_RELAXED)<16)wr_trace_contract("FindWindowExW: error parent result site",why,p.parent,p.id,(uintptr_t)__builtin_return_address(0));
    if(why)return (void *)(uintptr_t)error(why);return (void *)(uintptr_t)p.id;
}
static unsigned query(void *handle,wu_packet *p){if((uintptr_t)handle>INT32_MAX || !handle)return 1400;p->op=WU_QUERY;p->id=(uint32_t)(uintptr_t)handle;wr_lock(&gate);unsigned why=rpc(p);wr_unlock(&gate);return why;}
static int WR_WIN IsWindow(void *handle){wu_packet p={0};return !query(handle,&p);}
static unsigned WR_WIN GetWindowThreadProcessId(void *handle,unsigned *pid){wu_packet p={0};unsigned why=query(handle,&p);
    if(!why && pid && wr_memory_copy(pid,&p.pid,4))why=998;return why?(unsigned)error(why):p.tid;}
static int copy_string(const uint16_t *s,uint16_t *out,int cap){if(cap<=0)return error(87);unsigned n=0;while(s[n] && n<(unsigned)cap-1)n++;
    if(wr_memory_copy(out,s,n*2) || wr_memory_copy(out+n,&(uint16_t){0},2))return error(998);return (int)n;}
static int WR_WIN GetClassNameW(void *h,uint16_t *out,int cap){wu_packet p={0};unsigned why=query(h,&p);return why?error(why):copy_string(p.name,out,cap);}
static int stored_text(void *h,uint16_t *out,int cap){wu_packet p={0};unsigned why=query(h,&p);return why?error(why):copy_string(p.title,out,cap);}
static int stored_length(void *h){wu_packet p={0};unsigned why=query(h,&p);if(why)return error(why);unsigned n=0;while(p.title[n])n++;return (int)n;}
static intptr_t text_message(void *h,unsigned message,uintptr_t wp,intptr_t lp){
    wr_lock(&gate);local_window *w=local((uintptr_t)h);unsigned why=0;
    if(w){if(w->closing)why=1400;else if(w->tid!=wr_tid())why=50;else w->refs++;}
    wr_unlock(&gate);if(why)return error(why);
    if(!w)return message==0xd?stored_text(h,(uint16_t *)lp,(int)wp):stored_length(h);
    intptr_t value=w->type->spec.proc(h,message,wp,lp);wr_lock(&gate);release(w);wr_unlock(&gate);return value;
}
static int WR_WIN GetWindowTextW(void *h,uint16_t *out,int cap){return cap>0?(int)text_message(h,0xd,(uintptr_t)cap,(intptr_t)out):0;}
static int WR_WIN GetWindowTextLengthW(void *h){return (int)text_message(h,0xe,0,0);}
static int store_title(void *h,const uint16_t *text){wu_packet p={.op=WU_TITLE,.id=(uint32_t)(uintptr_t)h};if((uintptr_t)h>INT32_MAX)return error(1400);
    unsigned why=wide(text,p.title,512);if(!why){wr_lock(&gate);why=rpc(&p);wr_unlock(&gate);}return why?error(why):1;}
static int WR_WIN SetWindowTextW(void *h,const uint16_t *text){wr_lock(&gate);local_window *w=local((uintptr_t)h);unsigned why=0;
    if(!w || w->closing)why=1400;else if(w->tid!=wr_tid())why=50;else w->refs++;
    wr_unlock(&gate);if(why)return error(why);intptr_t result=w->type->spec.proc(h,0xc,0,(intptr_t)text);
    wr_lock(&gate);release(w);wr_unlock(&gate);return result!=0;
}
static intptr_t WR_WIN DefWindowProcW(void *h,unsigned message,uintptr_t w,intptr_t l){switch(message){case 0x81:return 1;
    case 0x10:DestroyWindow(h);return 0;case 0xc:return store_title(h,(const uint16_t *)l);
    case 0xd:return stored_text(h,(uint16_t *)l,(int)w);case 0xe:return stored_length(h);case 0x11:return 1;default:return 0;}}
static intptr_t WR_WIN CallWindowProcW(procedure proc,void *h,unsigned message,uintptr_t w,intptr_t l){vex_memory_view_info_t r;
    if(!proc || wr_memory_query((uintptr_t)proc,&r) || !(r.permissions&PE_X))return error(998);return proc(h,message,w,l);}
static intptr_t window_long(void *handle,int index,int set,intptr_t value){wr_lock(&gate);local_window *w=local((uintptr_t)handle);intptr_t result=0;unsigned why=0;
    if(!w || w->closing)why=1400;else if(w->tid!=wr_tid())why=5;
    else if(index==-21){result=w->userdata;if(set)w->userdata=value;}
    else if(index==-6){result=(intptr_t)w->type->spec.instance;if(set)why=50;}
    else if(index==-4){result=(intptr_t)w->type->spec.proc;if(set)why=50;}
    else if(index>=0 && (unsigned)w->type->spec.window_extra>=sizeof(value) && (unsigned)index<=(unsigned)w->type->spec.window_extra-sizeof(value)){
        memcpy(&result,w->extra+index,sizeof(result));if(set)memcpy(w->extra+index,&value,sizeof(value));}
    else why=1413;wr_unlock(&gate);return why?error(why):result;
}
static intptr_t WR_WIN GetWindowLongPtrW(void *h,int index){return window_long(h,index,0,0);}
static intptr_t WR_WIN SetWindowLongPtrW(void *h,int index,intptr_t value){return window_long(h,index,1,value);}
void *wr_user_exports(const char *name){
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(RegisterClassExW);E(RegisterClassW);E(UnregisterClassW);E(CreateWindowExW);E(DestroyWindow);E(FindWindowExW);
    E(IsWindow);E(GetWindowThreadProcessId);E(GetClassNameW);E(GetWindowTextW);E(GetWindowTextLengthW);E(SetWindowTextW);
    E(DefWindowProcW);E(CallWindowProcW);E(GetWindowLongPtrW);E(SetWindowLongPtrW);
#undef E
    return NULL;
}
