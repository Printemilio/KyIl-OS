#ifndef UNOS_VERSIONED_STORE_H
#define UNOS_VERSIONED_STORE_H
#include <unos/ora.h>
typedef struct versioned_info {
    uint32_t version;
    size_t size;
    uint8_t is_current;
} versioned_info_t;
/* Application key/version service. Process-local identity; ORA storage refs. */
int versioned_put(uint32_t process_id, uint64_t key, const void *data, size_t size,
                  uint32_t *version_out);
ora_ref_t versioned_acquire(uint32_t process_id, uint64_t key, uint32_t version,
                            versioned_info_t *info);
uint32_t versioned_current(uint32_t process_id, uint64_t key);
size_t versioned_count(uint32_t process_id, uint64_t key);
int versioned_info_at(uint32_t process_id, uint64_t key, size_t index,
                      versioned_info_t *info);
int versioned_remove(uint32_t process_id, uint64_t key, uint32_t version);
void versioned_remove_all(uint32_t process_id, uint64_t key);
void versioned_reap(uint32_t process_id);
#endif
