#ifndef VEX_THREAD_CONTROL_ABI_H
#define VEX_THREAD_CONTROL_ABI_H
#include <stdint.h>
#include <unos/vex_event_abi.h>

/* Execution control within the caller's Process, never authority over an
 * arbitrary PID. Pause is counted and asynchronous; SNAPSHOT waits until a
 * user-return boundary has parked the context and its CPU has released it.
 * A blocked syscall is allowed to finish; no kernel transaction is frozen.
 * RESUME cancels one pause and makes the final paused context runnable.
 * No register mutation or foreign-process debug authority is implied. */
#define VEX_SYS_THREAD_CONTROL 231ull
#define VEX_THREAD_PAUSE 0u
#define VEX_THREAD_RESUME 1u
#define VEX_THREAD_SNAPSHOT 2u
#define VEX_THREAD_CYCLES 3u
#define VEX_THREAD_YIELD 4u
#define VEX_THREAD_LATENCY_CLASS 5u /* signed value -4..4; entitlement unchanged */
#define VEX_THREAD_AUTOMATIC_FLOWS 6u /* value 0 disables automatic urgency, 1 enables */
#define VEX_THREAD_EFFICIENCY 7u /* performance/efficiency scheduling preference */
#define VEX_THREAD_QOS_QUERY 8u /* signed class in byte 0; disabled/efficiency bits 8/9 */
#define VEX_THREAD_CPU_SET 9u /* value -> vex_thread_cpu_set, same-Process only */
/* Atomic exchange of an allowed CPU set, not a processor-group bit mask.
 * Sorted unique online CPU indices. Empty input queries without changing it.
 * Previous capacity must hold every online CPU; no truncation or fixed N.
 * This does not change Process entitlement or transfer execution authority. */
typedef struct vex_thread_cpu_set {
    uint32_t size,count,capacity,previous_count;
    const uint32_t *cpus;
    uint32_t *previous;
} vex_thread_cpu_set_t;
#define VEX_THREAD_START_PAUSED 1u
typedef struct vex_thread_control_req {
    uint32_t size,operation,tid,reserved;
    /* PAUSE input: maximum outstanding count (0 = UINT32_MAX); output:
       previous count. Other operations return accounted TSC cycles / yield
       or current count. Limits are caller policy, not a personality ABI. */
    uint64_t value;
    vex_event_user_context_t context;
    uint64_t fs_base,gs_base;
    uint8_t fxsave[512]; /* x86-64 FXSAVE legacy region, not an XSAVE ABI */
} vex_thread_control_req_t;
#endif
