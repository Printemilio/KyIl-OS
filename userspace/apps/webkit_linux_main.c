/* Expose POSIX + GNU extensions (pread/pwrite/setenv/putenv) before any include */
#define _GNU_SOURCE
#define VEX_HOSTED_BUILD 1

/*
 * webkit_linux_main.c — Real WPEWebKit browser process.
 *
 * This is a standard Linux ELF (compiled with a normal Linux toolchain).
 * It runs under VexOS's Linux ABI compatibility layer.
 *
 * It links against:
 *   libWPEWebKit-2.0.so        – WebKit WPE port (2.x API)
 *   libwpe-1.0.so              – libwpe base
 *   libglib-2.0.so             – GLib main loop
 *   libgio-2.0.so              – GIO (for async IO used by WebKit)
 *
 * The native supervisor owns chrome and stable page identities. Reliable
 * control messages create/select/close real WebKit views and report URI,
 * title, loading/history/error state. Each view has its own Surface-bearing
 * renderer endpoint and native viewport input stream. The small SHM bootstrap
 * contains only the explicitly inherited control descriptor. Snapshots are
 * opt-in diagnostics, never presentation transport.
 */

#include <wpe/webkit.h>
#include <wpe/wpe.h>
#include <glib.h>
#include <glib-unix.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <malloc.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "vex_syscall.h"
#include "vex_browser_protocol.h"
#include "webkit_downloads.h"
#include <dlfcn.h>
#ifdef VEX_WEBKIT_RUNTIME_TRACE
#include <stdarg.h>
#include <sys/uio.h>
#include <printf.h>
/* Diagnostic build only: mirror journal fields, then use the actual
 * libsystemd sink with the same fields and preserve its error result. */
int sd_journal_send_with_location(const char *file,const char *line,
                                 const char *function,const char *format,...) {
    struct iovec fields[32]={0};unsigned used=0;int result=0;
    va_list ap;va_start(ap,format);
    while(format && used<32){
        char *text=NULL;va_list copy;va_copy(copy,ap);
        int count=vasprintf(&text,format,copy);va_end(copy);
        if(count<0){result=-ENOMEM;break;}
        fields[used++]=(struct iovec){text,(size_t)count};
        if(!strncmp(text,"MESSAGE=",8))fprintf(stderr,"WebKit diagnostic %s: %s\n",function,text);
        int types[32];size_t n=parse_printf_format(format,32,types);
        if(n>32){result=-E2BIG;break;}
        for(size_t i=0;i<n;i++){
            int flags=types[i]&PA_FLAG_MASK,kind=types[i]&~PA_FLAG_MASK;
            if(flags&PA_FLAG_PTR || kind==PA_STRING || kind==PA_POINTER || kind==PA_WSTRING){(void)va_arg(ap,void *);}
            else if(kind==PA_DOUBLE || kind==PA_FLOAT){if(flags&PA_FLAG_LONG_DOUBLE)(void)va_arg(ap,long double);else (void)va_arg(ap,double);}
            else if(kind==PA_INT && flags&PA_FLAG_LONG_LONG){(void)va_arg(ap,long long);}
            else if(kind==PA_INT && flags&PA_FLAG_LONG){(void)va_arg(ap,long);}
            else {(void)va_arg(ap,int);}
        }
        format=va_arg(ap,const char *);
    }
    va_end(ap);
    typedef int (*sendv_fn)(const char *,const char *,const char *,const struct iovec *,int);
    sendv_fn original=(sendv_fn)dlsym(RTLD_NEXT,"sd_journal_sendv_with_location");
    if(!result)result=format?-E2BIG:(original?original(file,line,function,fields,(int)used):-ENOSYS);
    for(unsigned i=0;i<used;i++)free(fields[i].iov_base);return result;
}
#endif

#include <sys/socket.h>
#include <errno.h>

/* Kept local because the minimal WPE ABI header used by the staging build
 * intentionally exposes only the APIs needed by this launcher. */
extern const gchar *webkit_web_view_get_title(WebKitWebView *web_view);
extern void webkit_web_view_evaluate_javascript(WebKitWebView *,const char *,gssize,const char *,const char *,GCancellable *,GAsyncReadyCallback,gpointer);
extern JSCValue *webkit_web_view_evaluate_javascript_finish(WebKitWebView *,GAsyncResult *,GError **);
extern gchar *jsc_value_to_string(JSCValue *);
extern void webkit_web_view_terminate_web_process(WebKitWebView *);
extern WebKitNetworkSession *webkit_network_session_get_default(void);

/* ── display dimensions (must match webkit_launcher.c) ──────────────────── */

#define FB_WIDTH   1280
#define FB_HEIGHT   720
#define FB_STRIDE  1280   /* pixels per row */

#define SHM_FB_NAME   "vex_webkit_fb"
#define SHM_CMD_NAME  "vex_webkit_cmd"
#define SHM_MAGIC     0x56455842u   /* "VEXB" */
#define SHM_CMD_MAX   512

#define PIXEL_BUF_BYTES  ((size_t)(FB_WIDTH * FB_HEIGHT * 4))
#define SHM_FB_SIZE      sizeof(vex_webkit_shm_hdr_t)

/* ── SHM header (keep in sync with webkit_launcher.c) ───────────────────── */

typedef struct {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t frame_seq;      /* we increment on each new frame      */
    uint32_t cmd_seq;        /* launcher increments on new command  */
    uint32_t cmd_seq_ack;    /* we increment after reading command  */
    uint32_t reserved;
} vex_webkit_shm_hdr_t;

/* ── globals ─────────────────────────────────────────────────────────────── */

static uint64_t         g_fb_handle = 0u;
static uint8_t         *g_shm_ptr = NULL;   /* mmap'd SHM region            */
static WebKitWebView   *g_view    = NULL;
static GMainLoop       *g_loop    = NULL;
static int             g_viewport_fd = -1;
static guint           g_window_watch;
static gboolean         g_snapshot_pending = FALSE;
static gboolean         g_periodic_snapshots = FALSE;
static uint32_t         g_snapshot_requests = 0;
static uint32_t         g_frames_produced = 0;
static uint32_t         g_snapshot_errors = 0;

/* Last command sequence number seen from the launcher */
extern const gchar *webkit_web_view_get_uri(WebKitWebView *);
extern gboolean webkit_web_view_is_loading(WebKitWebView *);
extern gboolean webkit_web_view_can_go_back(WebKitWebView *);
extern gboolean webkit_web_view_can_go_forward(WebKitWebView *);
extern void webkit_web_view_go_back(WebKitWebView *);
extern void webkit_web_view_go_forward(WebKitWebView *);
extern void webkit_web_view_reload(WebKitWebView *);
extern void webkit_web_view_stop_loading(WebKitWebView *);
extern WebKitURIResponse *webkit_response_policy_decision_get_response(WebKitResponsePolicyDecision *);
extern gboolean webkit_response_policy_decision_is_mime_type_supported(WebKitResponsePolicyDecision *);
extern const gchar *webkit_web_resource_get_uri(WebKitWebResource *);
extern WebKitURIResponse *webkit_web_resource_get_response(WebKitWebResource *);
extern const gchar *webkit_uri_response_get_mime_type(WebKitURIResponse *);
extern const gchar *webkit_uri_response_get_uri(WebKitURIResponse *);
extern guint webkit_uri_response_get_status_code(WebKitURIResponse *);
extern guint64 webkit_uri_response_get_content_length(WebKitURIResponse *);
static WebKitSettings *browser_settings;
static WebKitWebContext *browser_context;
static void *backend_module;
static struct wpe_view_backend *(*create_backend)(int);
typedef struct browser_page browser_page;
typedef struct browser_resource_profile {
    browser_page *page;
    gint64 started_us;
    gchar *uri;
    guint32 navigation_generation;
    gboolean accounted;
} browser_resource_profile;
struct browser_page {
    uint32_t id;
    WebKitWebView *view;
    int exported_fd;
    gboolean ready_pending,report_pending,navigated,navigation_requested,stop_requested,perf_finished;
    gboolean diverted;  /* the last navigation became a download; reported once */
    vex_browser_state_t state;
    gint64 perf_started_us,perf_committed_us;
    guint32 perf_generation,perf_resources_started,perf_resources_finished,perf_resources_failed,perf_resources_active;
    guint32 perf_resources_logged;
    guint64 perf_bytes,perf_resource_time_us,perf_slowest_us;
};

static void perf_resource_profile_free(gpointer data) {
    browser_resource_profile *profile=data;
    if(!profile)return;
    g_free(profile->uri);
    g_free(profile);
}

static void perf_resource_account(WebKitWebResource *resource,browser_resource_profile *profile, gboolean failed) {
    browser_page *page;
    gint64 elapsed;
    if (!profile || profile->accounted) return;
    profile->accounted = TRUE;
    page = profile->page;
    if (!page || profile->navigation_generation != page->perf_generation) return;
    elapsed = g_get_monotonic_time() - profile->started_us;
    if (elapsed < 0) elapsed = 0;
    if (page->perf_resources_active) --page->perf_resources_active;
    if (failed) ++page->perf_resources_failed;
    else ++page->perf_resources_finished;
    page->perf_resource_time_us += (guint64)elapsed;
    if ((guint64)elapsed > page->perf_slowest_us) page->perf_slowest_us = (guint64)elapsed;
    gpointer response=resource?webkit_web_resource_get_response(resource):NULL;
    guint status=response?webkit_uri_response_get_status_code(response):0u;
    guint64 length=response?webkit_uri_response_get_content_length(response):0u;
    if(response && length!=UINT64_MAX && UINT64_MAX-page->perf_bytes>=length)page->perf_bytes+=length;
    /* Keep one bounded trace for the diagnostic run. The WebKit resource API
     * has no "received-data" signal; logging a connection to it emits a GLib
     * critical for every request and severely distorts performance. */
    if (page->perf_resources_logged < 128u && (failed || elapsed >= 1000000)) {
        ++page->perf_resources_logged;
        fprintf(stderr,"browser resource: tab=%u elapsed_ms=%lld status=%u bytes=%llu uri=%s%s\n",
            page->id,(long long)(elapsed/1000),status,(unsigned long long)length,
            profile->uri?profile->uri:"(unknown)",failed?" failed":"");
    }
}

static void perf_resource_finished(WebKitWebResource *resource, gpointer data) {
    perf_resource_account(resource,data, FALSE);
}

static void perf_resource_failed(WebKitWebResource *resource, GError *error, gpointer data) {
    (void)error;
    perf_resource_account(resource,data, TRUE);
}

static void perf_resource_started(WebKitWebView *view, WebKitWebResource *resource,
                                  GObject *request, gpointer data) {
    browser_page *page = data;
    browser_resource_profile *profile;
    (void)view; (void)request;
    if (!page || !resource) return;
    profile = g_new0(browser_resource_profile, 1);
    if (!profile) return;
    profile->page = page;
    profile->started_us = g_get_monotonic_time();
    profile->uri = g_strdup(webkit_web_resource_get_uri(resource));
    profile->navigation_generation = page->perf_generation;
    ++page->perf_resources_started;
    ++page->perf_resources_active;
    if(page->perf_resources_started<=128u){
        gint64 nav_ms=page->perf_started_us?(profile->started_us-page->perf_started_us)/1000:0;
        fprintf(stderr,"browser resource start: tab=%u nav_ms=%lld uri=%s\n",
            page->id,(long long)nav_ms,profile->uri?profile->uri:"(unknown)");
    }
    g_object_set_data_full(G_OBJECT(resource), "vex-browser-resource-profile", profile, perf_resource_profile_free);
    g_signal_connect(resource, "finished", G_CALLBACK(perf_resource_finished), profile);
    g_signal_connect(resource, "failed", G_CALLBACK(perf_resource_failed), profile);
}
static browser_page browser_pages[VEX_BROWSER_MAX_TABS];
static browser_page *find_page(uint32_t id) {
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)if(browser_pages[i].id==id)return &browser_pages[i];
    return NULL;
}
static uint32_t page_id_for_view(WebKitWebView *view) {
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)if(browser_pages[i].id && browser_pages[i].view==view)return browser_pages[i].id;
    return 0;
}
/* Download states travel on the same reliable control channel as page state. */
static vex_download_report_result download_report_deliver(const vex_download_state_t *state,void *context) {
    (void)context;
    vex_browser_control m={.magic=VEX_BROWSER_CONTROL,.operation=VEX_BROWSER_DOWNLOAD_REPORT,.id=state->id,.download=*state};
    vex_message_io_req_t send={g_viewport_fd,MSG_DONTWAIT,&m,sizeof(m),NULL,0};
    int64_t n=vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send);
    if(n==-EAGAIN)return VEX_DOWNLOAD_REPORT_WOULD_BLOCK;
    return n==sizeof(m)?VEX_DOWNLOAD_REPORT_DELIVERED:VEX_DOWNLOAD_REPORT_FAILED;
}
static void page_report(browser_page *p) {
    if(!p || !p->id || !p->view)return;
    p->state.id=p->id;
    p->state.flags&=VEX_BROWSER_FAILED;
    if(p->diverted)p->state.flags|=VEX_BROWSER_DIVERTED;
    if(p->navigation_requested || (!p->stop_requested && webkit_web_view_is_loading(p->view)))p->state.flags|=VEX_BROWSER_LOADING;
    if(webkit_web_view_can_go_back(p->view))p->state.flags|=VEX_BROWSER_CAN_BACK;
    if(webkit_web_view_can_go_forward(p->view))p->state.flags|=VEX_BROWSER_CAN_FORWARD;
    const char *uri=webkit_web_view_get_uri(p->view),*title=webkit_web_view_get_title(p->view);
    /* Process replacement may notify the old URI/is-loading before STARTED.
     * Preserve the requested address and Stop until WebKit starts that load. */
    if(!p->navigation_requested && !p->stop_requested && !(p->state.flags&VEX_BROWSER_FAILED) && p->navigated && uri && *uri)g_strlcpy(p->state.uri,uri,sizeof(p->state.uri));
    g_strlcpy(p->state.title,p->state.flags&VEX_BROWSER_FAILED?"Navigation failed":(title&&*title?title:(p->navigated?"Loading...":"New Page")),sizeof(p->state.title));
    p->report_pending=TRUE;
}
static void page_notify(GObject *view,GParamSpec *property,gpointer data) {
    (void)view;(void)property;page_report(data);
}
/* A response that became a download interrupts the navigation by policy, which
 * WebKit reports as a load failure. Nothing failed and no new page appears: tell
 * the chrome to show the previously committed page again, or its native home
 * when the page had none. */
static gboolean load_interrupted_by_policy(const GError *error) {
    return g_strcmp0(g_quark_to_string(error->domain),"WebKitPolicyError")==0 && error->code==102;
}
static void page_diverted(browser_page *p,WebKitWebView *view) {
    const char *uri=webkit_web_view_get_uri(view);
    const gboolean committed=uri && *uri && g_strcmp0(uri,"about:blank")!=0;
    p->navigation_requested=FALSE;p->stop_requested=FALSE;p->perf_finished=TRUE;
    p->navigated=committed;p->diverted=TRUE;
    p->state.flags&=~VEX_BROWSER_FAILED;p->state.error[0]=0;
    g_strlcpy(p->state.uri,committed?uri:"",sizeof(p->state.uri));
    page_report(p);
}
static gboolean page_failed(WebKitWebView *view,WebKitLoadEvent event,const gchar *uri,GError *error,gpointer data) {
    (void)event;browser_page *p=data;
    if(load_interrupted_by_policy(error)){page_diverted(p,view);return TRUE;}
    /* Cancellation is the observable outcome of Stop or a superseding load. */
    if(g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED) ||
       (g_strcmp0(g_quark_to_string(error->domain),"WebKitNetworkError")==0 && error->code==302))return FALSE;
    p->navigation_requested=FALSE;p->stop_requested=FALSE;p->perf_finished=TRUE;
    if(uri && *uri)g_strlcpy(p->state.uri,uri,sizeof(p->state.uri));
    p->state.flags|=VEX_BROWSER_FAILED;g_strlcpy(p->state.error,error->message,sizeof(p->state.error));page_report(p);
    fprintf(stderr,"browser load failed: tab=%u uri=%s domain=%s code=%d message=%s\n",p->id,uri,g_quark_to_string(error->domain),error->code,error->message);
    return TRUE; /* Native chrome displays the genuine failure, no fake page. */
}
static void page_terminated(WebKitWebView *view,gint reason,gpointer data) {
    (void)view;browser_page *p=data;
    p->navigation_requested=FALSE;p->stop_requested=FALSE;p->state.flags|=VEX_BROWSER_FAILED;
    g_snprintf(p->state.error,sizeof(p->state.error),"Web process terminated (reason %d); reload to retry",reason);
    page_report(p);
    fprintf(stderr,"browser: web process terminated tab=%u reason=%d\n",p->id,reason);
}
static gboolean page_policy(WebKitWebView *view,gpointer decision,gint type,gpointer data) {
    (void)view;browser_page *p=data;
    if(type==2) {
        gpointer response=webkit_response_policy_decision_get_response(decision);
        fprintf(stderr,"browser response: tab=%u uri=%s status=%u mime=%s supported=%d\n",p->id,
            webkit_uri_response_get_uri(response),webkit_uri_response_get_status_code(response),
            webkit_uri_response_get_mime_type(response),webkit_response_policy_decision_is_mime_type_supported(decision));
    }
    /* Undisplayable or attachment responses are downloads, not failed loads. */
    if(type==2 && vex_downloads_handle_response(decision))return TRUE;
    return FALSE; /* Otherwise observe and retain WebKit's genuine policy. */
}
static void file_probe(const char *uri) {
    if(g_strcmp0(getenv("VEX_BROWSER_FILE_DIAGNOSTIC"),"1"))return;
    static unsigned probes;if(probes++>=4 || strncmp(uri,"file://",7))return;
    GFile *file=g_file_new_for_uri(uri);GError *error=NULL;
    GFileInfo *info=g_file_query_info(file,"standard::type,standard::size,standard::content-type",0,NULL,&error);
    if(info){fprintf(stderr,"browser file probe: uri=%s type=%d size=%lld content-type=%s\n",uri,
        g_file_info_get_file_type(info),(long long)g_file_info_get_size(info),g_file_info_get_content_type(info));g_object_unref(info);}
    else {fprintf(stderr,"browser file probe: query failed %s\n",error?error->message:"unknown");g_clear_error(&error);}
    char *content=NULL;gsize size=0;
    if(g_file_load_contents(file,NULL,&content,&size,NULL,&error)){fprintf(stderr,"browser file probe: read bytes=%zu prefix=%.32s\n",size,content);g_free(content);}
    else{fprintf(stderr,"browser file probe: read failed %s\n",error?error->message:"unknown");g_clear_error(&error);}
    g_object_unref(file);
}
static void on_load_changed(WebKitWebView *,WebKitLoadEvent,gpointer);
static browser_page *page_create(uint32_t id) {
    if(!id || find_page(id))return NULL;
    browser_page *p=NULL;for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)if(!browser_pages[i].id){p=&browser_pages[i];break;}
    if(!p)return NULL;
    int pair[2];vex_message_pair_req_t req={pair,SOCK_CLOEXEC,0};
    if((int64_t)vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&req))return NULL;
    struct wpe_view_backend *backend=create_backend(pair[1]);
    if(!backend){close(pair[0]);close(pair[1]);return NULL;}
    WebKitWebViewBackend *wrapper=webkit_web_view_backend_new(backend,(GDestroyNotify)wpe_view_backend_destroy,backend);
    if(!wrapper){wpe_view_backend_destroy(backend);close(pair[0]);return NULL;}
    *p=(browser_page){.id=id,.exported_fd=pair[0],.ready_pending=TRUE};
    p->view=WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,"backend",wrapper,
        "web-context",browser_context,"settings",browser_settings,NULL));
    if(!p->view){close(pair[0]);*p=(browser_page){0};return NULL;}
    g_signal_connect(p->view,"load-changed",G_CALLBACK(on_load_changed),p);
    g_signal_connect(p->view,"resource-load-started",G_CALLBACK(perf_resource_started),p);
    g_signal_connect(p->view,"load-failed",G_CALLBACK(page_failed),p);
    g_signal_connect(p->view,"web-process-terminated",G_CALLBACK(page_terminated),p);
    g_signal_connect(p->view,"decide-policy",G_CALLBACK(page_policy),p);
    g_signal_connect(p->view,"notify::uri",G_CALLBACK(page_notify),p);
    g_signal_connect(p->view,"notify::title",G_CALLBACK(page_notify),p);
    g_signal_connect(p->view,"notify::is-loading",G_CALLBACK(page_notify),p);
    page_report(p);
    fprintf(stderr,"browser: real page created tab=%u view=%p endpoint=%d\n",id,(void *)p->view,pair[0]);
    return p;
}
static void page_destroy(browser_page *p) {
    if(!p || !p->id)return;
    fprintf(stderr,"browser: destroy page tab=%u\n",p->id);
    g_signal_handlers_disconnect_by_data(p->view,p);
    if(p->view==g_view)g_view=NULL;
    webkit_web_view_stop_loading(p->view);
    g_object_unref(p->view);
    if(p->ready_pending)close(p->exported_fd);
    *p=(browser_page){0};
}
static int flush_reports(void) {
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++) {
        browser_page *p=&browser_pages[i];if(!p->id)continue;
        if(p->ready_pending){vex_browser_control m={.magic=VEX_BROWSER_CONTROL,.operation=VEX_BROWSER_READY,.id=p->id};
            vex_message_attachment_req_t a={(uint64_t)p->exported_fd,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
            vex_message_io_req_t send={g_viewport_fd,MSG_DONTWAIT,&m,sizeof(m),&a,1};int64_t n=vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send);
            if(n==-EAGAIN)continue;if(n!=sizeof(m))return -1;
            close(p->exported_fd);p->exported_fd=-1;p->ready_pending=FALSE;
        }
        if(p->report_pending){vex_browser_control m={.magic=VEX_BROWSER_CONTROL,.operation=VEX_BROWSER_REPORT,.id=p->id,.state=p->state};
            vex_message_io_req_t send={g_viewport_fd,MSG_DONTWAIT,&m,sizeof(m),NULL,0};int64_t n=vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send);
            if(n==-EAGAIN)continue;if(n!=sizeof(m))return -1;p->report_pending=FALSE;p->diverted=FALSE;
        }
    }
    return vex_downloads_flush(download_report_deliver,NULL);
}
static void probe_ready(GObject *view,GAsyncResult *result,gpointer data) {
    uint32_t id=(uint32_t)(uintptr_t)data;GError *error=NULL;
    JSCValue *value=webkit_web_view_evaluate_javascript_finish((WebKitWebView *)view,result,&error);
    if(value){gchar *text=jsc_value_to_string(value);fprintf(stderr,"browser DOM: tab=%u %s\n",id,text?text:"(null)");g_free(text);g_object_unref(value);}
    else fprintf(stderr,"browser DOM: tab=%u error=%s\n",id,error?error->message:"(null)");g_clear_error(&error);
}
static gboolean control_receive(gint fd,GIOCondition condition,gpointer data) {
    (void)data;if(!(condition&G_IO_IN)){g_main_loop_quit(g_loop);g_window_watch=0;return G_SOURCE_REMOVE;}
    for(unsigned i=0;i<16;i++) {
        vex_browser_control m={0};vex_message_io_req_t r={fd,MSG_DONTWAIT,&m,sizeof(m),NULL,0};int64_t n=vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&r);
        if(n==-EAGAIN)break;
        if(n!=sizeof(m)||m.magic!=VEX_BROWSER_CONTROL || !memchr(m.state.uri,0,sizeof(m.state.uri))){g_main_loop_quit(g_loop);g_window_watch=0;return G_SOURCE_REMOVE;}
        if(m.operation==VEX_BROWSER_SHUTDOWN){fprintf(stderr,"browser: shutdown requested\n");g_main_loop_quit(g_loop);g_window_watch=0;return G_SOURCE_REMOVE;}
        if(m.operation==VEX_BROWSER_DOWNLOAD_CANCEL){vex_downloads_cancel(m.id);continue;}
        browser_page *p=find_page(m.id);
        fprintf(stderr,"browser command: op=%u tab=%u uri=%s\n",m.operation,m.id,m.state.uri);
        if(m.operation==VEX_BROWSER_CREATE) {
            if(!page_create(m.id)){vex_browser_control fail={.magic=VEX_BROWSER_CONTROL,.operation=VEX_BROWSER_REPORT,.id=m.id,.state={.id=m.id,.flags=VEX_BROWSER_FAILED}};
                g_strlcpy(fail.state.error,"WebKit page creation failed",sizeof(fail.state.error));
                vex_message_io_req_t send={fd,0,&fail,sizeof(fail),NULL,0};if((int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send)!=sizeof(fail))g_main_loop_quit(g_loop);}
            continue;
        }
        if(!p)continue; /* queued event for an already closed page */
        switch(m.operation) {
        case VEX_BROWSER_SELECT:g_view=p->view;break;
        case VEX_BROWSER_CLOSE:page_destroy(p);break;
        case VEX_BROWSER_NAVIGATE:
            p->state.flags&=~VEX_BROWSER_FAILED;p->state.error[0]=0;p->navigated=TRUE;p->navigation_requested=TRUE;p->stop_requested=FALSE;p->diverted=FALSE;
            g_strlcpy(p->state.uri,m.state.uri,sizeof(p->state.uri));
            file_probe(m.state.uri);
            webkit_web_view_load_uri(p->view,m.state.uri);page_report(p);break;
        case VEX_BROWSER_BACK:p->navigation_requested=TRUE;p->stop_requested=FALSE;webkit_web_view_go_back(p->view);break;
        case VEX_BROWSER_FORWARD:p->navigation_requested=TRUE;p->stop_requested=FALSE;webkit_web_view_go_forward(p->view);break;
        case VEX_BROWSER_RELOAD:p->navigation_requested=TRUE;p->stop_requested=FALSE;webkit_web_view_reload(p->view);break;
        case VEX_BROWSER_STOP:
            p->navigation_requested=FALSE;p->stop_requested=TRUE;
            webkit_web_view_stop_loading(p->view);page_report(p);break;
        case VEX_BROWSER_PROBE:
            /* User-requested diagnostic only; inspect the actual main context. */
            fprintf(stderr,"browser GLib now=%lld context=%p\n",(long long)g_get_monotonic_time(),(void *)g_main_context_default());
            for(guint source_id=1;source_id<256;source_id++){
                GSource *source=g_main_context_find_source_by_id(NULL,source_id);
                if(source)fprintf(stderr,"browser GLib source id=%u name=%s priority=%d ready=%lld destroyed=%d\n",source_id,g_source_get_name(source),g_source_get_priority(source),(long long)g_source_get_ready_time(source),g_source_is_destroyed(source));
            }

            webkit_web_view_evaluate_javascript(p->view,"if(!window._vexInputTrace){window._vexInputTrace=[];for(const t of ['mousedown','mouseup','click','keydown','input'])document.addEventListener(t,e=>{window._vexInputTrace.push([t,e.target.tagName,e.target.id,e.clientX,e.clientY,e.button,e.key,e.timeStamp]);if(window._vexInputTrace.length>20)window._vexInputTrace.shift()},true)};JSON.stringify({events:window._vexInputTrace,metrics:(()=>{let c=document.createElement('canvas').getContext('2d');c.font='18px sans-serif';return [c.measureText('0').width,c.measureText('Navigate to document B').width]})(),rects:Array.from(document.querySelectorAll('h1,button,a,input')).slice(0,16).map(x=>({tag:x.tagName,rect:[x.getBoundingClientRect().x,x.getBoundingClientRect().y,x.getBoundingClientRect().width,x.getBoundingClientRect().height],font:getComputedStyle(x).font})),hit:document.elementFromPoint(126,287)?.outerHTML.slice(0,120),url:location.href,title:document.title,focus:document.hasFocus(),active:document.activeElement?.id,scrollY:scrollY,text:document.body.innerText.slice(0,500),inputs:Array.from(document.querySelectorAll('input')).map(x=>({id:x.id,value:x.value,box:[x.offsetLeft,x.offsetTop,x.offsetWidth,x.offsetHeight],size:x.size}))})",-1,NULL,NULL,NULL,probe_ready,(gpointer)(uintptr_t)m.id);break;
        default:fprintf(stderr,"browser: invalid control operation %u\n",m.operation);g_main_loop_quit(g_loop);break;
        }
    }
    return G_SOURCE_CONTINUE;
}

/* ── SHM helpers ─────────────────────────────────────────────────────────── */

static inline vex_webkit_shm_hdr_t *shm_hdr(void) {
    return (vex_webkit_shm_hdr_t *)g_shm_ptr;
}

static void shm_write_header(const vex_webkit_shm_hdr_t *hdr) {
    if (!g_shm_ptr) return;
    memcpy(g_shm_ptr, hdr, sizeof(*hdr));
}

static void shm_read_header(vex_webkit_shm_hdr_t *hdr) {
    if (!g_shm_ptr) { memset(hdr, 0, sizeof(*hdr)); return; }
    memcpy(hdr, g_shm_ptr, sizeof(*hdr));
}



/* ── snapshot callback ───────────────────────────────────────────────────── */

static void on_snapshot_ready(GObject *source, GAsyncResult *res, gpointer user_data) {
    GError       *error  = NULL;
    WebKitImage  *image  = NULL;
    GBytes       *bytes  = NULL;
    const uint32_t request_id = (uint32_t)(uintptr_t)user_data;

    g_snapshot_pending = FALSE;

    /* WPE 2.x: snapshot_finish returns WebKitImage*, not cairo_surface_t* */
    image = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(source), res, &error);
    if (error) {
        const char *domain = g_quark_to_string(error->domain);
        ++g_snapshot_errors;
        /* Keep this bounded: the 60 Hz timer can otherwise flood the serial
         * console and hide the first failing operation.  The domain/code are
         * the information WebKit's generic message omits. */
        if (g_snapshot_errors <= 8u) {
            fprintf(stderr, "webkit_linux_main: snapshot request #%u failed (%u) domain=%s code=%d: %s\n",
                request_id, g_snapshot_errors, domain ? domain : "(unknown)", error->code,
                error->message ? error->message : "(unknown)");
        }
        g_error_free(error);
        return;
    }
    if (!image) {
        fprintf(stderr, "webkit_linux_main: snapshot request #%u returned no image\n", request_id);
        return;
    }

    /* webkit_image_as_bytes() gives the raw BGRA_8888 pixel data.
     * As little-endian uint32: byte layout = B,G,R,A → value = 0xAARRGGBB.
     * The native renderer Surface is presented independently of these bytes. */
    bytes = webkit_image_as_bytes(image);
    if (g_frames_produced < 3u) {
        fprintf(stderr, "webkit_linux_main: snapshot request #%u returned image=%dx%d stride=%u bytes=%s\n",
            request_id, webkit_image_get_width(image), webkit_image_get_height(image),
            (unsigned)webkit_image_get_stride(image), bytes ? "present" : "missing");
    }
    if (bytes && g_shm_ptr) {
        gsize      data_size  = 0;
        const uint8_t *src_data = (const uint8_t *)g_bytes_get_data(bytes, &data_size);
        int src_width  = webkit_image_get_width(image);
        int src_height = webkit_image_get_height(image);
        guint src_stride = webkit_image_get_stride(image);

        if ((g_frames_produced < 64u || (g_frames_produced & 15u) == 0u) && src_data && src_width > 0 && src_height > 0 &&
            src_stride >= (guint)src_width * 4u && data_size >= (gsize)src_stride * src_height) {
            uint64_t hash = UINT64_C(14695981039346656037);
            size_t nonblack = 0u, differing = 0u;
            uint32_t first = 0u;
            memcpy(&first, src_data, sizeof(first));
            for (int row = 0; row < src_height; ++row) {
                const uint32_t *pixels = (const uint32_t *)(const void *)(src_data + (gsize)row * src_stride);
                for (int col = 0; col < src_width; ++col) {
                    uint32_t value = pixels[col];
                    nonblack += (value & 0xffffffu) != 0u;
                    differing += value != first;
                    for (unsigned byte = 0; byte < 4; ++byte) {
                        hash = (hash ^ ((value >> (byte * 8u)) & 255u)) * UINT64_C(1099511628211);
                    }
                }
            }
            fprintf(stderr, "webkit_linux_main: pixels request=%u hash=%016llx nonblack=%zu differing=%zu first=%08x\n",
                request_id, (unsigned long long)hash, nonblack, differing, first);
            fprintf(stderr, "webkit_linux_main: snapshot title=%s\n",
                webkit_web_view_get_title(WEBKIT_WEB_VIEW(source)) ? webkit_web_view_get_title(WEBKIT_WEB_VIEW(source)) : "(unset)");
        }

        /* webkit_image_as_bytes() returns storage owned by WebKitImage.
         * Keep the borrowed GBytes alive through validation and release
         * only the image below.  Unrefing both decrements the same object
         * twice and corrupts later snapshot requests. */

        /* Successful snapshot count; native Surface presentation is separate. */
        vex_webkit_shm_hdr_t *hdrp = shm_hdr();
        if (hdrp->magic == SHM_MAGIC) {
            hdrp->frame_seq++;
            ++g_frames_produced;
            if (g_frames_produced <= 64u || (g_frames_produced & 15u) == 0u) {
                fprintf(stderr, "webkit_linux_main: frame %u ready (%dx%d stride=%u)\n",
                    g_frames_produced, src_width, src_height, (unsigned)src_stride);
            }
        }
    }

    g_object_unref(image);
}

static void on_web_process_terminated(WebKitWebView *view, gint reason, gpointer user_data) {
    (void)view;
    (void)user_data;
    fprintf(stderr, "webkit_linux_main: web-process-terminated reason=%d requests=%u frames=%u snapshot_errors=%u\n",
        reason, g_snapshot_requests, g_frames_produced, g_snapshot_errors);
}

/* ── request a snapshot (guarded against queuing multiple at once) ────────── */

#ifdef VEX_WEBKIT_RENDER_SMOKE
static void dom_probe_ready(GObject *source,GAsyncResult *result,gpointer user) {
    (void)user;GError *error=NULL;
    JSCValue *value=webkit_web_view_evaluate_javascript_finish((WebKitWebView *)source,result,&error);
    if(value){gchar *text=jsc_value_to_string(value);fprintf(stderr,"webkit DOM probe: %s\n",text?text:"(null)");g_free(text);g_object_unref(value);}
    else {fprintf(stderr,"webkit DOM probe failed: %s\n",error?error->message:"(unset)");g_clear_error(&error);}
}
#endif
static void request_snapshot(void) {
    if (g_snapshot_pending || !g_view) return;
    g_snapshot_pending = TRUE;
    ++g_snapshot_requests;
#ifdef VEX_WEBKIT_RENDER_SMOKE
    if((g_snapshot_requests&31u)==0u) {
        struct mallinfo2 m=mallinfo2();
        fprintf(stderr,"WebKit UI libc memory: snapshots=%u arena=%zu live=%zu reusable=%zu mmap=%zu chunks=%zu\n",g_snapshot_requests,m.arena,m.uordblks,m.fordblks,m.hblkhd,m.hblks);
        const char *probe="if(!window._vexInputTrace){window._vexInputTrace=[];for(const t of ['mousedown','mouseup','click','keydown','input'])document.addEventListener(t,e=>{window._vexInputTrace.push([t,e.target.tagName,e.target.id,e.clientX,e.clientY,e.button,e.key,e.timeStamp]);if(window._vexInputTrace.length>20)window._vexInputTrace.shift()},true)};JSON.stringify({events:window._vexInputTrace,metrics:(()=>{let c=document.createElement('canvas').getContext('2d');c.font='18px sans-serif';return [c.measureText('0').width,c.measureText('Navigate to document B').width]})(),rects:Array.from(document.querySelectorAll('h1,button,a,input')).slice(0,16).map(x=>({tag:x.tagName,rect:[x.getBoundingClientRect().x,x.getBoundingClientRect().y,x.getBoundingClientRect().width,x.getBoundingClientRect().height],font:getComputedStyle(x).font})),hit:document.elementFromPoint(126,287)?.outerHTML.slice(0,120),url:location.href,focus:document.hasFocus(),active:document.activeElement.id,scrollY:scrollY,scrollHeight:document.scrollingElement.scrollHeight,clientHeight:document.scrollingElement.clientHeight,overflow:getComputedStyle(document.scrollingElement).overflowY,input:document.getElementById('entry').value,point:document.elementFromPoint(306,167)?.id,rect:document.getElementById('entry').getBoundingClientRect().toJSON()})";
        webkit_web_view_evaluate_javascript(g_view,probe,-1,NULL,NULL,NULL,dom_probe_ready,NULL);
    }
#endif
    if (g_snapshot_requests <= 4u) {
        fprintf(stderr, "webkit_linux_main: request_snapshot #%u\n", g_snapshot_requests);
    }
    webkit_web_view_get_snapshot(g_view,
                                 WEBKIT_SNAPSHOT_REGION_VISIBLE,
                                 WEBKIT_SNAPSHOT_OPTIONS_NONE,
                                 NULL,
                                 on_snapshot_ready,
                                 (gpointer)(uintptr_t)g_snapshot_requests);
}

/* ── periodic timer: poll for commands and refresh frame ─────────────────── */

static gboolean on_timer(gpointer user_data) {
    vex_webkit_shm_hdr_t hdr;
    static gint64 next_perf_report_us;
    (void)user_data;

    if(flush_reports()) {g_main_loop_quit(g_loop);return G_SOURCE_REMOVE;}

    /* Keep long navigations diagnosable even when WebKit never emits FINISHED:
     * report bounded aggregate resource progress, not a line per request. */
    gint64 now=g_get_monotonic_time();
    if(now>=next_perf_report_us){
        next_perf_report_us=now+5000000;
        for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++){
            browser_page *page=&browser_pages[i];
            if(!page->id || !page->perf_started_us || page->perf_finished)continue;
            fprintf(stderr,"browser perf pending: tab=%u elapsed_ms=%lld committed_ms=%lld resources=%u/%u failed=%u active=%u bytes=%llu resource_sum_ms=%llu slowest_ms=%llu\n",
                page->id,(long long)((now-page->perf_started_us)/1000),
                (long long)(page->perf_committed_us?(now-page->perf_committed_us)/1000:0),
                page->perf_resources_finished,page->perf_resources_started,page->perf_resources_failed,
                page->perf_resources_active,(unsigned long long)page->perf_bytes,
                (unsigned long long)(page->perf_resource_time_us/1000),
                (unsigned long long)(page->perf_slowest_us/1000));
        }
    }

    /* Snapshot/readback is not the presentation transport. Repeatedly asking
     * WebKit for the whole bitmap on a static page creates needless IPC,
     * mappings and CPU work, competing with input and actual render frames. */
    if (g_periodic_snapshots) request_snapshot();

    return G_SOURCE_CONTINUE;
}

/* ── load-changed signal ─────────────────────────────────────────────────── */

typedef struct { guint32 tab, generation; } navigation_probe;
static void navigation_timing_ready(GObject *view,GAsyncResult *result,gpointer data) {
    navigation_probe *probe=data;GError *error=NULL;
    JSCValue *value=webkit_web_view_evaluate_javascript_finish((WebKitWebView *)view,result,&error);
    browser_page *page=find_page(probe->tab);
    if(!page || page->perf_generation!=probe->generation) {
        if(value)g_object_unref(value);
        g_clear_error(&error);g_free(probe);return;
    }
    if(value){
        gchar *text=jsc_value_to_string(value);
        fprintf(stderr,"browser navigation timing: tab=%u generation=%u %s\n",probe->tab,probe->generation,text?text:"null");
        g_free(text);g_object_unref(value);
    }else fprintf(stderr,"browser navigation timing: tab=%u error=%s\n",probe->tab,error?error->message:"unknown");
    g_clear_error(&error);g_free(probe);
}
static void navigation_timing(WebKitWebView *view,const browser_page *page) {
    /* Read standard Navigation/Paint Timing once per completed document. No
     * mutation, synthetic resource or benchmark policy enters the page. */
    static const char script[]="JSON.stringify((()=>{const n=performance.getEntriesByType('navigation')[0];if(!n)return null;const f=performance.getEntriesByType('paint').find(p=>p.name==='first-contentful-paint');return {type:n.type,ttfb_ms:n.responseStart-n.requestStart,transfer_ms:n.responseEnd-n.responseStart,dom_interactive_ms:n.domInteractive,dom_content_loaded_ms:n.domContentLoadedEventEnd,dom_complete_ms:n.domComplete,first_paint_ms:f?f.startTime:null,transfer_bytes:n.transferSize,encoded_bytes:n.encodedBodySize,decoded_bytes:n.decodedBodySize,protocol:n.nextHopProtocol}})())";
    navigation_probe *probe=g_new(navigation_probe,1);
    *probe=(navigation_probe){page->id,page->perf_generation};
    webkit_web_view_evaluate_javascript(view,script,-1,NULL,NULL,NULL,navigation_timing_ready,probe);
}

static void on_load_changed(WebKitWebView *view, WebKitLoadEvent load_event, gpointer user_data) {
    browser_page *page=user_data;
    gint64 now=g_get_monotonic_time();
    if(load_event==WEBKIT_LOAD_STARTED){
        page->navigation_requested=FALSE;page->stop_requested=FALSE;page->state.flags&=~VEX_BROWSER_FAILED;page->state.error[0]=0;
        ++page->perf_generation;page->perf_started_us=now;page->perf_committed_us=0;
        page->perf_finished=FALSE;
        page->perf_resources_started=page->perf_resources_finished=page->perf_resources_failed=page->perf_resources_active=0;
        page->perf_resources_logged=0;
        page->perf_bytes=page->perf_resource_time_us=page->perf_slowest_us=0;
    } else if(load_event==WEBKIT_LOAD_COMMITTED)page->perf_committed_us=now;
    else if(load_event==WEBKIT_LOAD_FINISHED)page->stop_requested=FALSE;
    page_report(user_data);
    fprintf(stderr, "browser: tab=%u load event=%d elapsed_ms=%lld uri=%s\n",page->id,(int)load_event,
        (long long)(page->perf_started_us?(now-page->perf_started_us)/1000:0),webkit_web_view_get_uri(view));
    switch (load_event) {
    case WEBKIT_LOAD_COMMITTED:
    case WEBKIT_LOAD_FINISHED:
        if (load_event == WEBKIT_LOAD_FINISHED) {
            const gchar *title = webkit_web_view_get_title(view);
            fprintf(stderr, "webkit_linux_main: load finished status=%s title=%s perf_ms=%lld commit_to_finish_ms=%lld resources=%u/%u failed=%u active=%u bytes=%llu resource_sum_ms=%llu slowest_ms=%llu\n",
                page->state.flags&VEX_BROWSER_FAILED?"failed":page->perf_committed_us?"committed":"no-commit",
                title ? title : "(null)",
                (long long)(page->perf_started_us?(now-page->perf_started_us)/1000:0),
                (long long)(page->perf_committed_us?(now-page->perf_committed_us)/1000:0),
                page->perf_resources_finished,page->perf_resources_started,page->perf_resources_failed,
                page->perf_resources_active,(unsigned long long)page->perf_bytes,
                (unsigned long long)(page->perf_resource_time_us/1000),
                (unsigned long long)(page->perf_slowest_us/1000));
            page->perf_finished=TRUE;
            if(page->perf_committed_us && !(page->state.flags&VEX_BROWSER_FAILED))navigation_timing(view,page);
        }
        if (g_periodic_snapshots) request_snapshot();
        break;
    default:
        break;
    }
}

/* ── Native VexOS SHM setup ───────────────────────────────────────────────── */

static int open_and_init_shm(void) {
    vex_webkit_shm_hdr_t hdr;
    vex_shm_open_req_t open_req;
    vex_shm_map_req_t map_req;

    open_req.name = SHM_FB_NAME;
    open_req.handle = 0u;
    if (vex_syscall1(VEX_SYS_SHM_OPEN, (uint64_t)(uintptr_t)&open_req) != 0u) {
        fprintf(stderr, "webkit_linux_main: native framebuffer SHM unavailable\n");
        return -1;
    }
    g_fb_handle = open_req.handle;
    fprintf(stderr, "webkit_linux_main: framebuffer capability=%llu\n",
        (unsigned long long)g_fb_handle);
    map_req.handle = g_fb_handle; map_req.preferred_addr = NULL; map_req.length = SHM_FB_SIZE;
    map_req.offset = 0u; map_req.prot = PROT_READ | PROT_WRITE; map_req.flags = 0u; map_req.mapped_addr = NULL;
    if (vex_syscall1(VEX_SYS_SHM_MAP, (uint64_t)(uintptr_t)&map_req) != 0u) {
        fprintf(stderr, "webkit_linux_main: native framebuffer SHM map failed\n");
        return -1;
    }
    g_shm_ptr = (uint8_t *)map_req.mapped_addr;
    shm_read_header(&hdr);
    if (hdr.magic != SHM_MAGIC) {
        fprintf(stderr, "webkit_linux_main: invalid framebuffer SHM header\n");
        return -1;
    }
    {
        if(hdr.reserved>INT32_MAX || fcntl((int)hdr.reserved,F_GETFD)<0) {
            fprintf(stderr,"webkit_linux_main: invalid native viewport endpoint\n");
            return -1;
        }
        g_viewport_fd=(int)hdr.reserved;
        char endpoint[24];snprintf(endpoint,sizeof(endpoint),"%u",hdr.reserved);
        setenv("VEX_VIEWPORT_FD",endpoint,1);
    }
    fprintf(stderr, "webkit_linux_main: SHM framebuffer mapped (%zu bytes)\n", (size_t)SHM_FB_SIZE);

    return 0;
}

/* ── main ────────────────────────────────────────────────────────────────── */

static gboolean on_native_window_closed(gint fd,GIOCondition condition,gpointer data) {
    (void)fd;(void)data;
    if(!(condition&(G_IO_HUP|G_IO_ERR|G_IO_NVAL)))return G_SOURCE_CONTINUE;
    fprintf(stderr,"webkit_linux_main: native window disconnected, shutting down content\n");
    g_window_watch=0;
    g_main_loop_quit(g_loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
#ifdef VEX_WEBKIT_RUNTIME_TRACE
    setenv("WEBKIT_DEBUG","Process=debug,ProcessSwapping=debug,Network=debug,Loading=debug",1);
#endif
    WebKitSettings   *settings;
    WebKitWebContext *context;
    WebKitWebViewBackend *view_backend;
    struct wpe_view_backend *wpe_backend;
    const char *webkit_exec_path;
    const char *ld_library_path;
    const char *wpe_backend_library;
    (void)argc;
    (void)argv;

    fprintf(stderr, "webkit_linux_main: starting\n");
    g_periodic_snapshots = g_strcmp0(getenv("VEX_SNAPSHOT_DIAGNOSTIC"), "1") == 0;
#ifdef VEX_WEBKIT_RENDER_SMOKE
    g_periodic_snapshots = TRUE;
#endif
    if (getenv("VEX_JSC_DIAGNOSTIC")) {
        setenv("JSC_dumpOptions", "1", 1);
        setenv("JSC_reportCompileTimes", "true", 1);
        setenv("JSC_reportBaselineCompileTimes", "true", 1);
    }

    /* Force explicit runtime paths inside VexOS so helper processes and
       shared libraries are found without relying on host defaults. */
    setenv("LD_LIBRARY_PATH", "/lib:/usr/lib:/usr/local/lib", 1);
    /* Opt-in diagnostic only: inherited by WPEWebProcess to collect a CFI
     * unwind at the real libc abort path, then delegate to libc unchanged. */
    if(getenv("VEX_DIAG_FONT_TRACE"))setenv("LD_PRELOAD","/lib/libvex_font_trace.so",1);
    if (getenv("VEX_DIAG_ABORT_TRACE")) {
        setenv("LD_PRELOAD", "/lib/libvex_abort_trace.so", 1);
    }
    /* Bounded diagnostic for the Lot 30 JIT path; JSC prints its selected
     * runtime options and the page below exercises a hot JavaScript function. */
    /* Lot 27 diagnostic: preload only future WebKit subprocesses. */
    setenv("WEBKIT_EXEC_PATH", "/usr/local/libexec/wpe-webkit-2.0", 1);
    setenv("XDG_RUNTIME_DIR", "/tmp", 0);

    /* Use the custom WPE backend that bridges to the VexOS framebuffer. */
    if (!getenv("WPE_BACKEND_LIBRARY")) {
        putenv("WPE_BACKEND_LIBRARY=/lib/libvex_wpe_backend.so");
    }
    webkit_exec_path = getenv("WEBKIT_EXEC_PATH");
    ld_library_path = getenv("LD_LIBRARY_PATH");
    wpe_backend_library = getenv("WPE_BACKEND_LIBRARY");
    fprintf(stderr, "webkit_linux_main: WEBKIT_EXEC_PATH=%s\n",
        webkit_exec_path ? webkit_exec_path : "(null)");
    fprintf(stderr, "webkit_linux_main: LD_LIBRARY_PATH=%s\n",
        ld_library_path ? ld_library_path : "(null)");
    fprintf(stderr, "webkit_linux_main: WPE_BACKEND_LIBRARY=%s\n",
        wpe_backend_library ? wpe_backend_library : "(null)");
    /* Force software rendering — no GPU required under VexOS compat */
    setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 0);
    setenv("LIBGL_ALWAYS_SOFTWARE", "true", 0);
    setenv("GALLIUM_DRIVER", "softpipe", 0);

    /* Explicitly select the VexOS WPE implementation before any view is
     * created.  This is the native software/surface backend; it does not
     * introduce an X11, Wayland or GPU dependency. */
    fprintf(stderr, "webkit_linux_main: loading Vex WPE backend\n");
    if (!wpe_loader_init("/lib/libWPEBackend-default.so")) {
        fprintf(stderr, "webkit_linux_main: Vex WPE backend loader failed\n");
        return 1;
    }
    fprintf(stderr, "webkit_linux_main: Vex WPE backend loaded\n");

    /* Fonts & fontconfig — tell WebKit where to find fonts on VexOS.
     * /usr/share/fonts and /etc/fonts are staged on the boot CD-ROM.
     * Cache goes to /tmp (writable scratch space). */
    setenv("FONTCONFIG_PATH",    "/etc/fonts",       0);
    setenv("FONTCONFIG_CACHE",   "/tmp/fc-cache",    0);
    setenv("FC_CACHEDIR",        "/tmp/fc-cache",    0);
    setenv("XDG_DATA_DIRS",      "/usr/share",       0);
#ifdef VEX_WEBKIT_RENDER_SMOKE
    setenv("VEX_MEMORY_DIAGNOSTIC","1",1);
#endif

    /* Initialise shared memory */
    if (open_and_init_shm() < 0) {
        return 1;
    }
    fprintf(stderr, "webkit_linux_main: shared-memory setup complete\n");

    /* Build a WebKitSettings that disables features that need GPU/EGL.
     *
     * WPE 2.x API note:
     *   - webkit_settings_set_enable_accelerated_2d_canvas()     → REMOVED
     *   - webkit_settings_set_hardware_acceleration_policy()      → REMOVED
     *   Use webkit_settings_set_enable_webgl() to gate GPU paths instead.
     */
    settings = browser_settings = webkit_settings_new();
    fprintf(stderr, "webkit_linux_main: settings created\n");
    webkit_settings_set_enable_webgl(settings, FALSE);
    webkit_settings_set_enable_webaudio(settings, FALSE);
    webkit_settings_set_enable_media(settings, FALSE);
    webkit_settings_set_enable_mediasource(settings, FALSE);
    webkit_settings_set_javascript_can_access_clipboard(settings, FALSE);

    /* Create a web context.
     * WPE 2.x API note:
     *   - webkit_web_context_set_process_model()  → REMOVED (always multi-process now).
     *   Use WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER to minimise memory use. */
    context = browser_context = webkit_web_context_new();
    fprintf(stderr, "webkit_linux_main: context created\n");
    webkit_web_context_set_cache_model(context, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
    /* Downloads started by any page of the shared session end up in VexFS. */
    vex_downloads_init(webkit_network_session_get_default(), page_id_for_view);

    backend_module=dlopen("/lib/libvex_wpe_backend.so",RTLD_NOW|RTLD_LOCAL);
    create_backend=backend_module?dlsym(backend_module,"vex_wpe_view_backend_create_for_endpoint"):NULL;
    if(!create_backend){fprintf(stderr,"browser: endpoint factory unavailable: %s\n",dlerror());return 1;}
    fcntl(g_viewport_fd,F_SETFD,FD_CLOEXEC);
    browser_page *initial=page_create(1);
    if(!initial)return 1;
    g_view=initial->view;
    fprintf(stderr,"webkit_linux_main: web view created\n");

    /* The render regression uses the same real WebKit API/renderer. It is a
     * separate opt-in test build, never an alternative implementation of GL. */
#ifdef VEX_WEBKIT_RENDER_SMOKE
    const char *document_path = "/usr/linux-tests/webkit-render.html";
    int document_fd = open(document_path, O_RDONLY);
    struct stat document_stat;
    if (document_fd < 0 || fstat(document_fd, &document_stat) || document_stat.st_size <= 0 ||
        document_stat.st_size > 1024 * 1024) {
        fprintf(stderr, "webkit_linux_main: render document open/stat failed\n");
        return 1;
    }
    char *document = malloc((size_t)document_stat.st_size + 1u);
    if (!document) { close(document_fd); return 1; }
    size_t document_read = 0u;
    while (document_read < (size_t)document_stat.st_size) {
        ssize_t bytes = read(document_fd, document + document_read, (size_t)document_stat.st_size - document_read);
        if (bytes <= 0) { fprintf(stderr, "webkit_linux_main: render document read failed\n"); close(document_fd); free(document); return 1; }
        document_read += (size_t)bytes;
    }
    close(document_fd); document[document_read] = '\0';
    webkit_web_view_load_html(g_view, document, "file:///usr/linux-tests/webkit-render.html");
    /* Retain the source through shutdown, independently of loader ownership. */
#else
    /* New Page belongs to the native chrome; no HTML browser UI/bootstrap. */
#endif
    fprintf(stderr, "webkit_linux_main: initial document submitted\n");

    /* GLib main loop */
    g_loop = g_main_loop_new(NULL, FALSE);

    /* Timer: 16 ms = ~60 fps */
    guint timer_source=g_timeout_add(16, on_timer, NULL);
    /* The native window owns this content instance. Readiness/HUP is delivered
     * by IPC/GIO, not a timer checking the producer PID. */
    g_window_watch=g_unix_fd_add(g_viewport_fd,G_IO_IN|G_IO_HUP|G_IO_ERR|G_IO_NVAL,
        control_receive,NULL);

    fprintf(stderr, "webkit_linux_main: entering main loop\n");
    g_main_loop_run(g_loop);
    if(g_window_watch)g_source_remove(g_window_watch);
    g_source_remove(timer_source);
    /* Stop transfers and delete partial files while the NetworkProcess lives. */
    vex_downloads_shutdown();
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)if(browser_pages[i].view)
        webkit_web_view_terminate_web_process(browser_pages[i].view);
    for(unsigned i=0;i<VEX_BROWSER_MAX_TABS;i++)page_destroy(&browser_pages[i]);
    g_object_unref(browser_settings);g_object_unref(browser_context);
    close(g_viewport_fd);
    if(backend_module)dlclose(backend_module);
#ifdef VEX_WEBKIT_RENDER_SMOKE
    free(document);
#endif

    /* Cleanup */
    if (g_shm_ptr) munmap(g_shm_ptr, SHM_FB_SIZE);
    if (g_fb_handle) (void)vex_syscall1(VEX_SYS_SHM_CLOSE, g_fb_handle);
    g_main_loop_unref(g_loop);
    return 0;
}
