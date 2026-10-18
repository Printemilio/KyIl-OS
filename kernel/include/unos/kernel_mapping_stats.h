#ifndef UNOS_KERNEL_MAPPING_STATS_H
#define UNOS_KERNEL_MAPPING_STATS_H

#include <stdint.h>

/* Internal diagnostic counters; not a userspace ABI. */
typedef struct kernel_mapping_stats {
    uint64_t map_calls, unmap_calls, task_visits, distinct_spaces, duplicate_cr3;
    uint64_t sync_calls, root_slots_checked, root_stores, root_changes;
    uint64_t leaf_maps, leaf_unmaps, root_publications, registered_spaces;
} kernel_mapping_stats_t;

extern kernel_mapping_stats_t kernel_mapping_stats;
void vmm_kernel_mapping_stats_snapshot(kernel_mapping_stats_t *out, int reset);
void kernel_mapping_stats_report(int reset);

#endif
