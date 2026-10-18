#include <unos/elevation.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/debug_log.h>
#include <unos/gui.h>
#include <unos/random.h>
#include <unos/irq.h>
#include <unos/security.h>
#include <unos/scheduler.h>

#define ELEVATION_MAX_REQUESTS 8u
#define ELEVATION_REASON_MAX 96u
#define ELEVATION_TIMEOUT_TICKS 3000ull
#define ELEVATION_GRANT_TICKS 12000ull

typedef struct elevation_request {
    uint8_t used;
    uint8_t active_prompt;
    uint8_t reserved0[2];
    uint32_t request_id;
    uint32_t pid;
    uint32_t uid;
    uint32_t state;
    uint32_t flags;
    uint32_t token_hint;
    uint64_t nonce;
    uint64_t created_ticks;
    uint64_t expires_ticks;
    char reason[ELEVATION_REASON_MAX];
} elevation_request_t;

static elevation_request_t elevation_requests[ELEVATION_MAX_REQUESTS];
static uint32_t elevation_next_request_id;
static uint64_t elevation_nonce_counter;
static int32_t elevation_active_index;

static void elevation_copy_reason(char *dst, size_t capacity, const char *src) {
    size_t i = 0u;

    if (dst == NULL || capacity == 0u) {
        return;
    }

    if (src == NULL || src[0] == '\0') {
        src = "Admin access requested";
    }

    while (src[i] != '\0' && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void elevation_log_u32(uint32_t value) {
    char digits[11];
    size_t count = 0u;

    if (value == 0u) {
        debug_log_puts("0");
        return;
    }

    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count > 0u) {
        char out[2];
        out[0] = digits[--count];
        out[1] = '\0';
        debug_log_puts(out);
    }
}

static void elevation_log_state(const char *prefix, const elevation_request_t *request) {
    if (request == NULL) {
        return;
    }

    debug_log_puts(prefix);
    debug_log_puts(" pid=");
    elevation_log_u32(request->pid);
    debug_log_puts(" req=");
    elevation_log_u32(request->request_id);
    debug_log_puts(" state=");
    elevation_log_u32(request->state);
    debug_log_puts("\n");
}

static elevation_request_t *elevation_find_by_request_id(uint32_t request_id) {
    for (uint32_t i = 0u; i < ELEVATION_MAX_REQUESTS; ++i) {
        if (elevation_requests[i].used != 0u && elevation_requests[i].request_id == request_id) {
            return &elevation_requests[i];
        }
    }
    return NULL;
}

static elevation_request_t *elevation_find_pending_for_pid(uint32_t pid) {
    for (uint32_t i = 0u; i < ELEVATION_MAX_REQUESTS; ++i) {
        if (elevation_requests[i].used != 0u &&
            elevation_requests[i].pid == pid &&
            elevation_requests[i].state == VEX_ELEVATION_STATE_PENDING) {
            return &elevation_requests[i];
        }
    }
    return NULL;
}

static elevation_request_t *elevation_find_free_slot(void) {
    for (uint32_t i = 0u; i < ELEVATION_MAX_REQUESTS; ++i) {
        if (elevation_requests[i].used == 0u ||
            elevation_requests[i].state == VEX_ELEVATION_STATE_APPROVED ||
            elevation_requests[i].state == VEX_ELEVATION_STATE_DENIED ||
            elevation_requests[i].state == VEX_ELEVATION_STATE_EXPIRED ||
            elevation_requests[i].state == VEX_ELEVATION_STATE_CANCELLED) {
            return &elevation_requests[i];
        }
    }
    return NULL;
}

static uint64_t elevation_next_nonce(uint32_t pid) {
    uint64_t value;
    if (kernel_random_try_fill(&value,sizeof(value)) != 0) return 0;

    value ^= ((uint64_t)pid << 32u);
    value ^= ++elevation_nonce_counter * 0x9e3779b97f4a7c15ull;
    return value;
}

static uint32_t elevation_requested_capabilities(uint32_t flags) {
    uint32_t caps = security_cap_sanitize(flags);

    return caps == 0u ? VEX_CAP_FS_ADMIN : caps;
}

static void elevation_notify_gui(void) {
    gui_elevation_prompt_changed();
}

static void elevation_clear_active_flag(void) {
    if (elevation_active_index >= 0 &&
        (uint32_t)elevation_active_index < ELEVATION_MAX_REQUESTS &&
        elevation_requests[elevation_active_index].used != 0u) {
        elevation_requests[elevation_active_index].active_prompt = 0u;
    }
    elevation_active_index = -1;
}

static void elevation_activate_next_pending(void) {
    elevation_clear_active_flag();

    for (uint32_t i = 0u; i < ELEVATION_MAX_REQUESTS; ++i) {
        elevation_request_t *request = &elevation_requests[i];
        if (request->used == 0u || request->state != VEX_ELEVATION_STATE_PENDING) {
            continue;
        }
        if (!scheduler_process_alive(request->pid)) {
            request->state = VEX_ELEVATION_STATE_CANCELLED;
            continue;
        }
        request->active_prompt = 1u;
        elevation_active_index = (int32_t)i;
        break;
    }
}

static void elevation_finish_request(elevation_request_t *request, uint32_t new_state) {
    if (request == NULL) {
        return;
    }

    request->state = new_state;
    request->active_prompt = 0u;
    process_wake(request->pid);
    elevation_log_state("elevation:", request);
    elevation_activate_next_pending();
    elevation_notify_gui();
}

static void elevation_expire_old_requests(void) {
    const uint64_t now = scheduler_ticks_now();
    uint8_t changed = 0u;

    for (uint32_t i = 0u; i < ELEVATION_MAX_REQUESTS; ++i) {
        elevation_request_t *request = &elevation_requests[i];

        if (request->used == 0u || request->state != VEX_ELEVATION_STATE_PENDING) {
            continue;
        }
        if (request->expires_ticks > now) {
            continue;
        }

        request->state = VEX_ELEVATION_STATE_DENIED;
        request->active_prompt = 0u;
        process_wake(request->pid);
        changed = 1u;
        elevation_log_state("elevation:", request);
    }

    if (changed != 0u) {
        elevation_activate_next_pending();
        elevation_notify_gui();
    }
}

void elevation_init(void) {
    memset(elevation_requests, 0, sizeof(elevation_requests));
    elevation_next_request_id = 1u;
    elevation_nonce_counter = 0u;
    elevation_active_index = -1;
}

int elevation_request_create(const char *reason, uint32_t flags, uint32_t *request_id_out) {
    process_t *process = scheduler_current_process();
    elevation_request_t *request;
    const uint32_t requested_caps = elevation_requested_capabilities(flags);
    const uint32_t owner_pid = scheduler_current_process_id();

    if (request_id_out == NULL || process == NULL || process->user_mode == 0u) {
        return -1;
    }

    if (scheduler_current_has_capability(requested_caps)) {
        request = elevation_find_free_slot();
        if (request == NULL) {
            return -1;
        }
        memset(request, 0, sizeof(*request));
        request->used = 1u;
        request->request_id = elevation_next_request_id++;
        request->pid = owner_pid;
        request->uid = process->uid;
        request->state = VEX_ELEVATION_STATE_APPROVED;
        request->flags = requested_caps;
        request->created_ticks = scheduler_ticks_now();
        request->expires_ticks = request->created_ticks;
        request->nonce = elevation_next_nonce(owner_pid);
        if (!request->nonce) { request->used=0u;return -1; }
        request->token_hint = (uint32_t)(request->nonce ^ (request->nonce >> 32u));
        elevation_copy_reason(request->reason, sizeof(request->reason), reason);
        *request_id_out = request->request_id;
        elevation_log_state("elevation: already-admin", request);
        security_audit_log_event("elev-auto", request->reason, request->pid, request->uid, requested_caps, 1u);
        return 0;
    }

    request = elevation_find_pending_for_pid(owner_pid);
    if (request != NULL) {
        *request_id_out = request->request_id;
        return 0;
    }

    request = elevation_find_free_slot();
    if (request == NULL) {
        return -1;
    }

    memset(request, 0, sizeof(*request));
    request->used = 1u;
    request->request_id = elevation_next_request_id++;
    request->pid = owner_pid;
    request->uid = process->uid;
    request->state = VEX_ELEVATION_STATE_PENDING;
    request->flags = requested_caps;
    request->created_ticks = scheduler_ticks_now();
    request->expires_ticks = request->created_ticks + ELEVATION_TIMEOUT_TICKS;
    request->nonce = elevation_next_nonce(owner_pid);
    if (!request->nonce) { request->used=0u;return -1; }
    request->token_hint = (uint32_t)(request->nonce ^ (request->nonce >> 32u));
    elevation_copy_reason(request->reason, sizeof(request->reason), reason);
    *request_id_out = request->request_id;
    security_audit_log_event("elev-req", request->reason, request->pid, request->uid, requested_caps, 1u);

    if (elevation_active_index < 0) {
        elevation_activate_next_pending();
        elevation_notify_gui();
    }
    return 0;
}

int elevation_query(uint32_t caller_pid, vex_elevation_status_req_t *status) {
    elevation_request_t *request;

    if (status == NULL || status->request_id == 0u) {
        return -1;
    }

    elevation_expire_old_requests();
    request = elevation_find_by_request_id(status->request_id);
    if (request == NULL) {
        return -1;
    }

    if (request->pid != caller_pid && !scheduler_current_has_capability(VEX_CAP_AUDIT_ADMIN)) {
        return -1;
    }

    status->pid = request->pid;
    status->flags = request->flags;
    status->state = request->state;
    status->created_ticks = request->created_ticks;
    status->expires_ticks = request->expires_ticks;
    status->capabilities = scheduler_current_capabilities();
    status->is_admin = scheduler_current_is_admin();
    memset(status->reserved, 0, sizeof(status->reserved));
    return 0;
}

void elevation_tick(void) {
    elevation_expire_old_requests();
}

void elevation_process_exit(uint32_t pid) {
    uint8_t changed = 0u;

    for (uint32_t i = 0u; i < ELEVATION_MAX_REQUESTS; ++i) {
        elevation_request_t *request = &elevation_requests[i];

        if (request->used == 0u || request->pid != pid || request->state != VEX_ELEVATION_STATE_PENDING) {
            continue;
        }

        request->state = VEX_ELEVATION_STATE_CANCELLED;
        request->active_prompt = 0u;
        changed = 1u;
        elevation_log_state("elevation:", request);
    }

    if (changed != 0u) {
        elevation_activate_next_pending();
        elevation_notify_gui();
    }
}

uint8_t elevation_has_active_prompt(void) {
    elevation_expire_old_requests();
    return elevation_active_index >= 0 ? 1u : 0u;
}

int elevation_copy_active_prompt(elevation_prompt_view_t *out) {
    elevation_request_t *request;

    if (out == NULL) {
        return -1;
    }
    elevation_expire_old_requests();
    if (elevation_active_index < 0 || (uint32_t)elevation_active_index >= ELEVATION_MAX_REQUESTS) {
        return -1;
    }

    request = &elevation_requests[elevation_active_index];
    if (request->used == 0u || request->state != VEX_ELEVATION_STATE_PENDING) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->request_id = request->request_id;
    out->pid = request->pid;
    out->uid = request->uid;
    out->token_hint = request->token_hint;
    out->created_ticks = request->created_ticks;
    out->expires_ticks = request->expires_ticks;
    elevation_copy_reason(out->reason, sizeof(out->reason), request->reason);
    return 0;
}

int elevation_approve_active(void) {
    elevation_request_t *request;

    elevation_expire_old_requests();
    if (elevation_active_index < 0 || (uint32_t)elevation_active_index >= ELEVATION_MAX_REQUESTS) {
        return -1;
    }

    request = &elevation_requests[elevation_active_index];
    if (request->used == 0u || request->state != VEX_ELEVATION_STATE_PENDING) {
        return -1;
    }

    if (scheduler_grant_process_capabilities(request->pid, request->flags, ELEVATION_GRANT_TICKS) != 0) {
        security_audit_log_event("elev-approve", request->reason, request->pid, request->uid, request->flags, 0u);
        elevation_finish_request(request, VEX_ELEVATION_STATE_CANCELLED);
        return -1;
    }

    security_audit_log_event("elev-approve", request->reason, request->pid, request->uid, request->flags, 1u);
    elevation_finish_request(request, VEX_ELEVATION_STATE_APPROVED);
    return 0;
}

int elevation_deny_active(void) {
    elevation_request_t *request;

    elevation_expire_old_requests();
    if (elevation_active_index < 0 || (uint32_t)elevation_active_index >= ELEVATION_MAX_REQUESTS) {
        return -1;
    }

    request = &elevation_requests[elevation_active_index];
    if (request->used == 0u || request->state != VEX_ELEVATION_STATE_PENDING) {
        return -1;
    }

    security_audit_log_event("elev-deny", request->reason, request->pid, request->uid, request->flags, 0u);
    elevation_finish_request(request, VEX_ELEVATION_STATE_DENIED);
    return 0;
}
