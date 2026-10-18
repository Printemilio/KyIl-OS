#ifndef VEX_WPE_STUB_WEBKIT_H
#define VEX_WPE_STUB_WEBKIT_H

#include <glib-object.h>
#include <gio/gio.h>

typedef struct _WebKitWebView WebKitWebView;
typedef struct _WebKitSettings WebKitSettings;
typedef struct _WebKitWebContext WebKitWebContext;
typedef struct _WebKitImage WebKitImage;
typedef struct _WebKitWebViewBackend WebKitWebViewBackend;
typedef struct _JSCValue JSCValue;
typedef struct _WebKitWebResource WebKitWebResource;
typedef struct _WebKitURIResponse WebKitURIResponse;
typedef struct _WebKitResponsePolicyDecision WebKitResponsePolicyDecision;

struct wpe_view_backend;

typedef enum {
    WEBKIT_LOAD_STARTED,
    WEBKIT_LOAD_REDIRECTED,
    WEBKIT_LOAD_COMMITTED,
    WEBKIT_LOAD_FINISHED
} WebKitLoadEvent;

typedef enum {
    WEBKIT_SNAPSHOT_REGION_VISIBLE,
    WEBKIT_SNAPSHOT_REGION_FULL_DOCUMENT
} WebKitSnapshotRegion;

typedef enum {
    WEBKIT_SNAPSHOT_OPTIONS_NONE = 0
} WebKitSnapshotOptions;

typedef enum {
    WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER,
    WEBKIT_CACHE_MODEL_WEB_BROWSER,
    WEBKIT_CACHE_MODEL_DOCUMENT_BROWSER
} WebKitCacheModel;

GType webkit_web_view_get_type(void);
#define WEBKIT_TYPE_WEB_VIEW (webkit_web_view_get_type())
#define WEBKIT_WEB_VIEW(object) ((WebKitWebView *)(object))

WebKitWebViewBackend *webkit_web_view_backend_new(struct wpe_view_backend *backend,
    GDestroyNotify destroy_notify, gpointer user_data);

WebKitSettings *webkit_settings_new(void);
void webkit_settings_set_enable_webgl(WebKitSettings *settings, gboolean enabled);
void webkit_settings_set_enable_webaudio(WebKitSettings *settings, gboolean enabled);
void webkit_settings_set_enable_media(WebKitSettings *settings, gboolean enabled);
void webkit_settings_set_enable_mediasource(WebKitSettings *settings, gboolean enabled);
void webkit_settings_set_javascript_can_access_clipboard(WebKitSettings *settings, gboolean enabled);

WebKitWebContext *webkit_web_context_new(void);
void webkit_web_context_set_cache_model(WebKitWebContext *context, WebKitCacheModel cache_model);

void webkit_web_view_load_uri(WebKitWebView *web_view, const char *uri);
void webkit_web_view_load_html(WebKitWebView *web_view, const char *content, const char *base_uri);
void webkit_web_view_get_snapshot(WebKitWebView *web_view,
    WebKitSnapshotRegion region,
    WebKitSnapshotOptions options,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);
WebKitImage *webkit_web_view_get_snapshot_finish(WebKitWebView *web_view, GAsyncResult *result, GError **error);

GBytes *webkit_image_as_bytes(WebKitImage *image);
int webkit_image_get_width(WebKitImage *image);
int webkit_image_get_height(WebKitImage *image);
guint webkit_image_get_stride(WebKitImage *image);

#endif
