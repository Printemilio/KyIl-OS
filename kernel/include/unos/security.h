#ifndef UNOS_SECURITY_H
#define UNOS_SECURITY_H

#include <stddef.h>
#include <stdint.h>

#define VEX_CAP_FS_ADMIN       (1u << 0)
#define VEX_CAP_PROC_ADMIN     (1u << 1)
#define VEX_CAP_NET_ADMIN      (1u << 2)
#define VEX_CAP_USER_ADMIN     (1u << 3)
#define VEX_CAP_AUDIT_ADMIN    (1u << 4)
#define VEX_CAP_SYSTEM_ADMIN   (1u << 5)
#define VEX_CAP_BROWSER_ADMIN  (1u << 6)
#define VEX_CAP_NET_CONNECT    (1u << 7) /* Outbound connections; no network administration. */
#define VEX_CAP_ALL (VEX_CAP_FS_ADMIN | VEX_CAP_PROC_ADMIN | VEX_CAP_NET_ADMIN | \
                     VEX_CAP_USER_ADMIN | VEX_CAP_AUDIT_ADMIN | VEX_CAP_SYSTEM_ADMIN | \
                     VEX_CAP_BROWSER_ADMIN | VEX_CAP_NET_CONNECT)

typedef struct security_audit_event {
    uint64_t ticks;
    uint32_t pid;
    uint32_t uid;
    uint32_t caps;
    uint8_t allowed;
    char action[24];
    char target[72];
} security_audit_event_t;

typedef enum security_mrac_decision {
    SECURITY_MRAC_ALLOW = 0,
    SECURITY_MRAC_WARN = 1,
    SECURITY_MRAC_QUARANTINE = 2
} security_mrac_decision_t;

uint32_t security_cap_sanitize(uint32_t caps);
uint32_t security_default_caps_for_admin(uint8_t is_admin);
const char *security_cap_primary_name(uint32_t caps);
void security_audit_log_event(const char *action, const char *target, uint32_t pid, uint32_t uid, uint32_t caps, uint8_t allowed);
size_t security_audit_copy_recent(security_audit_event_t *out, size_t capacity);
security_mrac_decision_t security_mrac_record_write(uint32_t pid, uint32_t uid, uint32_t caps, const char *path, size_t size);
uint8_t security_mrac_pid_quarantined(uint32_t pid);
void security_mrac_reap_pid(uint32_t pid);
security_mrac_decision_t security_mrac_record_quarantine_activity(uint32_t pid, uint32_t uid, uint32_t caps,
                                                                  const char *action, const char *target);
void security_watchdog_note_process_state(uint32_t pid, uint32_t uid, uint8_t user_mode,
                                          uint32_t base_caps, uint32_t elevated_caps,
                                          uint64_t elevated_expires_tick);
void security_watchdog_reap_pid(uint32_t pid);
void security_watchdogs_start(void);

#endif
