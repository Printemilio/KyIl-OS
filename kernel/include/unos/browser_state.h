#ifndef UNOS_BROWSER_STATE_H
#define UNOS_BROWSER_STATE_H
#include <stdint.h>
/* Native chrome state. These are page identities, not PIDs or capabilities. */
#define VEX_BROWSER_MAX_TABS 8u
#define VEX_BROWSER_URI_SIZE 1024u
#define VEX_BROWSER_TITLE_SIZE 128u
#define VEX_BROWSER_ERROR_SIZE 256u
#define VEX_BROWSER_LOADING 1u
#define VEX_BROWSER_CAN_BACK 2u
#define VEX_BROWSER_CAN_FORWARD 4u
#define VEX_BROWSER_FAILED 8u
/* The navigation did not produce a page: the response became a download. `uri`
 * is the page that was already committed, or empty when there was none. The
 * chrome then shows that page again, or its native home. */
#define VEX_BROWSER_DIVERTED 16u
typedef struct vex_browser_state {
    uint32_t id, flags;
    char uri[VEX_BROWSER_URI_SIZE];
    char title[VEX_BROWSER_TITLE_SIZE];
    char error[VEX_BROWSER_ERROR_SIZE];
} vex_browser_state_t;

/* File downloads. A download belongs to the browser session, not to a tab:
 * closing the tab that started it does not interrupt it. Identities are
 * assigned by the browser runtime and are only meaningful within one session. */
#define VEX_BROWSER_MAX_DOWNLOADS 16u
#define VEX_DOWNLOAD_NAME_SIZE 96u
#define VEX_DOWNLOAD_PATH_SIZE 128u   /* The VexFS path contract, including NUL. */
#define VEX_DOWNLOAD_ERROR_SIZE 128u
#define VEX_DOWNLOAD_SIZE_UNKNOWN 0ull
enum vex_download_status {
    VEX_DOWNLOAD_RUNNING = 1,   /* Accepted; data may not have arrived yet. */
    VEX_DOWNLOAD_FINISHED,      /* The final file exists at `destination`. */
    VEX_DOWNLOAD_FAILED,        /* `error` explains; no final file was created. */
    VEX_DOWNLOAD_CANCELLED      /* Stopped on request; no final file was created. */
};
typedef struct vex_download_state {
    uint32_t id, tab_id, status, reserved;
    uint64_t received_bytes;
    uint64_t total_bytes;       /* VEX_DOWNLOAD_SIZE_UNKNOWN when not announced. */
    char name[VEX_DOWNLOAD_NAME_SIZE];
    char destination[VEX_DOWNLOAD_PATH_SIZE];
    char error[VEX_DOWNLOAD_ERROR_SIZE];
} vex_download_state_t;
static inline int vex_download_status_is_final(uint32_t status) {
    return status == VEX_DOWNLOAD_FINISHED || status == VEX_DOWNLOAD_FAILED ||
           status == VEX_DOWNLOAD_CANCELLED;
}
#endif
