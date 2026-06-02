#ifndef UNOS_PROCESS_MEMORY_H
#define UNOS_PROCESS_MEMORY_H

#include <stddef.h>
#include <stdint.h>

typedef struct process process_t;

#define PROCESS_MEMORY_REGION_MAGIC 0x4d5241435245474eull
#define PROCESS_MEMORY_FLAG_HEAP    0x1u
#define PROCESS_MEMORY_FLAG_STACK   0x2u
#define PROCESS_MEMORY_FLAG_MRAC_DIRECT 0x4u

typedef struct process_memory_region {
    uint64_t magic;
    uint64_t addr;
    size_t length;
    int prot;
    int flags;
    uint32_t kind_flags;
    uint32_t reserved0;
    uint64_t backing_key;
    uint64_t direct_source;
} process_memory_region_t;

uint64_t process_memory_region_key(uint32_t pid, uint64_t addr);
uint64_t process_memory_backing_key(uint32_t pid, uint64_t addr);
int process_memory_namespace_begin(uint32_t pid, uint64_t alloc_hint, uint64_t *namespace_id_out);
int process_memory_namespace_commit(uint32_t pid, uint64_t namespace_id);
int process_memory_namespace_destroy(uint32_t pid, uint64_t namespace_id, uint64_t cr3);
uint64_t process_memory_current_namespace(uint32_t pid);
int process_memory_snapshot_namespace(uint32_t pid, uint64_t source_namespace_id, uint64_t *snapshot_namespace_id_out);
int process_memory_register_region_in_namespace(uint32_t pid, uint64_t namespace_id, uint64_t addr, size_t length, int prot, int flags, uint32_t kind_flags, const void *data);
int process_memory_register_region_in_namespace_partial(uint32_t pid, uint64_t namespace_id, uint64_t addr, size_t length, int prot, int flags, uint32_t kind_flags, const void *data, size_t data_size);
int process_memory_materialize_namespace(uint32_t pid, uint64_t namespace_id, uint64_t cr3);
int process_memory_register_region(uint32_t pid, uint64_t addr, size_t length, int prot, int flags, uint32_t kind_flags, const void *data);
int process_memory_materialize_region(uint32_t pid, uint64_t cr3, uint64_t addr);
int process_memory_remove_region(uint32_t pid, uint64_t cr3, uint64_t addr, size_t length);
int process_memory_protect_region(uint32_t pid, uint64_t addr, size_t length, int prot);
int process_memory_snapshot_region(uint32_t pid, uint64_t cr3, uint64_t addr);
int process_memory_snapshot_all(uint32_t pid, uint64_t cr3);
int process_memory_materialize_all(uint32_t pid, uint64_t cr3);
int process_memory_clone_namespace(uint32_t src_pid, uint64_t src_cr3, uint32_t dst_pid, uint64_t dst_cr3);
int process_memory_clear_namespace(uint32_t pid, uint64_t cr3);
uint64_t process_memory_next_alloc_hint(uint32_t pid, uint64_t floor);
int process_memory_region_contains(process_t *process, uint64_t address, size_t length);

#endif
