#ifndef UNOS_ORA_H
#define UNOS_ORA_H
#include <stddef.h>
#include <stdint.h>

/* Kernel references, not userspace handles or capabilities. No PID/key namespace. */
typedef struct ora_object *ora_ref_t;
typedef enum ora_kind { ORA_CONTENT, ORA_STORAGE } ora_kind_t;
typedef enum ora_policy { ORA_UNIQUE, ORA_CANONICAL } ora_policy_t;
typedef struct ora_view { ora_ref_t object; size_t offset, length; } ora_view_t;
typedef struct ora_stats {
    uint64_t objects, contents, storages, bytes, fingerprints, deduplicated;
} ora_stats_t;

/* The published bytes are immutable. A fingerprint belongs to the copied
 * bytes, never to an independently reread caller buffer. */
ora_ref_t ora_create(const void *bytes, size_t length, ora_policy_t policy);
ora_ref_t ora_create_zero_extended(const void *bytes, size_t data_length,
                                  size_t length, ora_policy_t policy);
/* Storage providers preserve their own mutable identity, paging and access
 * policy. ORA owns the reference lifetime and calls destroy exactly once. */
ora_ref_t ora_storage_create(size_t length, void *state, void (*destroy)(void *));
void *ora_storage_state(ora_ref_t object);
/* Retain requires an owned reference or a registry lock pinning the object.
   A freed pointer is never a weak reference or a valid userspace handle. */
ora_ref_t ora_retain(ora_ref_t object);
uint64_t ora_reference_count(ora_ref_t object);
void ora_release(ora_ref_t object);
ora_kind_t ora_kind(ora_ref_t object);
size_t ora_size(ora_ref_t object);
const void *ora_data(ora_ref_t object);
/* out must be zero-initialized/empty. Error leaves it unchanged; success owns
   one reference. Release it before acquiring another view into it. */
int ora_view_acquire(ora_ref_t object, size_t offset, size_t length, ora_view_t *out);
void ora_view_release(ora_view_t *view);
const void *ora_view_data(const ora_view_t *view);
int ora_view_physical_page(const ora_view_t *view, size_t offset, uint64_t *out);
void ora_get_stats(ora_stats_t *out);
void ora_memory_diagnostic(void);
int ora_self_test(void);

#ifdef ORA_TESTING
/* Collision injection only in tests; production has no supplied-hash API. */
ora_ref_t ora_test_create_fingerprint(const void *, size_t, uint64_t);
#endif
#endif
