#ifndef UNOS_ELEVATION_H
#define UNOS_ELEVATION_H

#include <stdint.h>

#include <unos/vex_abi.h>

typedef struct elevation_prompt_view {
    uint32_t request_id;
    uint32_t pid;
    uint32_t uid;
    uint32_t token_hint;
    uint64_t created_ticks;
    uint64_t expires_ticks;
    char reason[96];
} elevation_prompt_view_t;

void elevation_init(void);
int elevation_request_create(const char *reason, uint32_t flags, uint32_t *request_id_out);
int elevation_query(uint32_t caller_pid, vex_elevation_status_req_t *status);
void elevation_tick(void);
void elevation_process_exit(uint32_t pid);
uint8_t elevation_has_active_prompt(void);
int elevation_copy_active_prompt(elevation_prompt_view_t *out);
int elevation_approve_active(void);
int elevation_deny_active(void);

#endif
