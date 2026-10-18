#ifndef VEX_WEBKIT_DOWNLOADS_H
#define VEX_WEBKIT_DOWNLOADS_H

/*
 * Download manager of the Vex browser runtime (Linux ELF, WPEWebKit).
 *
 * WebKit performs the transfer: its NetworkProcess streams the response body
 * to the destination we choose, chunk by chunk, through the ordinary file
 * API. This module owns policy only:
 *
 *   - where the file goes (the Downloads directory, a safe, unique name);
 *   - one explicit state per download (running / finished / failed /
 *     cancelled) with received and total byte counts;
 *   - publishing: data goes to "<name>.vexpart" and is renamed to its final
 *     name only after WebKit reports success, so an interrupted download never
 *     leaves a plausible-looking partial file behind;
 *   - cancellation and teardown, which remove the partial file.
 *
 * Nothing here ever opens, executes or inspects downloaded content.
 *
 * Everything runs on the GLib main thread; there is no locking.
 */

#include <glib.h>
#include <stdint.h>

#include <unos/browser_state.h>

typedef struct _WebKitNetworkSession WebKitNetworkSession;
typedef struct _WebKitWebView WebKitWebView;

/* Maps the view that started a download to the native page identity (0 when
 * the view is unknown, for example because the page was already closed). */
typedef uint32_t (*vex_downloads_page_lookup)(WebKitWebView *view);

typedef enum {
    VEX_DOWNLOAD_REPORT_DELIVERED,
    VEX_DOWNLOAD_REPORT_WOULD_BLOCK,    /* retry on the next flush */
    VEX_DOWNLOAD_REPORT_FAILED          /* the channel is unusable */
} vex_download_report_result;

typedef vex_download_report_result (*vex_download_report_fn)(
    const vex_download_state_t *state, void *context);

/* Start observing the session. Removes partial files left by a previous run
 * that could not clean up (crash, kill). */
void vex_downloads_init(WebKitNetworkSession *session, vex_downloads_page_lookup lookup);

/* Response policy for a page: content the view cannot display, or that the
 * server marks as an attachment, becomes a download instead of a failed load.
 * `decision` is the WebKitResponsePolicyDecision. Returns TRUE when the
 * decision was taken here, FALSE to leave it to WebKit's default. */
gboolean vex_downloads_handle_response(gpointer decision);

/* Deliver pending state changes. Progress-only updates are rate limited;
 * final states are delivered immediately. Returns -1 when `report` fails. */
int vex_downloads_flush(vex_download_report_fn report, void *context);

/* Request cancellation. Unknown or already finished identities are ignored. */
void vex_downloads_cancel(uint32_t id);

/* Cancel everything still running and wait (bounded) for WebKit to release the
 * partial files, then delete them. Safe to call once before exit. */
void vex_downloads_shutdown(void);

#endif
