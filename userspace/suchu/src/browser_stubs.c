/* Suchu/WebKit interop. This file has no chrome, tab-selection, URL editor,
 * hit-test or navigation policy. Those live in browser.suchu. */
#define _GNU_SOURCE
#define VEX_HOSTED_BUILD 1
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <caml/mlvalues.h>
#include <caml/memory.h>
#include <caml/alloc.h>
#include <caml/fail.h>
#include "raylib.h"
#include "rlgl.h"
#include "vex_syscall.h"
#include "vex_browser_protocol.h"
#include "vex_input_observer.h"
#include "vex_surface_pixels.h"
#include <unos/browser_navigation.h>

#define NOWAIT 0x40u
#define RAW_MAX 256u
#define SHM_MAGIC 0x56455842u
#ifndef VEX_DESKTOP_TRACE
#define VEX_DESKTOP_TRACE 0
#endif
static int control=-1,child_control=-1,diagnostics=-1,child=-1;
static uint64_t shm_handle,shm_address;
static int transport_error;
static vex_app_window_event_t raw[RAW_MAX];
static unsigned raw_count;
static vex_download_state_t downloads[VEX_BROWSER_MAX_DOWNLOADS];
static void log_text(const char *s){vex_syscall1(VEX_SYS_DEBUG_LOG,(uintptr_t)s);}
static uint64_t shutdown_clock(void){
#if VEX_DESKTOP_TRACE
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
#else
    return 0;
#endif
}
static void shutdown_trace(const char *stage,uint64_t start){
#if VEX_DESKTOP_TRACE
    char line[128];snprintf(line,sizeof(line),"Suchu shutdown: %s us=%llu\n",stage,(unsigned long long)(shutdown_clock()-start));log_text(line);
#else
    (void)stage;(void)start;
#endif
}
static void discard_attachment(const vex_message_attachment_req_t *a){
    if(a->type==VEX_MESSAGE_OBJECT_FD)vex_syscall1(VEX_SYS_CLOSE,a->handle);
    if(a->type==VEX_MESSAGE_OBJECT_SURFACE)vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,a->handle);
}
#include "webkit_session.h"
typedef struct {uint32_t magic,width,height,stride,frame_seq,cmd_seq,cmd_seq_ack,reserved;} bootstrap;
static struct page {
    uint32_t id;int fd;
    Texture2D texture;
    unsigned char *staging;
    size_t staging_bytes;
    uint64_t mapped_cap,mapped_addr,mapped_bytes,mapped_backing,mapped_object;
    uint64_t cap;
    vex_browser_frame frame;
    int ack_pending;
} pages[VEX_BROWSER_MAX_TABS];
static struct page *page_for(uint32_t id){
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)if(pages[i].id==id)return &pages[i];
    return NULL;
}
static int release_view(struct page *p){
    if(p->mapped_addr){
        vex_surface_unmap_req_t r={.surface_handle=p->mapped_cap,.mapped_addr=p->mapped_addr,
            .length=p->mapped_bytes,.access=VEX_SURFACE_MAP_READ};
        if((int64_t)vex_syscall1(VEX_SYS_SURFACE_UNMAP,(uintptr_t)&r))return -1;
    }
    if(p->mapped_cap)vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,p->mapped_cap);
    p->mapped_addr=p->mapped_cap=0;return 0;
}
static void release_page(struct page *p){
    if(p->texture.id){rlDrawRenderBatchActive();UnloadTexture(p->texture);}
    free(p->staging);
    if(release_view(p))log_text("Suchu browser: Surface view unmap failed during cleanup\n");
    if(p->cap)vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,p->cap);
    if(p->fd>=0)vex_syscall1(VEX_SYS_CLOSE,p->fd);
    *p=(struct page){.fd=-1};
}
static void observe(const vex_app_window_event_t *e,void *unused){
    (void)unused;
    if(raw_count && e->type==VEX_APP_EVENT_POINTER && !(int64_t)e->w &&
       raw[raw_count-1].type==e->type && !raw[raw_count-1].w &&
       raw[raw_count-1].flags==e->flags){raw[raw_count-1]=*e;return;}
    if(raw_count==RAW_MAX){transport_error=1;return;}
    raw[raw_count++]=*e;
}
static void close_transport(void){
    uint64_t mark=shutdown_clock();
    vex_raylib_set_input_observer(NULL,NULL);
    if(child>0){
        if(control>=0){
            vex_browser_control msg={.magic=VEX_BROWSER_CONTROL,.operation=VEX_BROWSER_SHUTDOWN,.id=1};
            vex_message_io_req_t send={control,NOWAIT,&msg,sizeof(msg),NULL,0};
            (void)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send);
        }
        /* HUP is an existing WebKit shutdown source. Close endpoints rather
         * than retaining a stalled frame's backpressure during teardown. */
        for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)release_page(&pages[i]);
        shutdown_trace("pages/textures/views",mark);mark=shutdown_clock();
        if(control>=0){vex_syscall1(VEX_SYS_CLOSE,control);control=-1;}
        int status=0;vex_process_wait_req_t w={child,&status,VEX_PROCESS_WAIT_NOHANG};
        struct timespec start,now;clock_gettime(CLOCK_MONOTONIC,&start);
        for(;;){
            int64_t result=(int64_t)vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&w);
            if(result>0 || result==-10)break;
            clock_gettime(CLOCK_MONOTONIC,&now);
            if(now.tv_sec-start.tv_sec>=3){
                vex_syscall1(VEX_SYS_PROCESS_KILL,(uint32_t)child);
                w.flags=0;(void)vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&w);break;
            }
            vex_syscall1(VEX_SYS_USLEEP,10000);
        }
        child=-1;
        shutdown_trace("child-handshake/wait",mark);mark=shutdown_clock();
    }
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)release_page(&pages[i]);
    if(control>=0)vex_syscall1(VEX_SYS_CLOSE,control);
    if(child_control>=0)vex_syscall1(VEX_SYS_CLOSE,child_control);
    if(diagnostics>=0)vex_syscall1(VEX_SYS_CLOSE,diagnostics);
    control=child_control=diagnostics=-1;
    if(shm_address){vex_memory_req_t unmap={.addr=(void *)(uintptr_t)shm_address,.length=sizeof(bootstrap)};
        (void)vex_syscall1(VEX_SYS_MUNMAP,(uintptr_t)&unmap);shm_address=0;}
    if(shm_handle){vex_syscall1(VEX_SYS_SHM_CLOSE,shm_handle);shm_handle=0;}
    raw_count=0;
    shutdown_trace("endpoints/bootstrap",mark);
}
CAMLprim value kyil_browser_open(value unit){
    CAMLparam1(unit);
    if(control>=0)caml_failwith("WebKit transport already open");
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)pages[i]=(struct page){.fd=-1};
    pages[0].id=1; /* WebKit's existing initial page, not a second CREATE. */
    memset(downloads,0,sizeof(downloads));
    transport_error=0;
    if(prepare_webkit_runtime())caml_failwith("WebKit runtime directories unavailable");
    int pair[2];vex_message_pair_req_t pr={pair,0,0};
    if((int64_t)vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pr))goto failed;
    control=pair[0];child_control=pair[1];
    vex_shm_create_req_t create={.name="vex_webkit_fb",.size=sizeof(bootstrap),.prot=PROT_READ|PROT_WRITE};
    if((int64_t)vex_syscall1(VEX_SYS_SHM_CREATE,(uintptr_t)&create))goto failed;
    shm_handle=create.handle;
    vex_shm_map_req_t map={.handle=shm_handle,.length=sizeof(bootstrap),.prot=PROT_READ|PROT_WRITE};
    if((int64_t)vex_syscall1(VEX_SYS_SHM_MAP,(uintptr_t)&map))goto failed;
    shm_address=(uintptr_t)map.mapped_addr;
    *(bootstrap *)(uintptr_t)shm_address=(bootstrap){SHM_MAGIC,1280,720,1280,0,0,0,(uint32_t)child_control};
    child=webkit_session_spawn(control,&child_control,&diagnostics);
    if(child<0)goto failed;
    vex_raylib_set_input_observer(observe,NULL);
    log_text("Suchu browser: native transport started\n");
    CAMLreturn(Val_unit);
failed:
    close_transport();caml_failwith("Cannot start native WebKit transport");
}
CAMLprim value kyil_browser_close(value unit){CAMLparam1(unit);close_transport();CAMLreturn(Val_unit);}
CAMLprim value kyil_browser_time(value unit){
    CAMLparam1(unit);struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    CAMLreturn(Val_long((uint64_t)t.tv_sec*1000+t.tv_nsec/1000000));
}
CAMLprim value kyil_browser_normalize(value input){
    CAMLparam1(input);char uri[VEX_BROWSER_URI_SIZE];
    if(caml_string_length(input)>=sizeof(uri) || strlen(String_val(input))!=caml_string_length(input) ||
       browser_navigation_normalize(String_val(input),uri,sizeof(uri)))caml_invalid_argument("Invalid or oversized navigation address");
    CAMLreturn(caml_copy_string(uri));
}
static int send_input(struct page *p,const vex_viewport_event_t *event){
    if(!p || p->fd<0)return -11;
    /* End the borrowed view BEFORE authorizing a producer resize. Raylib's
     * copied texture stays valid through that resize and until the next frame. */
    if(event->type==VEX_VIEWPORT_CONFIGURE && release_view(p))return -5;
    vex_browser_input input={VEX_BROWSER_INPUT,*event};
    vex_message_io_req_t io={p->fd,NOWAIT,&input,sizeof(input),NULL,0};
    int64_t n=(int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&io);
    return n==sizeof(input)?0:(n<0?(int)n:-5);
}
CAMLprim value kyil_browser_command(value op,value id,value uri){
    CAMLparam3(op,id,uri);
    if(control<0 || Long_val(id)<=0 || Long_val(id)>UINT32_MAX)caml_invalid_argument("browser page identity");
    if(caml_string_length(uri)>=VEX_BROWSER_URI_SIZE ||
       strlen(String_val(uri))!=caml_string_length(uri))caml_invalid_argument("browser URI size/NUL");
    vex_browser_control msg={.magic=VEX_BROWSER_CONTROL,.operation=Int_val(op),.id=Long_val(id)};
    if(msg.operation<VEX_BROWSER_CREATE || msg.operation>VEX_BROWSER_STOP){
        if(msg.operation!=VEX_BROWSER_PROBE && msg.operation!=VEX_BROWSER_SHUTDOWN &&
           msg.operation!=VEX_BROWSER_DOWNLOAD_CANCEL)caml_invalid_argument("browser operation");
    }
    struct page *created=NULL;
    if(msg.operation==VEX_BROWSER_CREATE){
        if(page_for(msg.id))caml_invalid_argument("duplicate page identity");
        for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)if(!pages[i].id){created=&pages[i];break;}
        if(!created)CAMLreturn(Val_int(-12));
    }
    strcpy(msg.state.uri,String_val(uri));
    vex_message_io_req_t io={control,NOWAIT,&msg,sizeof(msg),NULL,0};
    int64_t n=(int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&io);
    if(n==sizeof(msg)){
        if(created)created->id=msg.id;
        if(msg.operation==VEX_BROWSER_CLOSE){struct page *p=page_for(msg.id);if(p)release_page(p);}
        CAMLreturn(Val_int(0));
    }
    CAMLreturn(Val_int(n<0?n:-5));
}
CAMLprim value kyil_browser_input(value id,value array){
    CAMLparam2(id,array);
    if(Wosize_val(array)!=11)caml_invalid_argument("browser input fields");
    vex_viewport_event_t e={
        .type=Long_val(Field(array,0)),.time=Long_val(Field(array,1)),
        .modifiers=Long_val(Field(array,2)),.pressed=Long_val(Field(array,3)),
        .x=Long_val(Field(array,4)),.y=Long_val(Field(array,5)),.value=Long_val(Field(array,6)),
        .key=Long_val(Field(array,7)),.hardware=Long_val(Field(array,8)),
        .width=Long_val(Field(array,9)),.height=Long_val(Field(array,10))};
    CAMLreturn(Val_int(send_input(page_for(Long_val(id)),&e)));
}
static int upload_frame(struct page *p){
    vex_surface_query_req_t q={.surface_handle=p->cap};
    if((int64_t)vex_syscall1(VEX_SYS_SURFACE_QUERY,(uintptr_t)&q))return -1;
    if(q.width!=p->frame.width || q.height!=p->frame.height || !q.width || !q.height ||
       q.width>4096 || q.height>4096 || q.stride<q.width ||
       (uint64_t)q.stride*q.height*4>q.backing_size || (q.format!=1 && q.format!=2) || q.alpha_mode>2 ||
       !(q.capability_rights&VEX_SURFACE_RIGHT_MAP_READ))return -1;
    if(q.completed_serial<q.submitted_serial)return 1;
    /* Vex Surface stride counts uint32_t pixels; Raylib's image is packed
     * RGBA bytes. Convert that native contract explicitly at this boundary. */
    uint64_t mapped_bytes=(uint64_t)q.stride*q.height*4;
    if(p->mapped_addr && (p->mapped_backing!=q.backing_identity ||
       p->mapped_object!=q.object_identity || p->mapped_bytes!=mapped_bytes)){
        if(release_view(p))return -1;
    }
    if(!p->mapped_addr){
        vex_surface_map_req_t map={.surface_handle=p->cap,.length=mapped_bytes,.access=VEX_SURFACE_MAP_READ};
        if((int64_t)vex_syscall1(VEX_SYS_SURFACE_MAP,(uintptr_t)&map))return -1;
        p->mapped_cap=p->cap;p->cap=0;
        p->mapped_addr=(uintptr_t)map.mapped_addr;p->mapped_bytes=map.length;
        p->mapped_backing=q.backing_identity;p->mapped_object=q.object_identity;
    }
    size_t bytes=(size_t)q.width*q.height*4;
    if(p->staging_bytes!=bytes){
        unsigned char *next=malloc(bytes);
        if(next){free(p->staging);p->staging=next;p->staging_bytes=bytes;}
    }
    unsigned char *rgba=p->staging_bytes==bytes?p->staging:NULL;
    if(rgba){
        const unsigned char *src=(const unsigned char *)(uintptr_t)p->mapped_addr;
        if(kyil_surface_rgba(rgba,bytes,src,p->mapped_bytes,q.width,q.height,q.stride,q.format,q.alpha_mode))return -1;
        rlDrawRenderBatchActive();
        if(p->texture.id && p->texture.width==(int)q.width && p->texture.height==(int)q.height)
            UpdateTexture(p->texture,rgba);
        else{
            Texture2D next=LoadTextureFromImage((Image){rgba,q.width,q.height,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8});
            if(next.id){if(p->texture.id)UnloadTexture(p->texture);p->texture=next;}
            else transport_error=1;
        }
        if(p->frame.sequence<=3){
            size_t nonblack=0;uint64_t hash=1469598103934665603ull;
            for(size_t i=0;i<bytes;i+=4){if(rgba[i]||rgba[i+1]||rgba[i+2])nonblack++;
                hash=(hash^((uint32_t)rgba[i]|((uint32_t)rgba[i+1]<<8)|((uint32_t)rgba[i+2]<<16)))*1099511628211ull;}
            char line[192];snprintf(line,sizeof(line),"Suchu browser: page=%u frame=%llu %ux%u stride_pixels=%u nonblack=%zu hash=%016llx\n",
                p->id,(unsigned long long)p->frame.sequence,q.width,q.height,q.stride,nonblack,(unsigned long long)hash);log_text(line);
        }
    }
    if(!rgba || transport_error)return -1;
    /* UpdateTexture synchronously owns its software texture bytes. Deferred
     * draws sample that independent storage, never the producer's mapped view. */
    /* Keep one authorized read view, reading it ONLY after a new completed
     * frame. The renderer may reuse it after ACK; draws use our copied texture.
     * Avoid new page tables/VA reservations on every frame. */
    if(p->cap)vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,p->cap);
    p->cap=0;p->ack_pending=1;
    return 0;
}
static int pump_page(struct page *p){
    if(p->fd<0)return 0;
    if(p->ack_pending){
        vex_message_io_req_t ack={p->fd,NOWAIT,&p->frame,sizeof(p->frame),NULL,0};
        int64_t n=(int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&ack);
        if(n==-11)return 0;if(n!=sizeof(p->frame))return -1;p->ack_pending=0;
    }
    if(!p->cap){
        vex_message_attachment_req_t a={0};
        vex_message_io_req_t r={p->fd,NOWAIT,&p->frame,sizeof(p->frame),&a,1};
        int64_t n=(int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&r);
        if(n==-11)return 0;
        if(n!=sizeof(p->frame)||p->frame.magic!=VEX_BROWSER_FRAME||r.attachment_count!=1||a.type!=VEX_MESSAGE_OBJECT_SURFACE){
            if(r.attachment_count){if(a.type==VEX_MESSAGE_OBJECT_SURFACE)vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,a.handle);
                else if(a.type==VEX_MESSAGE_OBJECT_FD)vex_syscall1(VEX_SYS_CLOSE,a.handle);}
            return -1;
        }
        p->cap=a.handle;
    }
    return upload_frame(p)<0?-1:0;
}
CAMLprim value kyil_browser_poll(value unit){
    CAMLparam1(unit);CAMLlocal5(result,item,tuple,text,rest);
    result=Val_emptylist;
    if(transport_error)caml_failwith("WebKit transport/input queue failure");
    char logs[1025];if(diagnostics>=0){vex_fd_io_req_t r={diagnostics,logs,1024};
        int64_t n=(int64_t)vex_syscall1(VEX_SYS_READFD,(uintptr_t)&r);
        if(n>0){logs[n]=0;log_text(logs);}else if(!n){vex_syscall1(VEX_SYS_CLOSE,diagnostics);diagnostics=-1;}}
    for(unsigned i=0;i<32;i++){
        vex_browser_control msg;vex_message_attachment_req_t a={0};
        vex_message_io_req_t io={control,NOWAIT,&msg,sizeof(msg),&a,1};
        int64_t n=(int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&io);
        if(n==-11)break;
        if(n!=sizeof(msg)||msg.magic!=VEX_BROWSER_CONTROL){
            if(io.attachment_count)discard_attachment(&a);transport_error=1;break;
        }
        if(msg.operation==VEX_BROWSER_READY){
            struct page *p=page_for(msg.id);
            if(io.attachment_count!=1 || a.type!=VEX_MESSAGE_OBJECT_FD || a.handle>INT32_MAX){
                if(io.attachment_count)discard_attachment(&a);transport_error=1;break;
            }
            if(!p){vex_syscall1(VEX_SYS_CLOSE,a.handle);continue;} /* closed while startup was in flight */
            release_page(p);p->id=msg.id;p->fd=a.handle;
        }else if(msg.operation==VEX_BROWSER_REPORT){
            if(io.attachment_count || !memchr(msg.state.uri,0,sizeof(msg.state.uri)) ||
               !memchr(msg.state.title,0,sizeof(msg.state.title)) || !memchr(msg.state.error,0,sizeof(msg.state.error)) || msg.state.id!=msg.id){
                if(io.attachment_count)discard_attachment(&a);transport_error=1;break;
            }
        }else if(msg.operation==VEX_BROWSER_DOWNLOAD_REPORT){
            if(io.attachment_count || !memchr(msg.download.name,0,sizeof(msg.download.name)) ||
               !memchr(msg.download.destination,0,sizeof(msg.download.destination)) ||
               !memchr(msg.download.error,0,sizeof(msg.download.error)) || msg.download.id!=msg.id){
                if(io.attachment_count)discard_attachment(&a);transport_error=1;break;
            }
            unsigned j=0;for(;j<VEX_BROWSER_MAX_DOWNLOADS;j++)if(downloads[j].id==msg.id || !downloads[j].id)break;
            if(j<VEX_BROWSER_MAX_DOWNLOADS)downloads[j]=msg.download;
        }else{if(io.attachment_count)discard_attachment(&a);transport_error=1;break;}
        tuple=caml_alloc_tuple(6);for(unsigned f=0;f<6;f++)Store_field(tuple,f,Val_unit);
        Store_field(tuple,0,Val_int(msg.operation));Store_field(tuple,1,Val_int(msg.id));
        Store_field(tuple,2,Val_int(msg.state.flags));
        text=caml_copy_string(msg.operation==VEX_BROWSER_REPORT?msg.state.uri:"");Store_field(tuple,3,text);
        text=caml_copy_string(msg.operation==VEX_BROWSER_REPORT?msg.state.title:"");Store_field(tuple,4,text);
        text=caml_copy_string(msg.operation==VEX_BROWSER_REPORT?msg.state.error:"");Store_field(tuple,5,text);
        item=caml_alloc(2,0);Store_field(item,0,tuple);Store_field(item,1,result);result=item;
    }
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)if(pages[i].id && pump_page(&pages[i]))transport_error=1;
    if(transport_error)caml_failwith("WebKit endpoint/frame protocol failed");
    CAMLreturn(result);
}
CAMLprim value kyil_browser_downloads(value unit){
    CAMLparam1(unit);CAMLlocal4(list,node,tuple,text);list=Val_emptylist;
    for(unsigned i=VEX_BROWSER_MAX_DOWNLOADS;i;i--){vex_download_state_t *d=&downloads[i-1];if(!d->id)continue;
        tuple=caml_alloc_tuple(8);for(unsigned f=0;f<8;f++)Store_field(tuple,f,Val_unit);
        Store_field(tuple,0,Val_long(d->id));Store_field(tuple,1,Val_long(d->tab_id));
        Store_field(tuple,2,Val_int(d->status));char number[24];
        snprintf(number,sizeof(number),"%llu",(unsigned long long)d->received_bytes);
        text=caml_copy_string(number);Store_field(tuple,3,text);
        snprintf(number,sizeof(number),"%llu",(unsigned long long)d->total_bytes);
        text=caml_copy_string(number);Store_field(tuple,4,text);
        text=caml_copy_string(d->name);Store_field(tuple,5,text);
        text=caml_copy_string(d->destination);Store_field(tuple,6,text);
        text=caml_copy_string(d->error);Store_field(tuple,7,text);
        node=caml_alloc(2,0);Store_field(node,0,tuple);Store_field(node,1,list);list=node;
    }CAMLreturn(list);
}
CAMLprim value kyil_browser_events(value unit){
    CAMLparam1(unit);CAMLlocal3(list,node,event);list=Val_emptylist;
    for(unsigned i=raw_count;i;i--){vex_app_window_event_t *e=&raw[i-1];
        event=caml_alloc_tuple(6);Store_field(event,0,Val_int(e->type));Store_field(event,1,Val_int(e->flags));
        Store_field(event,2,Val_long((int64_t)e->x));Store_field(event,3,Val_long((int64_t)e->y));
        Store_field(event,4,Val_long((int64_t)e->w));Store_field(event,5,Val_long((int64_t)e->h));
        node=caml_alloc(2,0);Store_field(node,0,event);Store_field(node,1,list);list=node;
    }raw_count=0;CAMLreturn(list);
}
CAMLprim value kyil_browser_draw(value id,value rect){
    CAMLparam2(id,rect);struct page *p=page_for(Long_val(id));
    if(Wosize_val(rect)!=4)caml_invalid_argument("browser viewport rectangle");
    if(!p || !p->texture.id)CAMLreturn(Val_false);
    Rectangle destination={Long_val(Field(rect,0)),Long_val(Field(rect,1)),Long_val(Field(rect,2)),Long_val(Field(rect,3))};
    if(destination.width<=0 || destination.height<=0)CAMLreturn(Val_false);
    DrawTexturePro(p->texture,(Rectangle){0,0,p->texture.width,p->texture.height},destination,(Vector2){0,0},0,WHITE);
    CAMLreturn(Val_true);
}
