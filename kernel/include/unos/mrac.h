#ifndef UNOS_MRAC_H
#define UNOS_MRAC_H

#include <stddef.h>
#include <stdint.h>

typedef struct MRAC_entry MRAC_entry_t;
typedef struct MRAC_version_info {
    uint32_t version;
    size_t size;
    uint8_t is_current;
} MRAC_version_info_t;

typedef struct MRAC_binding_info {
    uint64_t key_hash;
    uint32_t current_version;
    size_t current_size;
} MRAC_binding_info_t;

void MRAC_init(void);
int MRAC_primary_allocator_active(void);
void *MRAC_alloc(uint32_t pid, size_t size);
void MRAC_free(void *ptr);
void *MRAC_realloc(uint32_t pid, void *ptr, size_t size);
void *MRAC_store(uint32_t pid, void *data, size_t size);
void *MRAC_store_with_hash(uint32_t pid, uint64_t hash, void *data, size_t size);
void *MRAC_store_with_hash_zero_extended(uint32_t pid, uint64_t hash, const void *data, size_t data_size, size_t total_size);
int MRAC_can_direct_map(const void *data, size_t size);
int MRAC_direct_physical_page(const void *data, size_t offset, uint64_t *phys_out);
void *MRAC_lookup(uint32_t pid, uint64_t hash);
void *MRAC_lookup_version(uint32_t pid, uint64_t hash, uint32_t version);
void *MRAC_modify(uint32_t pid, uint64_t hash, void *new_data, size_t size);
void *MRAC_modify_with_hash(uint32_t pid, uint64_t hash, void *new_data, size_t size);
void MRAC_release(uint32_t pid, uint64_t hash);
void MRAC_release_version(uint32_t pid, uint64_t hash, uint32_t version);
void MRAC_remove(uint32_t pid, uint64_t hash);
int MRAC_remove_version(uint32_t pid, uint64_t hash, uint32_t version);
uint32_t MRAC_current_version(uint32_t pid, uint64_t hash);
size_t MRAC_version_count(uint32_t pid, uint64_t hash);
int MRAC_version_info_at(uint32_t pid, uint64_t hash, size_t index, MRAC_version_info_t *out);
int MRAC_binding_info_at(uint32_t pid, size_t index, MRAC_binding_info_t *out);
void MRAC_reap_pid(uint32_t pid);

#endif
