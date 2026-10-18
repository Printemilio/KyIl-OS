#ifndef VEX_BROWSER_PROTOCOL_H
#define VEX_BROWSER_PROTOCOL_H
#include <unos/vex_abi.h>
#include <unos/browser_state.h>
/* One inherited, process-owned message endpoint. No global IDs/FD transfer. */
#define VEX_BROWSER_FRAME UINT64_C(0x5657504546524d31)
#define VEX_BROWSER_INPUT UINT64_C(0x56575045494e5031)
typedef struct vex_browser_frame {uint64_t magic,sequence;uint32_t width,height;} vex_browser_frame;
typedef struct vex_browser_input {uint64_t magic;vex_viewport_event_t event;} vex_browser_input;
/* Control is a separate reliable message channel. Each page has its own
 * renderer/input endpoint, transferred as an FD object attachment. */
#define VEX_BROWSER_CONTROL UINT64_C(0x5658425243544c31)
enum vex_browser_operation { VEX_BROWSER_CREATE=1, VEX_BROWSER_SELECT,
    VEX_BROWSER_CLOSE, VEX_BROWSER_NAVIGATE, VEX_BROWSER_BACK,
    VEX_BROWSER_FORWARD, VEX_BROWSER_RELOAD, VEX_BROWSER_STOP,
    VEX_BROWSER_READY, VEX_BROWSER_REPORT, VEX_BROWSER_PROBE, VEX_BROWSER_SHUTDOWN,
    /* Downloads. DOWNLOAD_REPORT flows from the runtime to the supervisor and
     * carries a complete vex_download_state_t in `download` (`id` repeats its
     * identity). DOWNLOAD_CANCEL flows the other way: `id` is the download. */
    VEX_BROWSER_DOWNLOAD_REPORT, VEX_BROWSER_DOWNLOAD_CANCEL };
typedef struct vex_browser_control {
    uint64_t magic;
    uint32_t operation, id;
    union {
        vex_browser_state_t state;
        vex_download_state_t download;
    };
} vex_browser_control;
#endif
