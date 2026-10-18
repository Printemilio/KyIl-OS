#ifndef KYIL_PROCESS_RESOURCE_ABI_H
#define KYIL_PROCESS_RESOURCE_ABI_H
#include <stdint.h>
#define VEX_SYS_PROCESS_RESOURCE 228ull
#define VEX_PROCESS_LAUNCH 1u
#define VEX_PROCESS_QUERY 2u
#define VEX_PROCESS_WAIT 3u
#define VEX_PROCESS_TERMINATE 4u
#define VEX_PROCESS_RESUME 5u
#define VEX_PROCESS_VIEW 6u
#define VEX_PROCESS_SELF 7u
#define VEX_PROCESS_USAGE 8u
#define VEX_PROCESS_EXIT_ORDER 9u
#define VEX_PROCESS_ULTRA_OPEN 10u
#define VEX_PROCESS_ULTRA_QUERY 11u
#define VEX_PROCESS_ULTRA_OBSERVE 12u /* returns QUERY|WAIT process capability, not domain ownership */
/* ULTRA_OPEN: fd is a native executable resource, status is a root scope FD.
 * argv/envp/flags/rights/out/timeout are zero. Explicit grants, if any, use
 * child slots >=16; ingress is slot 8. A cache hit requires the same scope,
 * exact executable bytes and exact delegated resources. Sender identity is
 * authenticated on each message, even across explicitly delegated scopes.
 * The returned SEND|TRANSFER message capability owns one domain lifetime.
 * Last reference removes discovery and terminates/reclaims the service.
 * No PID, object ID, path or payload field grants domain authority.
 * ULTRA_QUERY: fd is that capability, out receives an observational snapshot. */
typedef struct vex_ultra_info {uint64_t identity,created_ns;uint32_t pid,clients;uint64_t opens;} vex_ultra_info_t;
/* Current Process lifecycle ordering: timeout_ns carries an unsigned 32-bit
 * rank, higher closes earlier. flags=0 sets, flags=1 queries scalar rank.
 * Default rank 0x80000000. A control capability is required. Once a power
 * transition starts, changes fail; no foreign PID-based control is implied. */
/* CPU nanoseconds distinguish actual ring-3 execution from syscall/exception
 * execution. Realtime timestamps are Unix nanoseconds; bit 0 marks a valid
 * birth time, bit 1 final completion. Data survives the last task via a query
 * capability; it never grants access to another address space. */
typedef struct vex_process_usage {uint64_t birth_ns,exit_ns,user_ns,kernel_ns;uint32_t flags,reserved;} vex_process_usage_t;
#define VEX_PROCESS_WAIT_THREAD 1u
#define VEX_PROCESS_WAIT_POLL 2u /* atomic completion check; never enqueue/sleep */
#define VEX_PROCESS_START_SUSPENDED 1u
#define VEX_PROCESS_RIGHT_QUERY 1u
#define VEX_PROCESS_RIGHT_WAIT 2u
#define VEX_PROCESS_RIGHT_TERMINATE 4u
#define VEX_PROCESS_RIGHT_RESUME 8u
#define VEX_PROCESS_RIGHT_TRANSFER 16u
#define VEX_PROCESS_RIGHT_ALL 31u
/* Launch a native executable resource, never a foreign binary translation.
 * Grants have exact child-local slots; duplicate slots and unauthorized
 * transfer are refused. argv/envp are copied before publication. Scalar return
 * is the new process capability FD (no output publication race). */
typedef struct vex_process_grant {int32_t fd,slot;uint32_t flags,reserved;} vex_process_grant_t;
typedef struct vex_process_resource_req {
    uint32_t size,operation;int32_t fd;uint32_t flags;
    const char *const *argv,*const *envp;
    const vex_process_grant_t *grants;
    uint32_t grant_count,rights;
    uint64_t timeout_ns;
    void *out;
    int32_t status;uint32_t reserved;
} vex_process_resource_req_t;
typedef struct vex_process_resource_info {uint32_t pid,tid,exited,suspended;int32_t status;uint32_t primary_exited;int32_t primary_status;uint32_t reserved;} vex_process_resource_info_t;
#endif
