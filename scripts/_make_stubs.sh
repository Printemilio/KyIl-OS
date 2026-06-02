#!/bin/bash
mkdir -p /tmp/wpe-shim/wpe
printf '#pragma once\n/* WPE stub */\n' > /tmp/wpe-shim/wpe/wpe.h
cat > /tmp/wpe-shim/wpe/webkit.h << 'EOF'
#pragma once
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
typedef struct _WebKitWebView    WebKitWebView;
typedef struct _WebKitSettings   WebKitSettings;
typedef struct _WebKitWebContext WebKitWebContext;
typedef struct _WebKitImage      WebKitImage;
typedef enum { WEBKIT_LOAD_STARTED,WEBKIT_LOAD_REDIRECTED,WEBKIT_LOAD_COMMITTED,WEBKIT_LOAD_FINISHED } WebKitLoadEvent;
typedef enum { WEBKIT_SNAPSHOT_REGION_VISIBLE,WEBKIT_SNAPSHOT_REGION_FULL_DOCUMENT } WebKitSnapshotRegion;
typedef enum { WEBKIT_SNAPSHOT_OPTIONS_NONE=0 } WebKitSnapshotOptions;
typedef enum { WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER,WEBKIT_CACHE_MODEL_WEB_BROWSER,WEBKIT_CACHE_MODEL_DOCUMENT_BROWSER } WebKitCacheModel;
GType webkit_web_view_get_type(void);
#define WEBKIT_TYPE_WEB_VIEW (webkit_web_view_get_type())
#define WEBKIT_WEB_VIEW(o) ((WebKitWebView*)(o))
WebKitSettings *webkit_settings_new(void);
void webkit_settings_set_enable_webgl(WebKitSettings*,gboolean);
void webkit_settings_set_enable_webaudio(WebKitSettings*,gboolean);
void webkit_settings_set_enable_media(WebKitSettings*,gboolean);
void webkit_settings_set_enable_mediasource(WebKitSettings*,gboolean);
void webkit_settings_set_javascript_can_access_clipboard(WebKitSettings*,gboolean);
WebKitWebContext *webkit_web_context_new(void);
void webkit_web_context_set_cache_model(WebKitWebContext*,WebKitCacheModel);
void webkit_web_view_load_uri(WebKitWebView*,const char*);
void webkit_web_view_load_html(WebKitWebView*,const char*,const char*);
void webkit_web_view_get_snapshot(WebKitWebView*,WebKitSnapshotRegion,WebKitSnapshotOptions,GCancellable*,GAsyncReadyCallback,gpointer);
WebKitImage *webkit_web_view_get_snapshot_finish(WebKitWebView*,GAsyncResult*,GError**);
GBytes *webkit_image_as_bytes(WebKitImage*);
int webkit_image_get_width(WebKitImage*);
int webkit_image_get_height(WebKitImage*);
guint webkit_image_get_stride(WebKitImage*);
EOF
echo "OK"
