#ifndef UNOS_BROWSER_DOWNLOADS_H
#define UNOS_BROWSER_DOWNLOADS_H

#include <stdint.h>
#include <unos/browser_state.h>

/* State of the browser's file downloads as last reported by the browser
 * runtime. The kernel does not transfer or store anything itself: the file is
 * a normal VexFS file, written by the browser's own processes. This table only
 * lets the native shell and chrome show and cancel downloads. */

/* A new browser session starts with an empty table; identities restart. */
void browser_downloads_session_started(void);

/* The browser went away: downloads that were still running cannot continue. */
void browser_downloads_session_ended(void);

/* Record a report from the runtime. Returns 0, or a negative errno:
 * -22 for a malformed report, -28 when the table holds only running downloads.
 * Reports for a download that already reached a final state are ignored. */
int browser_downloads_update(const vex_download_state_t *state);

/* Copy up to `capacity` entries, oldest first. Returns how many were copied. */
unsigned browser_downloads_snapshot(vex_download_state_t *out, unsigned capacity);

/* Ask the browser to cancel a running download. Returns 0 when the request was
 * queued, -2 when no such running download exists, or the queue's error. */
int browser_downloads_request_cancel(uint32_t id);

#endif
