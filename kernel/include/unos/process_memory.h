#ifndef UNOS_PROCESS_MEMORY_H
#define UNOS_PROCESS_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <unos/ora.h>
#include <unos/kernel_guard.h>
#include <unos/vex_memory_view_abi.h>

/* Kernel-only transaction: inventory and its PTE publication are one update.
   No scheduling or blocking operation may occur while this guard is held. */
kernel_guard_t process_memory_begin_update(void);
void process_memory_end_update(kernel_guard_t *);

typedef struct process process_t;
struct vex_web_fd_object;
void process_memory_file_object_release(struct vex_web_fd_object *object);
void process_memory_file_object_cache_copy(struct vex_web_fd_object *object,
    uint64_t offset, void *bytes, size_t count, int writing);
void process_memory_file_object_truncate(struct vex_web_fd_object *object, uint64_t size);

#define PROCESS_MEMORY_REGION_MAGIC 0x4d5241435245474eull
#define PROCESS_MEMORY_FLAG_HEAP    0x1u
#define PROCESS_MEMORY_FLAG_STACK   0x2u
#define PROCESS_MEMORY_FLAG_ORA_DIRECT 0x4u
/* Caller's data buffer is transient (freed after registration) — the region
 * must copy it instead of direct-mapping the source pages. */
#define PROCESS_MEMORY_FLAG_NO_DIRECT   0x8u
/* The page tables contain the backing for this mapping.  The process-memory
 * inventory owns the mapping lifetime, while the physical pages are released
 * by the inventory teardown (before the address space is destroyed). */
#define PROCESS_MEMORY_FLAG_VMM_DIRECT  0x10u
/* MAP_SHARED mappings refer to a refcounted process-memory backing object. */
#define PROCESS_MEMORY_FLAG_SHARED       0x20u
/* A PROT_NONE reservation has an inventory entry but no physical backing. */
#define PROCESS_MEMORY_FLAG_RESERVATION  0x40u
/* JIT aliases are backed by the JIT arena, which owns their physical pages. */
#define PROCESS_MEMORY_FLAG_JIT          0x80u
/* Anonymous mappings may reserve a large VA range without backing pages.
 * Pages are allocated by the user page-fault path on first access. */
#define PROCESS_MEMORY_FLAG_LAZY         0x100u
/* A lazy mapping whose bytes come from a retained file object. */
#define PROCESS_MEMORY_FLAG_FILE_BACKED  0x200u
/* A shared mapping created from a Surface capability has an immutable maximum
 * protection mask in reserved0; mprotect may narrow it, never widen it. */
#define PROCESS_MEMORY_FLAG_SURFACE      0x400u

typedef struct process_memory_region {
    uint64_t magic;
    uint64_t addr;
    size_t length;
    int prot;
    int flags;
    uint32_t kind_flags;
    uint32_t reserved0; /* maximum protection for PROCESS_MEMORY_FLAG_SURFACE */
    ora_view_t content; /* immutable source/view, explicitly pinned by inventory */
    uint64_t allocation_base;
    size_t allocation_size; /* logical native heap size, owned by process_memory */
    uint64_t shared_id;
    uint64_t shared_offset;
    uint64_t file_backing_id;
    uint64_t file_offset;
} process_memory_region_t;

void process_memory_reap(uint32_t owner);
/* Final address-space teardown: mapped active inventory first, then unpublished
   construction inventories, before VMM retires the page tables. */
void process_memory_reap_mappings(uint32_t owner, uint64_t cr3);
int process_memory_inventory_test(void);
int process_memory_index_test(void);
int process_memory_acquire_content(uint32_t pid, uint64_t address, ora_view_t *out);
int process_memory_prepare_write(uint32_t pid,uint64_t cr3,uint64_t addr,size_t length);
int process_memory_set_allocation_size(uint32_t pid, uint64_t address, size_t size);
/* Metadata copy only: content.object is borrowed. Use acquire_content for
   an owning lease before accessing content bytes outside a transaction. */
int process_memory_get_region(uint32_t owner, uint64_t address, process_memory_region_t *out);
int process_memory_query_view(uint32_t owner,uint64_t address,vex_memory_view_info_t *out);
int process_memory_discard_private(uint32_t owner,uint64_t cr3,uint64_t addr,uint64_t length);
int process_memory_private_state(uint32_t owner,uint64_t cr3,uint64_t addr,uint64_t length,int prot,int commit);
uint64_t process_memory_available_va(uint32_t owner,uint64_t begin,uint64_t end);
int process_memory_commit_usage(uint32_t owner,uint64_t *current,uint64_t *peak);
int process_memory_page_info(uint32_t owner,uint64_t cr3,uint64_t addr,vex_memory_page_info_t *out);
/* The region containing `address` (profiler/diagnostics): its start, kind flags
 * and, when file-backed, the offset of its first byte and the file's path. */
int process_memory_describe_address(uint32_t owner, uint64_t address, uint64_t *base,
                                    uint32_t *kind_flags, uint64_t *file_offset,
                                    char *path, size_t path_capacity);
int process_memory_namespace_begin(uint32_t pid, uint64_t alloc_hint, uint64_t *namespace_id_out);
int process_memory_namespace_commit(uint32_t pid, uint64_t namespace_id);
int process_memory_namespace_destroy(uint32_t pid, uint64_t namespace_id, uint64_t cr3);
uint64_t process_memory_current_namespace(uint32_t pid);
int process_memory_snapshot_namespace(uint32_t pid, uint64_t source_namespace_id, uint64_t *snapshot_namespace_id_out);
int process_memory_register_region_in_namespace(uint32_t pid, uint64_t namespace_id, uint64_t addr, size_t length, int prot, int flags, uint32_t kind_flags, const void *data);
int process_memory_register_region_in_namespace_partial(uint32_t pid, uint64_t namespace_id, uint64_t addr, size_t length, int prot, int flags, uint32_t kind_flags, const void *data, size_t data_size);
int process_memory_register_mapping(uint32_t pid, uint64_t addr, size_t length, int prot, int flags, uint32_t kind_flags);
int process_memory_file_map(uint32_t pid, uint64_t cr3, uint64_t addr,
                            size_t length, int prot, int flags, int fd,
                            uint64_t file_offset, size_t file_length);
int process_memory_adopt_shared_mapping(uint32_t pid, uint64_t cr3, uint64_t addr);
int process_memory_shared_create(size_t length, uint64_t *id_out);
int process_memory_shared_create_lazy(size_t length, uint64_t *id_out);
int process_memory_shared_create_contiguous(size_t length, uint64_t *id_out,
                                            void **kernel_mapping_out);
int process_memory_shared_get_contiguous(uint64_t id, size_t offset,
                                         size_t length, void **mapping_out);
int process_memory_shared_map(uint32_t pid, uint64_t cr3, uint64_t id,
                             uint64_t addr, size_t length, size_t offset, int prot);
int process_memory_shared_map_lazy(uint32_t pid, uint64_t cr3, uint64_t id,
                                   uint64_t addr, size_t length, size_t offset,
                                   int prot);
int process_memory_shared_map_surface(uint32_t pid, uint64_t cr3, uint64_t id,
                                      uint64_t addr, size_t length,
                                      size_t offset, int prot);
int process_memory_protection_allowed(uint32_t pid, uint64_t addr,
                                      size_t length, int prot);
/* Mark an already registered mapping as owned by a native JIT arena.  The
 * backing and lazy state remain those of the shared mapping; this only makes
 * the inventory carry the native ownership tag used by fork/teardown/maps. */
int process_memory_mark_jit_region(uint32_t pid, uint64_t addr);
void process_memory_shared_release(uint64_t id);
int process_memory_shared_retain(uint64_t id);
size_t process_memory_shared_size(uint64_t id);
/* Native JIT decommit drops both local aliases before returning sparse pages.
 * References retained by fork/snapshots keep the backing alive. */
size_t process_memory_shared_committed_bytes(uint64_t id);
int process_memory_shared_page_present(uint64_t id, uint32_t index);
int process_memory_jit_discard(uint32_t pid, uint64_t cr3, uint64_t id,
                               size_t offset, size_t length);
int process_memory_materialize_namespace(uint32_t pid, uint64_t namespace_id, uint64_t cr3);
int process_memory_register_region(uint32_t pid, uint64_t addr, size_t length, int prot, int flags, uint32_t kind_flags, const void *data);
int process_memory_materialize_region(uint32_t pid, uint64_t cr3, uint64_t addr);
/* Resolve a non-present user fault in a lazy region.  Returns 0 when the
 * fault was handled and -1 when the address/access is not a valid lazy fault. */
int process_memory_materialize_page(uint32_t pid, uint64_t cr3, uint64_t address,
                                    uint64_t page_fault_error);
/* True when a file-backed fault addresses a whole page beyond the retained
 * file length (Linux personality translates this to SIGBUS/BUS_ADRERR). */
int process_memory_file_fault_is_bus(process_t *process, uint64_t address);
int process_memory_remove_region(uint32_t pid, uint64_t cr3, uint64_t addr, size_t length);
int process_memory_validate_surface_mapping(uint32_t pid, uint64_t addr,
                                             size_t length, uint64_t shared_id,
                                             int required_prot);
int process_memory_protect_region(uint32_t pid, uint64_t addr, size_t length, int prot);
int process_memory_snapshot_region(uint32_t pid, uint64_t cr3, uint64_t addr);
int process_memory_snapshot_all(uint32_t pid, uint64_t cr3);
int process_memory_materialize_all(uint32_t pid, uint64_t cr3);
int process_memory_clone_namespace(uint32_t src_pid, uint64_t src_cr3, uint32_t dst_pid, uint64_t dst_cr3);
int process_memory_clear_namespace(uint32_t pid, uint64_t cr3);
uint64_t process_memory_next_alloc_hint(uint32_t pid, uint64_t floor);
int process_memory_region_contains(process_t *process, uint64_t address, size_t length);
int process_memory_region_is_reservation(process_t *process, uint64_t address, size_t length);
int process_memory_region_is_lazy(process_t *process, uint64_t address, size_t length);
int process_memory_range_is_free(process_t *process, uint64_t address, size_t length);
/* Size of the process' registered user stack, or zero when absent. */
size_t process_memory_stack_size(uint32_t pid);

/* Render the authoritative active namespace as a Linux-compatibility maps
 * view.  The caller owns the output buffer; no process-memory metadata is
 * exposed to userspace directly. */
int process_memory_format_maps(uint32_t pid, char *buffer, size_t capacity,
                              size_t *length_out);

#endif
