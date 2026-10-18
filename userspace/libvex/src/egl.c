#define _GNU_SOURCE
#include "vex_gl_state.h"
#include <EGL/eglext.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdio.h>
#include "vex_syscall.h"
#include <unos/vex_abi.h>

pthread_mutex_t vex_gl_lock=PTHREAD_MUTEX_INITIALIZER;
_Thread_local egl_context *vex_gl_current;
_Thread_local egl_surface *vex_gl_draw_surface,*vex_gl_read_surface;
static _Thread_local EGLint last_error=EGL_SUCCESS;
static _Thread_local EGLenum client_api=EGL_OPENGL_ES_API;
static egl_context *contexts;static egl_surface *surfaces;
static uintptr_t next_token=16;static int initialized;
static pthread_key_t current_key;static pthread_once_t current_key_once=PTHREAD_ONCE_INIT;static int current_key_error;
static void unbind(void);
static void release_at_thread_exit(void *unused){(void)unused;pthread_mutex_lock(&vex_gl_lock);unbind();pthread_mutex_unlock(&vex_gl_lock);}
static void initialize_current_key(void){current_key_error=pthread_key_create(&current_key,release_at_thread_exit);}
#define DISPLAY ((EGLDisplay)(uintptr_t)1)
#define CONFIG ((EGLConfig)(uintptr_t)2)
static EGLBoolean failure(EGLint error){static unsigned reports;last_error=error;if(reports++<16)fprintf(stderr,"Vex EGL: error=%x caller=%p\n",error,__builtin_return_address(0));return EGL_FALSE;}
static int display(EGLDisplay d){return d==DISPLAY;}
static int ready(EGLDisplay d){if(!display(d)){failure(EGL_BAD_DISPLAY);return 0;}if(!initialized){failure(EGL_NOT_INITIALIZED);return 0;}return 1;}
static egl_context *context(EGLContext handle){for(egl_context *c=contexts;c;c=c->next)if(c->token==(uintptr_t)handle&&!c->deleted)return c;return NULL;}
static egl_surface *surface(EGLSurface handle){for(egl_surface *s=surfaces;s;s=s->next)if(s->token==(uintptr_t)handle&&!s->deleted)return s;return NULL;}
static int native_call(uint64_t op,void *req){return (int)vex_syscall1(op,(uint64_t)(uintptr_t)req);}
static void release_surface(egl_surface *s){
    if(!s||--s->refs)return;egl_surface **p=&surfaces;while(*p&&*p!=s)p=&(*p)->next;if(*p)*p=s->next;
    if(s->native){vex_surface_unmap_req_t r={0};r.surface_handle=s->capability;r.mapped_addr=s->mapped;r.length=s->length;r.access=VEX_SURFACE_MAP_READ|VEX_SURFACE_MAP_WRITE;
        (void)native_call(VEX_SYS_SURFACE_UNMAP,&r);(void)vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,s->capability);if(s->queue)(void)vex_syscall1(VEX_SYS_GPU_QUEUE_DESTROY,s->queue);
        vex_egl_window *window=s->window;if(window->release)window->release(window->owner);
    }else free(s->target.pixels);free(s->target.depth_stencil);free(s);
}
static void release_context(egl_context *c){if(!c||--c->refs)return;egl_context **p=&contexts;while(*p&&*p!=c)p=&(*p)->next;if(*p)*p=c->next;vex_gl_context_cleanup(c);free(c);}
static void unbind(void){
    egl_context *c=vex_gl_current;egl_surface *d=vex_gl_draw_surface,*r=vex_gl_read_surface;
    vex_gl_current=NULL;vex_gl_draw_surface=vex_gl_read_surface=NULL;
    if(c)pthread_setspecific(current_key,NULL);
    if(d){d->bound=0;release_surface(d);}if(r&&r!=d){r->bound=0;release_surface(r);}if(c){c->bound=0;release_context(c);}
}
EGLDisplay eglGetDisplay(EGLNativeDisplayType native){if(native!=EGL_DEFAULT_DISPLAY){failure(EGL_BAD_PARAMETER);return EGL_NO_DISPLAY;}return DISPLAY;}
EGLBoolean eglInitialize(EGLDisplay d,EGLint *major,EGLint *minor){pthread_mutex_lock(&vex_gl_lock);if(!display(d)){pthread_mutex_unlock(&vex_gl_lock);return failure(EGL_BAD_DISPLAY);}initialized=1;if(major)*major=1;if(minor)*minor=4;pthread_mutex_unlock(&vex_gl_lock);return EGL_TRUE;}
EGLBoolean eglTerminate(EGLDisplay d){pthread_mutex_lock(&vex_gl_lock);if(!display(d)){pthread_mutex_unlock(&vex_gl_lock);return failure(EGL_BAD_DISPLAY);}initialized=0;
    egl_context *c=contexts;while(c){egl_context *n=c->next;if(!c->deleted){c->deleted=1;release_context(c);}c=n;}
    egl_surface *s=surfaces;while(s){egl_surface *n=s->next;if(!s->deleted){s->deleted=1;release_surface(s);}s=n;}
    pthread_mutex_unlock(&vex_gl_lock);return EGL_TRUE;}
EGLint eglGetError(void){EGLint e=last_error;last_error=EGL_SUCCESS;return e;}
EGLBoolean eglBindAPI(EGLenum api){if(api!=EGL_OPENGL_ES_API)return failure(EGL_BAD_PARAMETER);client_api=api;return EGL_TRUE;}
EGLenum eglQueryAPI(void){return client_api;}
const char *eglQueryString(EGLDisplay d,EGLint name){if(!ready(d))return NULL;switch(name){case EGL_VENDOR:return "Vex";case EGL_VERSION:return "1.4 Vex experimental";case EGL_CLIENT_APIS:return "OpenGL_ES";case EGL_EXTENSIONS:return "EGL_KHR_surfaceless_context";default:failure(EGL_BAD_PARAMETER);return NULL;}}
static EGLint config_value(EGLint key,int *valid){*valid=1;switch(key){
    case EGL_CONFIG_ID:return 1;case EGL_RED_SIZE:case EGL_GREEN_SIZE:case EGL_BLUE_SIZE:case EGL_ALPHA_SIZE:return 8;
    case EGL_BUFFER_SIZE:return 32;case EGL_DEPTH_SIZE:return 32;case EGL_STENCIL_SIZE:return 8;
    case EGL_SURFACE_TYPE:return EGL_PBUFFER_BIT|EGL_WINDOW_BIT;case EGL_RENDERABLE_TYPE:return EGL_OPENGL_ES2_BIT;
    case EGL_CONFORMANT:return 0; /* Experimental subset, not a conformant ES2 implementation. */
    case EGL_COLOR_BUFFER_TYPE:return EGL_RGB_BUFFER;case EGL_CONFIG_CAVEAT:return EGL_NON_CONFORMANT_CONFIG;
    case EGL_LEVEL:case EGL_SAMPLES:case EGL_SAMPLE_BUFFERS:case EGL_NATIVE_VISUAL_ID:case EGL_NATIVE_VISUAL_TYPE:case EGL_NATIVE_RENDERABLE:case EGL_BIND_TO_TEXTURE_RGB:case EGL_BIND_TO_TEXTURE_RGBA:return 0;
    case EGL_MAX_PBUFFER_WIDTH:case EGL_MAX_PBUFFER_HEIGHT:return 4096;case EGL_MAX_PBUFFER_PIXELS:return 4096*4096;
    case EGL_MIN_SWAP_INTERVAL:case EGL_MAX_SWAP_INTERVAL:return 1;
    case EGL_TRANSPARENT_TYPE:return EGL_NONE;case EGL_TRANSPARENT_RED_VALUE:case EGL_TRANSPARENT_GREEN_VALUE:case EGL_TRANSPARENT_BLUE_VALUE:return 0;
    default:*valid=0;return 0;}}
EGLBoolean eglGetConfigAttrib(EGLDisplay d,EGLConfig c,EGLint key,EGLint *value){if(!ready(d))return EGL_FALSE;if(c!=CONFIG)return failure(EGL_BAD_CONFIG);if(!value)return failure(EGL_BAD_PARAMETER);int valid;*value=config_value(key,&valid);return valid?EGL_TRUE:failure(EGL_BAD_ATTRIBUTE);}
EGLBoolean eglGetConfigs(EGLDisplay d,EGLConfig *configs,EGLint size,EGLint *count){if(!ready(d))return EGL_FALSE;if(size<0||!count)return failure(EGL_BAD_PARAMETER);*count=1;if(configs&&size)*configs=CONFIG;return EGL_TRUE;}
EGLBoolean eglChooseConfig(EGLDisplay d,const EGLint *attrs,EGLConfig *configs,EGLint size,EGLint *count){if(!ready(d))return EGL_FALSE;if(size<0||!count)return failure(EGL_BAD_PARAMETER);int matches=1;
    for(unsigned i=0;attrs&&i<128&&attrs[i]!=EGL_NONE;i+=2){int valid;EGLint key=attrs[i],v=attrs[i+1],actual=config_value(key,&valid);if(!valid)return failure(EGL_BAD_ATTRIBUTE);if(v==EGL_DONT_CARE)continue;
        if(key==EGL_SURFACE_TYPE||key==EGL_RENDERABLE_TYPE||key==EGL_CONFORMANT){if((actual&v)!=v)matches=0;}
        else if(key==EGL_CONFIG_ID||key==EGL_COLOR_BUFFER_TYPE||key==EGL_CONFIG_CAVEAT||key==EGL_LEVEL||key==EGL_TRANSPARENT_TYPE||key==EGL_NATIVE_RENDERABLE){if(actual!=v)matches=0;}
        else if(v<0)return failure(EGL_BAD_ATTRIBUTE);else if(actual<v)matches=0;}
    *count=matches;if(matches&&configs&&size)*configs=CONFIG;return EGL_TRUE;}
static egl_surface *create_surface(uint32_t w,uint32_t h,vex_egl_window *window){
    if(!w||!h||w>4096||h>4096)return NULL;egl_surface *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->target.width=w;s->target.height=h;s->target.depth_stencil=calloc((size_t)w*h,8);if(!s->target.depth_stencil){free(s);return NULL;}
    if(window){
        vex_gpu_buffer_create_req_t b={0};b.size=(uint64_t)w*h*4;b.width=w;b.height=h;b.stride=w;b.format=VEX_GPU_FORMAT_XRGB8888;b.flags=VEX_GPU_MEMORY_CPU_VISIBLE;
        if(native_call(VEX_SYS_GPU_BUFFER_CREATE,&b)!=0)goto fail;
        vex_gpu_surface_create_req_t c={0};c.buffer_handle=b.handle;c.alpha_mode=VEX_GPU_SURFACE_ALPHA_OPAQUE;c.usage=VEX_GPU_SURFACE_USAGE_SAMPLED;
        int result=native_call(VEX_SYS_GPU_SURFACE_CREATE,&c);(void)vex_syscall1(VEX_SYS_GPU_BUFFER_DESTROY,b.handle);if(result!=0)goto fail;
        s->capability=c.surface_handle;vex_surface_query_req_t q={0};q.surface_handle=s->capability;if(native_call(VEX_SYS_SURFACE_QUERY,&q)!=0)goto native_fail;
        vex_surface_map_req_t m={0};m.surface_handle=s->capability;m.length=q.backing_size;m.access=VEX_SURFACE_MAP_READ|VEX_SURFACE_MAP_WRITE;if(native_call(VEX_SYS_SURFACE_MAP,&m)!=0)goto native_fail;
        s->mapped=m.mapped_addr;s->length=m.length;s->target.pixels=(void*)(uintptr_t)m.mapped_addr;s->target.format=VK_FORMAT_B8G8R8A8_UNORM;s->target.top_left=1;s->native=1;s->window=window;
    }else{s->target.pixels=calloc((size_t)w*h,4);if(!s->target.pixels)goto fail;}
    if(window&&window->retain)window->retain(window->owner);
    s->refs=1;s->token=next_token++;s->next=surfaces;surfaces=s;return s;
native_fail:(void)vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,s->capability);
fail:free(s->target.depth_stencil);free(s);return NULL;
}
/* Called under the frontend mutex before using the default framebuffer.
 * Allocate first; an OOM retains the entire previous version. No consumer
 * retains a mutable version after the synchronous present ACK. */
int vex_egl_refresh_surface(egl_surface *s){
    if(!s||!s->native)return 1;vex_egl_window *w=s->window;
    if(w->width==s->target.width&&w->height==s->target.height)return 1;
    egl_surface *replacement=create_surface(w->width,w->height,w);if(!replacement){failure(EGL_BAD_ALLOC);return 0;}
    vex_render_target target=s->target;s->target=replacement->target;replacement->target=target;
    uint64_t cap=s->capability,map=s->mapped,length=s->length;
    s->capability=replacement->capability;s->mapped=replacement->mapped;s->length=replacement->length;
    replacement->capability=cap;replacement->mapped=map;replacement->length=length;
    release_surface(replacement);return 1;
}
EGLSurface eglCreatePbufferSurface(EGLDisplay d,EGLConfig config,const EGLint *attrs){pthread_mutex_lock(&vex_gl_lock);if(!ready(d)||config!=CONFIG){pthread_mutex_unlock(&vex_gl_lock);if(config!=CONFIG)failure(EGL_BAD_CONFIG);return EGL_NO_SURFACE;}
    EGLint w=0,h=0;for(unsigned i=0;attrs&&i<128&&attrs[i]!=EGL_NONE;i+=2){if(attrs[i]==EGL_WIDTH)w=attrs[i+1];else if(attrs[i]==EGL_HEIGHT)h=attrs[i+1];else if(attrs[i]==EGL_LARGEST_PBUFFER&&attrs[i+1]==EGL_FALSE){}else {pthread_mutex_unlock(&vex_gl_lock);failure(EGL_BAD_ATTRIBUTE);return EGL_NO_SURFACE;}}
    egl_surface *s=create_surface(w,h,NULL);pthread_mutex_unlock(&vex_gl_lock);if(!s){failure(w<0||h<0?EGL_BAD_PARAMETER:EGL_BAD_ALLOC);return EGL_NO_SURFACE;}return (void*)s->token;}
EGLSurface eglCreateWindowSurface(EGLDisplay d,EGLConfig config,EGLNativeWindowType native,const EGLint *attrs){pthread_mutex_lock(&vex_gl_lock);if(!ready(d)||config!=CONFIG){pthread_mutex_unlock(&vex_gl_lock);if(config!=CONFIG)failure(EGL_BAD_CONFIG);return EGL_NO_SURFACE;}
    vex_egl_window *w=(void*)(uintptr_t)native;if(!w||w->magic!=VEX_EGL_WINDOW_MAGIC||!!w->retain!=!!w->release){pthread_mutex_unlock(&vex_gl_lock);failure(EGL_BAD_NATIVE_WINDOW);return EGL_NO_SURFACE;}
    if(attrs&&attrs[0]!=EGL_NONE){pthread_mutex_unlock(&vex_gl_lock);failure(EGL_BAD_ATTRIBUTE);return EGL_NO_SURFACE;}
    egl_surface *s=create_surface(w->width,w->height,w);pthread_mutex_unlock(&vex_gl_lock);if(!s){failure(EGL_BAD_ALLOC);return EGL_NO_SURFACE;}return (void*)s->token;}
EGLSurface eglCreatePixmapSurface(EGLDisplay d,EGLConfig config,EGLNativePixmapType native,const EGLint *attrs){(void)d;(void)config;(void)native;(void)attrs;failure(EGL_BAD_NATIVE_PIXMAP);return EGL_NO_SURFACE;}
EGLContext eglCreateContext(EGLDisplay d,EGLConfig config,EGLContext share,const EGLint *attrs){pthread_mutex_lock(&vex_gl_lock);
    if(!ready(d)||config!=CONFIG){pthread_mutex_unlock(&vex_gl_lock);if(config!=CONFIG)failure(EGL_BAD_CONFIG);return EGL_NO_CONTEXT;}
    EGLint version=1;unsigned attr_index=0;
    for(;attrs&&attr_index<128&&attrs[attr_index]!=EGL_NONE;attr_index+=2){
        if(attrs[attr_index]!=EGL_CONTEXT_CLIENT_VERSION){pthread_mutex_unlock(&vex_gl_lock);failure(EGL_BAD_ATTRIBUTE);return EGL_NO_CONTEXT;}
        version=attrs[attr_index+1];
    }
    if(attrs&&attr_index==128){pthread_mutex_unlock(&vex_gl_lock);failure(EGL_BAD_ATTRIBUTE);return EGL_NO_CONTEXT;}
    if(version!=2){pthread_mutex_unlock(&vex_gl_lock);failure(EGL_BAD_MATCH);return EGL_NO_CONTEXT;}
    egl_context *parent=share?context(share):NULL;if(share&&!parent){pthread_mutex_unlock(&vex_gl_lock);failure(EGL_BAD_CONTEXT);return EGL_NO_CONTEXT;}
    egl_context *c=calloc(1,sizeof(*c));if(!c){pthread_mutex_unlock(&vex_gl_lock);failure(EGL_BAD_ALLOC);return EGL_NO_CONTEXT;}
    c->share=parent?parent->share:calloc(1,sizeof(gl_share));if(!c->share){free(c);pthread_mutex_unlock(&vex_gl_lock);failure(EGL_BAD_ALLOC);return EGL_NO_CONTEXT;}
    ++c->share->refs;if(!c->share->next_name)c->share->next_name=1;c->refs=1;c->token=next_token++;c->next=contexts;contexts=c;vex_gl_context_initialize(c);
    pthread_mutex_unlock(&vex_gl_lock);return (void*)c->token;}
EGLBoolean eglDestroyContext(EGLDisplay d,EGLContext h){pthread_mutex_lock(&vex_gl_lock);egl_context *c=context(h);EGLBoolean r;if(!ready(d))r=EGL_FALSE;else if(!c)r=failure(EGL_BAD_CONTEXT);else {c->deleted=1;release_context(c);r=EGL_TRUE;}pthread_mutex_unlock(&vex_gl_lock);return r;}
EGLBoolean eglDestroySurface(EGLDisplay d,EGLSurface h){pthread_mutex_lock(&vex_gl_lock);egl_surface *s=surface(h);EGLBoolean r;if(!ready(d))r=EGL_FALSE;else if(!s)r=failure(EGL_BAD_SURFACE);else {s->deleted=1;release_surface(s);r=EGL_TRUE;}pthread_mutex_unlock(&vex_gl_lock);return r;}
EGLBoolean eglMakeCurrent(EGLDisplay d,EGLSurface draw,EGLSurface read,EGLContext h){pthread_mutex_lock(&vex_gl_lock);
    if(!display(d)){pthread_mutex_unlock(&vex_gl_lock);return failure(EGL_BAD_DISPLAY);}if(!h){if(draw||read){pthread_mutex_unlock(&vex_gl_lock);return failure(EGL_BAD_MATCH);}unbind();pthread_mutex_unlock(&vex_gl_lock);return EGL_TRUE;}
    if(!ready(d)){pthread_mutex_unlock(&vex_gl_lock);return EGL_FALSE;}egl_context *c=context(h);egl_surface *a=draw?surface(draw):NULL,*b=read?surface(read):NULL;
    if(!c|| (draw&&!a)||(read&&!b)){pthread_mutex_unlock(&vex_gl_lock);return failure(!c?EGL_BAD_CONTEXT:EGL_BAD_SURFACE);}
    if((draw==0)!=(read==0)){pthread_mutex_unlock(&vex_gl_lock);return failure(EGL_BAD_MATCH);}pthread_t owner=pthread_self();
    if((c->bound&&!pthread_equal(c->owner,owner))||(a&&a->bound&&!pthread_equal(a->owner,owner))||(b&&b->bound&&!pthread_equal(b->owner,owner))){pthread_mutex_unlock(&vex_gl_lock);return failure(EGL_BAD_ACCESS);}
    pthread_once(&current_key_once,initialize_current_key);
    if(current_key_error||pthread_setspecific(current_key,(void*)1)!=0){pthread_mutex_unlock(&vex_gl_lock);return failure(EGL_BAD_ALLOC);}
    ++c->refs;if(a)++a->refs;if(b&&b!=a)++b->refs;unbind();vex_gl_current=c;vex_gl_draw_surface=a;vex_gl_read_surface=b;c->bound=1;c->owner=owner;
    pthread_setspecific(current_key,(void*)1);
    if(a){a->bound=1;a->owner=owner;}if(b){b->bound=1;b->owner=owner;}
    if(c->state.viewport.width==0&&a){c->state.viewport=(VkViewport){0,0,a->target.width,a->target.height,0,1};c->state.scissor=(VkRect2D){{0,0},{a->target.width,a->target.height}};}
    pthread_mutex_unlock(&vex_gl_lock);return EGL_TRUE;}
EGLContext eglGetCurrentContext(void){return vex_gl_current?(void*)vex_gl_current->token:EGL_NO_CONTEXT;}
EGLDisplay eglGetCurrentDisplay(void){return vex_gl_current?DISPLAY:EGL_NO_DISPLAY;}
EGLSurface eglGetCurrentSurface(EGLint which){egl_surface *s;if(which==EGL_DRAW)s=vex_gl_draw_surface;else if(which==EGL_READ)s=vex_gl_read_surface;else {failure(EGL_BAD_PARAMETER);return EGL_NO_SURFACE;}return s?(void*)s->token:EGL_NO_SURFACE;}
EGLBoolean eglReleaseThread(void){pthread_mutex_lock(&vex_gl_lock);unbind();pthread_mutex_unlock(&vex_gl_lock);client_api=EGL_OPENGL_ES_API;last_error=EGL_SUCCESS;return EGL_TRUE;}
EGLBoolean eglQueryContext(EGLDisplay d,EGLContext h,EGLint attr,EGLint *value){pthread_mutex_lock(&vex_gl_lock);egl_context *c=context(h);EGLBoolean r=EGL_FALSE;if(ready(d)){if(!c)r=failure(EGL_BAD_CONTEXT);else if(!value)r=failure(EGL_BAD_PARAMETER);else if(attr==EGL_CONTEXT_CLIENT_VERSION){*value=2;r=EGL_TRUE;}else if(attr==EGL_CONFIG_ID){*value=1;r=EGL_TRUE;}else if(attr==EGL_CONTEXT_CLIENT_TYPE){*value=EGL_OPENGL_ES_API;r=EGL_TRUE;}else r=failure(EGL_BAD_ATTRIBUTE);}pthread_mutex_unlock(&vex_gl_lock);return r;}
EGLBoolean eglQuerySurface(EGLDisplay d,EGLSurface h,EGLint attr,EGLint *value){pthread_mutex_lock(&vex_gl_lock);egl_surface *s=surface(h);EGLBoolean r=EGL_FALSE;if(ready(d)){if(!s)r=failure(EGL_BAD_SURFACE);else if(!value)r=failure(EGL_BAD_PARAMETER);else {r=EGL_TRUE;switch(attr){case EGL_WIDTH:*value=s->target.width;break;case EGL_HEIGHT:*value=s->target.height;break;case EGL_CONFIG_ID:*value=1;break;case EGL_RENDER_BUFFER:*value=EGL_BACK_BUFFER;break;case EGL_SWAP_BEHAVIOR:*value=EGL_BUFFER_PRESERVED;break;default:r=failure(EGL_BAD_ATTRIBUTE);}}}pthread_mutex_unlock(&vex_gl_lock);return r;}
EGLBoolean eglSwapBuffers(EGLDisplay d,EGLSurface h){pthread_mutex_lock(&vex_gl_lock);egl_surface *s=surface(h);EGLBoolean r=EGL_FALSE;if(ready(d)){if(!s)r=failure(EGL_BAD_SURFACE);else if(!vex_gl_current||vex_gl_draw_surface!=s)r=failure(EGL_BAD_CURRENT_SURFACE);else if(!s->native)r=EGL_TRUE;else {vex_egl_window *w=s->window;
        if(!vex_egl_refresh_surface(s))r=EGL_FALSE;else if(!w->present||w->present(w->owner,s->capability,s->serial+1)!=0)r=failure(EGL_BAD_ACCESS);else {++s->serial;if((s->serial&31u)==0u)vex_gl_memory_diagnostic(vex_gl_current,s->serial);r=EGL_TRUE;}}}pthread_mutex_unlock(&vex_gl_lock);return r;}
EGLBoolean eglSwapInterval(EGLDisplay d,EGLint interval){if(!ready(d))return EGL_FALSE;if(!vex_gl_current)return failure(EGL_BAD_CONTEXT);if(interval!=1)return failure(EGL_BAD_PARAMETER);return EGL_TRUE;}
EGLBoolean eglWaitGL(void){vex_gl_finish();return EGL_TRUE;}
EGLBoolean eglWaitClient(void){vex_gl_finish();return EGL_TRUE;}
EGLBoolean eglWaitNative(EGLint engine){if(engine!=EGL_CORE_NATIVE_ENGINE)return failure(EGL_BAD_PARAMETER);return EGL_TRUE;}
EGLBoolean eglSurfaceAttrib(EGLDisplay d,EGLSurface s,EGLint attr,EGLint value){(void)d;(void)s;(void)attr;(void)value;return failure(EGL_BAD_ATTRIBUTE);}
EGLBoolean eglBindTexImage(EGLDisplay d,EGLSurface s,EGLint b){(void)d;(void)s;(void)b;return failure(EGL_BAD_MATCH);}
EGLBoolean eglReleaseTexImage(EGLDisplay d,EGLSurface s,EGLint b){(void)d;(void)s;(void)b;return failure(EGL_BAD_MATCH);}
EGLBoolean eglCopyBuffers(EGLDisplay d,EGLSurface s,EGLNativePixmapType p){(void)d;(void)s;(void)p;return failure(EGL_BAD_NATIVE_PIXMAP);}
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name){
    if(!name||strncmp(name,"gl",2))return NULL;
    Dl_info own,found;if(!dladdr((void*)eglGetProcAddress,&own))return NULL;
    void *module=dlopen(own.dli_fname,RTLD_LAZY|RTLD_NOLOAD);if(!module)return NULL;
    void *symbol=dlsym(module,name);
    if(symbol&&(!dladdr(symbol,&found)||found.dli_fbase!=own.dli_fbase))symbol=NULL;
    if(!symbol){static unsigned missing;if(missing++<64)fprintf(stderr,"Vex EGL: unavailable entry point %s\n",name);}
    dlclose(module);return (__eglMustCastToProperFunctionPointerType)symbol;
}
