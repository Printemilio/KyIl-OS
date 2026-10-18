#include <unos/security.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/scheduler.h>

#define SECURITY_AUDIT_CAPACITY 128u
#define SECURITY_MRAC_GUARD_SLOTS 16u
#define SECURITY_WATCHDOG_TRACK_SLOTS 64u
#define SECURITY_WATCHDOG_COUNT 2u
#define SECURITY_WATCHDOG_SCAN_SLEEP_TICKS 40ull
#define SECURITY_WATCHDOG_PEER_STALL_TICKS 320ull
#define SECURITY_MRAC_WINDOW_TICKS 80ull
#define SECURITY_MRAC_QUARANTINE_TICKS 24000ull
#define SECURITY_MRAC_WRITE_THRESHOLD 14u
#define SECURITY_MRAC_PATH_THRESHOLD 8u
#define SECURITY_MRAC_KILL_AFTER_QUARANTINE_EVENTS 1u

typedef struct security_mrac_guard {
    uint32_t pid;
    uint32_t uid;
    uint32_t last_path_hash;
    uint32_t write_count;
    uint32_t path_changes;
    uint32_t quarantine_events;
    uint64_t bytes_written;
    uint64_t window_start_ticks;
    uint64_t quarantine_until_ticks;
} security_mrac_guard_t;

typedef struct security_watchdog_track {
    uint32_t pid;
    uint32_t uid;
    uint8_t user_mode;
    uint32_t authorized_base_caps;
    uint32_t authorized_elevated_caps;
    uint64_t authorized_elevated_expires_tick;
} security_watchdog_track_t;

typedef struct security_watchdog_runtime {
    uint32_t pid;
    uint32_t restarts;
    uint64_t heartbeat_tick;
    uint64_t heartbeat_seq;
    uint64_t last_peer_stall_audit_tick;
    uint8_t online_audited;
    uint8_t peer_linked;
} security_watchdog_runtime_t;

static security_audit_event_t security_audit_events[SECURITY_AUDIT_CAPACITY];
static size_t security_audit_next_index = 0u;
static size_t security_audit_count = 0u;
static security_mrac_guard_t security_mrac_guards[SECURITY_MRAC_GUARD_SLOTS];
static security_watchdog_track_t security_watchdog_tracks[SECURITY_WATCHDOG_TRACK_SLOTS];
static security_watchdog_runtime_t security_watchdog_runtimes[SECURITY_WATCHDOG_COUNT];
static uint8_t security_watchdogs_started;

static void security_watchdog_a_loop(void);
static void security_watchdog_b_loop(void);

static void copy_text(char *dst, size_t capacity, const char *src) {
    size_t i = 0u;

    if (dst == NULL || capacity == 0u) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static uint32_t hash_text32(const char *text) {
    uint32_t hash = 2166136261u;

    if (text == NULL) {
        return 0u;
    }
    for (size_t i = 0u; text[i] != '\0'; ++i) {
        hash ^= (uint8_t)text[i];
        hash *= 16777619u;
    }
    return hash;
}

static security_watchdog_track_t *security_watchdog_find_track(uint32_t pid) {
    for (size_t i = 0u; i < SECURITY_WATCHDOG_TRACK_SLOTS; ++i) {
        if (security_watchdog_tracks[i].pid == pid) {
            return &security_watchdog_tracks[i];
        }
    }
    return NULL;
}

static security_watchdog_track_t *security_watchdog_find_or_allocate_track(uint32_t pid) {
    security_watchdog_track_t *free_slot = NULL;

    for (size_t i = 0u; i < SECURITY_WATCHDOG_TRACK_SLOTS; ++i) {
        security_watchdog_track_t *track = &security_watchdog_tracks[i];
        if (track->pid == pid) {
            return track;
        }
        if (track->pid == 0u && free_slot == NULL) {
            free_slot = track;
        }
    }

    if (free_slot != NULL) {
        free_slot->pid = pid;
        free_slot->uid = 0u;
        free_slot->user_mode = 0u;
        free_slot->authorized_base_caps = 0u;
        free_slot->authorized_elevated_caps = 0u;
        free_slot->authorized_elevated_expires_tick = 0u;
    }
    return free_slot;
}

static void security_watchdog_clear_track(security_watchdog_track_t *track) {
    if (track == NULL) {
        return;
    }
    track->pid = 0u;
    track->uid = 0u;
    track->user_mode = 0u;
    track->authorized_base_caps = 0u;
    track->authorized_elevated_caps = 0u;
    track->authorized_elevated_expires_tick = 0u;
}

static void security_watchdog_reset_runtime(size_t slot) {
    if (slot >= SECURITY_WATCHDOG_COUNT) {
        return;
    }
    security_watchdog_runtimes[slot].pid = 0u;
    security_watchdog_runtimes[slot].restarts = 0u;
    security_watchdog_runtimes[slot].heartbeat_tick = 0u;
    security_watchdog_runtimes[slot].heartbeat_seq = 0u;
    security_watchdog_runtimes[slot].last_peer_stall_audit_tick = 0u;
    security_watchdog_runtimes[slot].online_audited = 0u;
    security_watchdog_runtimes[slot].peer_linked = 0u;
}

static void security_watchdog_track_live_processes(void) {
    const uint64_t count = scheduler_process_count();
    scheduler_process_info_t info;

    for (uint64_t i = 0u; i < count; ++i) {
        if (scheduler_process_info_at(i, &info) != 0) {
            continue;
        }
        security_watchdog_note_process_state(info.pid, info.uid, info.user_mode,
                                             info.base_capabilities, info.elevated_capabilities,
                                             info.elevated_capabilities_expires_tick);
    }
}

static void security_watchdog_register_self(size_t slot) {
    security_watchdog_runtime_t *runtime;
    const uint32_t pid = scheduler_current_pid();

    if (slot >= SECURITY_WATCHDOG_COUNT || pid == 0u) {
        return;
    }

    runtime = &security_watchdog_runtimes[slot];
    if (runtime->pid == pid) {
        return;
    }
    runtime->pid = pid;
    runtime->heartbeat_tick = scheduler_ticks_now();
    runtime->heartbeat_seq = 0u;
    runtime->last_peer_stall_audit_tick = 0u;
    runtime->online_audited = 0u;
    runtime->peer_linked = 0u;
    security_watchdog_note_process_state(pid, scheduler_current_uid(), scheduler_current_user_mode(),
                                         scheduler_current_capabilities(), 0u, 0u);
}

static uint32_t security_watchdog_spawn_slot(size_t slot) {
    uint32_t pid = 0u;

    if (slot >= SECURITY_WATCHDOG_COUNT) {
        return 0u;
    }

    if (slot == 0u) {
        pid = process_create(security_watchdog_a_loop, 0u);
    } else {
        pid = process_create(security_watchdog_b_loop, 0u);
    }

    if (pid != 0u) {
        security_watchdog_runtimes[slot].pid = pid;
        ++security_watchdog_runtimes[slot].restarts;
    }
    return pid;
}

static void security_watchdog_kill_process(uint32_t pid) {
    if (pid == 0u) {
        return;
    }
    if (pid == scheduler_current_pid()) {
        process_exit_status(128 + 9);
    } else {
        (void)process_kill(pid);
    }
}

static void security_watchdog_enforce_track(const scheduler_process_info_t *info,
                                            security_watchdog_track_t *track) {
    uint32_t allowed_elevated;
    const uint64_t now = scheduler_ticks_now();
    const uint8_t quarantined = security_mrac_pid_quarantined(info->pid);

    if (info == NULL || track == NULL || info->pid == 0u) {
        return;
    }

    if (track->authorized_elevated_expires_tick != 0u &&
        track->authorized_elevated_expires_tick <= now) {
        track->authorized_elevated_caps = 0u;
        track->authorized_elevated_expires_tick = 0u;
    }

    allowed_elevated = track->authorized_elevated_caps;

    if (info->uid != track->uid ||
        info->base_capabilities != track->authorized_base_caps ||
        (info->elevated_capabilities & ~allowed_elevated) != 0u ||
        (quarantined != 0u && info->capabilities != 0u)) {
        const uint32_t actual_caps = info->capabilities;
        const uint32_t target_caps = quarantined != 0u ? 0u : track->authorized_base_caps;

        security_audit_log_event("wd-priv-drift", "capability", info->pid, info->uid, actual_caps, 0u);
        (void)scheduler_set_process_capabilities(info->pid, target_caps);
        security_audit_log_event("wd-priv-reset", "capability", info->pid, info->uid, target_caps, 1u);
        if (info->user_mode != 0u) {
            security_audit_log_event("wd-priv-kill", "capability", info->pid, info->uid, actual_caps, 0u);
            security_watchdog_kill_process(info->pid);
        }
    }
}

static void security_watchdog_scan_processes(size_t slot) {
    const uint64_t count = scheduler_process_count();
    scheduler_process_info_t info;

    (void)slot;
    for (uint64_t i = 0u; i < count; ++i) {
        security_watchdog_track_t *track;

        if (scheduler_process_info_at(i, &info) != 0) {
            continue;
        }
        if (info.pid == 0u) {
            continue;
        }

        track = security_watchdog_find_track(info.pid);
        if (track == NULL) {
            security_watchdog_note_process_state(info.pid, info.uid, info.user_mode,
                                                 info.base_capabilities, info.elevated_capabilities,
                                                 info.elevated_capabilities_expires_tick);
            track = security_watchdog_find_track(info.pid);
        }
        if (track == NULL) {
            continue;
        }

        security_watchdog_enforce_track(&info, track);
    }
}

static void security_watchdog_check_peer(size_t slot) {
    const size_t peer_slot = slot == 0u ? 1u : 0u;
    security_watchdog_runtime_t *self = &security_watchdog_runtimes[slot];
    security_watchdog_runtime_t *peer = &security_watchdog_runtimes[peer_slot];
    const uint64_t now = scheduler_ticks_now();

    if (peer->pid == 0u || !scheduler_process_alive(peer->pid)) {
        self->peer_linked = 0u;
        if (peer->pid != 0u) {
            security_audit_log_event("wd-peer-missing", peer_slot == 0u ? "watchdog-a" : "watchdog-b",
                                     scheduler_current_pid(), scheduler_current_uid(),
                                     scheduler_current_capabilities(), 0u);
        }
        security_watchdog_reset_runtime(peer_slot);
        if (security_watchdog_spawn_slot(peer_slot) != 0u) {
            security_audit_log_event("wd-peer-restart", peer_slot == 0u ? "watchdog-a" : "watchdog-b",
                                     scheduler_current_pid(), scheduler_current_uid(),
                                     scheduler_current_capabilities(), 1u);
        }
        return;
    }

    if (self->peer_linked == 0u) {
        self->peer_linked = 1u;
        security_audit_log_event("wd-peer-linked", peer_slot == 0u ? "watchdog-a" : "watchdog-b",
                                 scheduler_current_pid(), scheduler_current_uid(),
                                 scheduler_current_capabilities(), 1u);
    }

    if (peer->heartbeat_tick != 0u &&
        now > peer->heartbeat_tick &&
        now - peer->heartbeat_tick > SECURITY_WATCHDOG_PEER_STALL_TICKS &&
        (self->last_peer_stall_audit_tick == 0u ||
         now - self->last_peer_stall_audit_tick > SECURITY_WATCHDOG_PEER_STALL_TICKS)) {
        self->last_peer_stall_audit_tick = now;
        security_audit_log_event("wd-peer-stall", peer_slot == 0u ? "watchdog-a" : "watchdog-b",
                                 scheduler_current_pid(), scheduler_current_uid(),
                                 scheduler_current_capabilities(), 0u);
    }
}

static void security_watchdog_loop(size_t slot) {
    security_watchdog_register_self(slot);

    for (;;) {
        security_watchdog_runtime_t *runtime = &security_watchdog_runtimes[slot];

        security_watchdog_register_self(slot);
        if (runtime->online_audited == 0u) {
            runtime->online_audited = 1u;
            security_audit_log_event("wd-online", slot == 0u ? "watchdog-a" : "watchdog-b",
                                     scheduler_current_pid(), scheduler_current_uid(),
                                     scheduler_current_capabilities(), 1u);
        }
        runtime->heartbeat_tick = scheduler_ticks_now();
        ++runtime->heartbeat_seq;
        security_watchdog_check_peer(slot);
        security_watchdog_scan_processes(slot);
        process_sleep_ticks(SECURITY_WATCHDOG_SCAN_SLEEP_TICKS);
    }
}

static void security_watchdog_a_loop(void) {
    security_watchdog_loop(0u);
}

static void security_watchdog_b_loop(void) {
    security_watchdog_loop(1u);
}

static security_mrac_guard_t *find_or_allocate_guard(uint32_t pid) {
    security_mrac_guard_t *free_slot = NULL;
    const uint64_t now = scheduler_ticks_now();

    for (size_t i = 0u; i < SECURITY_MRAC_GUARD_SLOTS; ++i) {
        security_mrac_guard_t *guard = &security_mrac_guards[i];
        if (guard->pid == pid) {
            return guard;
        }
        if (guard->pid == 0u || (guard->quarantine_until_ticks != 0u && guard->quarantine_until_ticks <= now)) {
            free_slot = guard;
        }
    }

    if (free_slot != NULL) {
        free_slot->pid = pid;
        free_slot->uid = 0u;
        free_slot->last_path_hash = 0u;
        free_slot->write_count = 0u;
        free_slot->path_changes = 0u;
        free_slot->quarantine_events = 0u;
        free_slot->bytes_written = 0u;
        free_slot->window_start_ticks = now;
        free_slot->quarantine_until_ticks = 0u;
    }
    return free_slot;
}

uint32_t security_cap_sanitize(uint32_t caps) {
    return caps & VEX_CAP_ALL;
}

uint32_t security_default_caps_for_admin(uint8_t is_admin) {
    return is_admin != 0u ? VEX_CAP_ALL : 0u;
}

const char *security_cap_primary_name(uint32_t caps) {
    if ((caps & VEX_CAP_FS_ADMIN) != 0u) {
        return "fs_admin";
    }
    if ((caps & VEX_CAP_PROC_ADMIN) != 0u) {
        return "proc_admin";
    }
    if ((caps & VEX_CAP_NET_ADMIN) != 0u) {
        return "net_admin";
    }
    if ((caps & VEX_CAP_USER_ADMIN) != 0u) {
        return "user_admin";
    }
    if ((caps & VEX_CAP_AUDIT_ADMIN) != 0u) {
        return "audit_admin";
    }
    if ((caps & VEX_CAP_SYSTEM_ADMIN) != 0u) {
        return "system_admin";
    }
    if ((caps & VEX_CAP_BROWSER_ADMIN) != 0u) {
        return "browser_admin";
    }
    if ((caps & VEX_CAP_NET_CONNECT) != 0u) return "net_connect";
    return "none";
}

void security_audit_log_event(const char *action, const char *target, uint32_t pid, uint32_t uid, uint32_t caps, uint8_t allowed) {
    security_audit_event_t *event = &security_audit_events[security_audit_next_index];

    event->ticks = scheduler_ticks_now();
    event->pid = pid;
    event->uid = uid;
    event->caps = security_cap_sanitize(caps);
    event->allowed = allowed != 0u;
    copy_text(event->action, sizeof(event->action), action);
    copy_text(event->target, sizeof(event->target), target);

    security_audit_next_index = (security_audit_next_index + 1u) % SECURITY_AUDIT_CAPACITY;
    if (security_audit_count < SECURITY_AUDIT_CAPACITY) {
        ++security_audit_count;
    }
}

size_t security_audit_copy_recent(security_audit_event_t *out, size_t capacity) {
    size_t copied = 0u;
    size_t available = security_audit_count;

    if (out == NULL || capacity == 0u || available == 0u) {
        return 0u;
    }
    if (available > capacity) {
        available = capacity;
    }

    for (size_t i = 0u; i < available; ++i) {
        size_t index = (security_audit_next_index + SECURITY_AUDIT_CAPACITY - available + i) % SECURITY_AUDIT_CAPACITY;
        out[copied++] = security_audit_events[index];
    }
    return copied;
}

uint8_t security_mrac_pid_quarantined(uint32_t pid) {
    const uint64_t now = scheduler_ticks_now();

    if (pid == 0u) {
        return 0u;
    }
    for (size_t i = 0u; i < SECURITY_MRAC_GUARD_SLOTS; ++i) {
        security_mrac_guard_t *guard = &security_mrac_guards[i];
        if (guard->pid != pid) {
            continue;
        }
        if (guard->quarantine_until_ticks > now) {
            return 1u;
        }
        if (guard->quarantine_until_ticks != 0u) {
            guard->pid = 0u;
            guard->quarantine_until_ticks = 0u;
        }
        return 0u;
    }
    return 0u;
}

void security_mrac_reap_pid(uint32_t pid) {
    if (pid == 0u) {
        return;
    }

    for (size_t i = 0u; i < SECURITY_MRAC_GUARD_SLOTS; ++i) {
        security_mrac_guard_t *guard = &security_mrac_guards[i];
        if (guard->pid != pid) {
            continue;
        }

        guard->pid = 0u;
        guard->uid = 0u;
        guard->last_path_hash = 0u;
        guard->write_count = 0u;
        guard->path_changes = 0u;
        guard->quarantine_events = 0u;
        guard->bytes_written = 0u;
        guard->window_start_ticks = 0u;
        guard->quarantine_until_ticks = 0u;
        return;
    }
}

void security_watchdog_note_process_state(uint32_t pid, uint32_t uid, uint8_t user_mode,
                                          uint32_t base_caps, uint32_t elevated_caps,
                                          uint64_t elevated_expires_tick) {
    security_watchdog_track_t *track;

    if (pid == 0u) {
        return;
    }

    track = security_watchdog_find_or_allocate_track(pid);
    if (track == NULL) {
        return;
    }

    track->pid = pid;
    track->uid = uid;
    track->user_mode = user_mode != 0u;
    track->authorized_base_caps = security_cap_sanitize(base_caps);
    track->authorized_elevated_caps = security_cap_sanitize(elevated_caps);
    track->authorized_elevated_expires_tick = elevated_expires_tick;
}

void security_watchdog_reap_pid(uint32_t pid) {
    for (size_t i = 0u; i < SECURITY_WATCHDOG_TRACK_SLOTS; ++i) {
        if (security_watchdog_tracks[i].pid == pid) {
            security_watchdog_clear_track(&security_watchdog_tracks[i]);
            break;
        }
    }

    for (size_t i = 0u; i < SECURITY_WATCHDOG_COUNT; ++i) {
        if (security_watchdog_runtimes[i].pid == pid) {
            security_watchdog_reset_runtime(i);
        }
    }
}

void security_watchdogs_start(void) {
    if (security_watchdogs_started != 0u) {
        return;
    }

    security_watchdogs_started = 1u;
    security_watchdog_track_live_processes();
    (void)security_watchdog_spawn_slot(0u);
    (void)security_watchdog_spawn_slot(1u);
}

security_mrac_decision_t security_mrac_record_write(uint32_t pid, uint32_t uid, uint32_t caps, const char *path, size_t size) {
    security_mrac_guard_t *guard;
    const uint64_t now = scheduler_ticks_now();
    const uint32_t path_hash = hash_text32(path);

    if (pid == 0u || (caps & VEX_CAP_SYSTEM_ADMIN) != 0u) {
        return SECURITY_MRAC_ALLOW;
    }

    guard = find_or_allocate_guard(pid);
    if (guard == NULL) {
        return SECURITY_MRAC_ALLOW;
    }

    if (guard->quarantine_until_ticks > now) {
        return SECURITY_MRAC_QUARANTINE;
    }

    if (now - guard->window_start_ticks > SECURITY_MRAC_WINDOW_TICKS) {
        guard->uid = uid;
        guard->last_path_hash = 0u;
        guard->write_count = 0u;
        guard->path_changes = 0u;
        guard->quarantine_events = 0u;
        guard->bytes_written = 0u;
        guard->window_start_ticks = now;
    }

    guard->uid = uid;
    ++guard->write_count;
    guard->bytes_written += (uint64_t)size;
    if (guard->last_path_hash != 0u && guard->last_path_hash != path_hash) {
        ++guard->path_changes;
    }
    guard->last_path_hash = path_hash;

    if (guard->write_count >= SECURITY_MRAC_WRITE_THRESHOLD &&
        guard->path_changes >= SECURITY_MRAC_PATH_THRESHOLD) {
        guard->quarantine_until_ticks = now + SECURITY_MRAC_QUARANTINE_TICKS;
        guard->quarantine_events = 0u;
        security_audit_log_event("mrac-quarantine", path, pid, uid, caps, 0u);
        (void)scheduler_set_process_capabilities(pid, 0u);
        return SECURITY_MRAC_QUARANTINE;
    }

    if (guard->write_count + 2u >= SECURITY_MRAC_WRITE_THRESHOLD &&
        guard->path_changes + 1u >= SECURITY_MRAC_PATH_THRESHOLD) {
        security_audit_log_event("mrac-alert", path, pid, uid, caps, 0u);
        return SECURITY_MRAC_WARN;
    }

    return SECURITY_MRAC_ALLOW;
}

security_mrac_decision_t security_mrac_record_quarantine_activity(uint32_t pid, uint32_t uid, uint32_t caps,
                                                                  const char *action, const char *target) {
    security_mrac_guard_t *guard;
    const uint64_t now = scheduler_ticks_now();

    if (pid == 0u) {
        return SECURITY_MRAC_ALLOW;
    }

    guard = find_or_allocate_guard(pid);
    if (guard == NULL || guard->quarantine_until_ticks <= now) {
        return SECURITY_MRAC_ALLOW;
    }

    ++guard->quarantine_events;
    security_audit_log_event(action, target, pid, uid, caps, 0u);
    if (guard->quarantine_events >= SECURITY_MRAC_KILL_AFTER_QUARANTINE_EVENTS) {
        security_audit_log_event("mrac-kill", target, pid, uid, caps, 0u);
        if (pid == scheduler_current_pid()) {
            /* Called from inside file system operations: the process stops
               on its way back to user mode, after the caller's locks. */
            process_request_exit(128 + 9);
        } else {
            (void)process_kill(pid);
        }
        return SECURITY_MRAC_QUARANTINE;
    }

    return SECURITY_MRAC_WARN;
}
