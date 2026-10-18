#ifndef KYIL_MEMORY_VIEW_ABI_H
#define KYIL_MEMORY_VIEW_ABI_H
#include <stdint.h>
#define VEX_SYS_MEMORY_VIEW 229ull
#define VEX_MEMORY_VIEW_QUERY 1u
#define VEX_MEMORY_VIEW_READ 2u
#define VEX_MEMORY_VIEW_WRITE 3u
#define VEX_MEMORY_VIEW_ACCESS 4u
#define VEX_MEMORY_VIEW_MACHINE 5u
#define VEX_MEMORY_VIEW_DISCARD 6u
#define VEX_MEMORY_VIEW_PAGE 7u
#define VEX_MEMORY_VIEW_PATH 8u
#define VEX_MEMORY_VIEW_CAPACITY 9u
#define VEX_MEMORY_VIEW_CPU 10u
#define VEX_MEMORY_VIEW_USAGE 11u
/* Current-space private anonymous storage only. Both transitions preserve VA
 * ownership. COMMIT is demand-zero, DECOMMIT drops resident pages after TLB
 * shootdown. Neither applies to shared/file/device/JIT backings. */
#define VEX_MEMORY_VIEW_COMMIT 12u
#define VEX_MEMORY_VIEW_DECOMMIT 13u
/* Actual resident backing, never VA reservation promises or swap accounting.
 * Counts sample each subsystem under its owning lock. Private bytes exclude
 * shared/ORA-projected leaves; aliases are resident mappings, not unique RAM.
 * Materializations count newly populated leaves, including eager loader work.
 * Pool counters describe the native nonpageable kernel allocator. */
typedef struct vex_memory_usage {
    uint64_t physical_bytes,available_bytes,peak_used_bytes,cache_bytes;
    uint64_t allocator_bytes,owned_allocator_bytes,peak_owned_allocator_bytes;
    uint64_t resident_bytes,peak_resident_bytes,private_bytes,peak_private_bytes,materializations;
    uint32_t processes,threads,resource_handles,reserved;
    uint64_t private_charge_bytes,peak_private_charge_bytes,committed_bytes,peak_committed_bytes;
} vex_memory_usage_t;
typedef struct vex_cpu_cache_info {
    uint32_t level,type,line_bytes,ways;
    uint64_t bytes;
    uint32_t sharing_domain,reserved;
} vex_cpu_cache_info_t;
typedef struct vex_cpu_topology_info {
    uint32_t index,package,core,thread;
    uint32_t online,valid,cache_count,reserved;
    vex_cpu_cache_info_t caches[8];
} vex_cpu_topology_info_t;
typedef struct vex_memory_capacity {
    uint64_t physical_bytes,available_physical_bytes;
    uint64_t virtual_begin,virtual_end,available_virtual_bytes;
} vex_memory_capacity_t;
typedef struct vex_memory_page_info {
    uint32_t present,permissions,shared,mapping_refs;
} vex_memory_page_info_t;
#define VEX_MEMORY_COPY_MAX 1048576ull
/* Current Process only. Metadata query never materializes pages. No physical
 * addresses, ORA identities or foreign-process pointers cross this boundary. */
typedef struct vex_memory_view_req {
    uint32_t size,operation;
    uint64_t address,length;
    void *out;
    uint32_t permissions,reserved;
} vex_memory_view_req_t;
typedef struct vex_memory_view_info {
    uint64_t base,length,allocation_base;
    uint32_t permissions,kind,mapping_flags,present;
} vex_memory_view_info_t;
#define VEX_MEMORY_VIEW_FREE 0u
#define VEX_MEMORY_VIEW_RESERVED 1u
#define VEX_MEMORY_VIEW_MAPPED 2u
typedef struct vex_memory_machine_info {
    uint64_t user_limit,page_size;
    uint32_t online_cpus,flags;
} vex_memory_machine_info_t;
#define VEX_MEMORY_MACHINE_NX 1u
#define VEX_MEMORY_MACHINE_FXSAVE 2u
#endif
