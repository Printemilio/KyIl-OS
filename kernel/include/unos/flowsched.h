#ifndef UNOS_FLOWSCHED_H
#define UNOS_FLOWSCHED_H
#include <stdint.h>
/* Internal scheduling contract, not a userspace ABI. All mutations serialized
 * by the owning runqueue. Multi-CPU domains additionally serialize the short
 * shared fairness/dependency transaction; this lock is never held in execution. */
#define FLOW_SLICE_NS 2000000ull
#define FLOW_LAG_NS 4000000ll
enum flow_origin { FLOW_NONE, FLOW_INPUT, FLOW_TIMER, FLOW_IO, FLOW_IPC, FLOW_EVENT, FLOW_SURFACE };
typedef struct flow_context {
    uint64_t id, born_ns, deadline_ns;
    uint32_t origin, origin_tid;
} flow_context_t;
typedef struct flow_node {
    struct flow_node *left,*right,*best;
    int64_t service, deadline;
    uint64_t sequence;
    uint32_t rank;
    uint8_t linked;
    void *owner;
} flow_node_t;
typedef struct flow_index {
    flow_node_t *root;
    __int128 sum; /* aggregation must not overflow as the runnable set grows */
    int64_t floor;
    uint32_t count;
} flow_index_t;
struct flow_cpu;
struct flow_group;
typedef struct flow_group_view {
    flow_node_t entity;
    flow_index_t threads;
    struct flow_group *group;
    struct flow_cpu *cpu;
} flow_group_view_t;
typedef struct flow_domain {
    flow_index_t groups; /* one entitlement entity per Process, not per CPU */
    struct flow_cpu **cpus;
    uint32_t cpu_count;
    uint64_t sequence,flow_sequence,fair_clock;
} flow_domain_t;

typedef struct flow_group {
    flow_node_t entity;
    flow_index_t threads;
    uint64_t cpu_ns, entitlement_ns, entitlement_stamp, thread_clock;
    flow_group_view_t *views;
    uint32_t view_count;
} flow_group_t;
typedef struct flow_thread {
    flow_node_t entity;
    flow_node_t local_entity;
    struct flow_cpu *cpu;
    uint32_t running_cpu; /* UINT32_MAX until a CPU claims this context */
    uint32_t pinned_cpu; /* internal affinity: UINT32_MAX permits any CPU */
    uint32_t *allowed_cpus,allowed_count; /* NULL = all; sorted dynamic set */
    flow_group_t *group;
    flow_group_t private_group; /* resource-less kernel tasks still get a group */
    flow_context_t local, effective;
    struct flow_thread *blocker,*donors,*donor_next;
    uint64_t cpu_ns, entitlement_ns, entitlement_stamp, runnable_ns, selected_ns;
    uint64_t sleep_deadline, wait_order;
    uint64_t latency_hist[32], latency_max_ns, running_ns;
    uint64_t input_at_ns,input_hist[32],input_max_ns;
    struct flow_thread *timer_left,*timer_right;
    uint32_t timer_rank;
    uint8_t runnable, sleeping;
    int8_t latency_class;
    uint8_t suppress_automatic,efficiency;
} flow_thread_t;
/* Process entitlement is unchanged. QoS changes virtual request deadlines,
 * never eligibility or group weight. Dependency donations remain mandatory. */
int flow_set_qos(struct flow_cpu *,flow_thread_t *,int,int,int,uint64_t);
typedef struct flow_cpu {
    flow_index_t groups;
    flow_thread_t *timers;
    uint64_t sequence, flow_sequence, switches, propagated, inversions, cycles;
    uint64_t latency_hist[32], latency_max_ns, wakes, dependency_cycles;
    uint64_t accounting_ns, fair_clock;
    uint32_t cpu_id;
    flow_domain_t *domain;
} flow_cpu_t;
void flow_cpu_init(flow_cpu_t *cpu);
/* Storage belongs to the scheduler and follows group/domain lifetime. N is
 * arbitrary and dynamic; no public ABI or CPU bit mask is introduced here. */
void flow_domain_init(flow_domain_t *domain,flow_cpu_t **cpus,uint32_t count);
int flow_cpu_join(flow_cpu_t *cpu,flow_domain_t *domain,uint32_t index);
int flow_group_bind_views(flow_group_t *group,flow_group_view_t *views,uint32_t count);
int flow_migrate(flow_cpu_t *source,flow_cpu_t *destination,flow_thread_t *thread);
int flow_claim(flow_cpu_t *cpu,flow_thread_t *thread);
int flow_cpu_allowed(const flow_thread_t *thread,uint32_t index);
void flow_unclaim(flow_thread_t *thread);
void flow_thread_init(flow_cpu_t *cpu,flow_thread_t *thread,flow_group_t *group,void *owner);
void flow_rebind(flow_cpu_t *cpu,flow_thread_t *thread,flow_group_t *group,uint64_t now);
void flow_runnable(flow_cpu_t *cpu,flow_thread_t *thread,uint64_t now);
void flow_block(flow_cpu_t *cpu,flow_thread_t *thread);
void flow_charge(flow_cpu_t *cpu,flow_thread_t *thread,uint64_t elapsed,uint64_t now);
flow_thread_t *flow_pick(flow_cpu_t *cpu);
void flow_selected(flow_cpu_t *cpu,flow_thread_t *thread,uint64_t now);
flow_context_t flow_new(flow_cpu_t *cpu,uint32_t origin,uint32_t tid,uint64_t now,uint64_t budget);
void flow_adopt(flow_cpu_t *cpu,flow_thread_t *thread,flow_context_t context,uint64_t now);
int flow_depend(flow_cpu_t *cpu,flow_thread_t *waiter,flow_thread_t *blocker,uint64_t now);
void flow_undepend(flow_cpu_t *cpu,flow_thread_t *waiter,uint64_t now);
void flow_forget(flow_cpu_t *cpu,flow_thread_t *thread,uint64_t now);
void flow_timer_arm(flow_cpu_t *cpu,flow_thread_t *thread,uint64_t deadline);
void flow_timer_cancel(flow_cpu_t *cpu,flow_thread_t *thread);
flow_thread_t *flow_timer_pop(flow_cpu_t *cpu,uint64_t now);
int flow_wait_precedes(const flow_thread_t *a,const flow_thread_t *b,uint64_t now);
uint64_t flow_latency_percentile(const flow_cpu_t *cpu,unsigned percentile);
/* Read-only diagnostic; caller holds the scheduling metadata transaction. */
int flow_validate(const flow_cpu_t *cpu);
#endif
