#ifndef KYIL_RENDEZVOUS_ABI_H
#define KYIL_RENDEZVOUS_ABI_H
#include <stdint.h>
#define VEX_SYS_RENDEZVOUS 227ull
#define VEX_RENDEZVOUS_PUBLISH 1u
#define VEX_RENDEZVOUS_CLAIM 2u
#define VEX_RENDEZVOUS_REARM 3u
#define VEX_RENDEZVOUS_QUERY 4u
#define VEX_RENDEZVOUS_WAIT 5u
#define VEX_RENDEZVOUS_ATTENUATE 6u
#define VEX_RENDEZVOUS_COMMIT 7u
#define VEX_RENDEZVOUS_CANCEL_ENDPOINT 8u
#define VEX_RENDEZVOUS_LEASE_STATE 9u
#define VEX_RENDEZVOUS_REVOKE_ENDPOINT 10u /* requires non-delegated endpoint REVOKE right */
#define VEX_RENDEZVOUS_EXCLUSIVE 1u
#define VEX_RENDEZVOUS_LABEL_MAX 512u
#define VEX_RENDEZVOUS_TRAITS_MAX 64u
#define VEX_ENDPOINT_SEND 1u
#define VEX_ENDPOINT_RECEIVE 2u
#define VEX_ENDPOINT_TRANSFER 4u
/* Opaque labels and traits, not filesystem names or personality structures.
 * Scope is a typed root capability: publish needs WRITE|CREATE; discovery
 * needs READ|ENUM. Publish delegates a native message endpoint, retaining its
 * root and backing. Returns a CLOEXEC registration FD. Its last close removes
 * the offer; clients already holding references keep their endpoint.
 * Each registration offers one claim until REARM(lease_fd). Closing an
 * uncommitted claim restores availability automatically; COMMIT(claim_fd)
 * confirms the handoff. Native fd views preserve the reservation lifetime.
 * Same labels may
 * have up to limit registrations with exactly matching traits/limits. Exclusive
 * publication rejects an existing group. CLAIM compares expected traits again
 * and returns a new rights-attenuated local FD. Never transfer an FD number.
 * QUERY copies traits/limit and returns 1 (available) or 2 (all claimed).
 * WAIT blocks for availability, 0 timeout means infinite. ATTENUATE(resource)
 * makes a local endpoint view; no cross-process authority is granted by it. */
typedef struct vex_rendezvous_req {
    uint32_t size,operation;
    int32_t scope_fd,resource_fd;
    uint32_t rights,flags,limit,label_size;
    const void *label;
    uint64_t timeout_ns;
    uint8_t traits[VEX_RENDEZVOUS_TRAITS_MAX];
    uint32_t traits_size,reserved;
} vex_rendezvous_req_t;
#endif
