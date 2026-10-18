#ifndef UNOS_NAMED_CONTENT_H
#define UNOS_NAMED_CONTENT_H
#include <unos/ora.h>
/* A subsystem-owned catalog, not an ORA namespace. Names are compared exactly;
 * its hash only selects a bucket. Each current binding owns one content ref. */
typedef struct named_content_entry named_content_entry_t;
typedef struct named_content_map {
    named_content_entry_t *buckets[128];
    uint8_t lock;
} named_content_map_t;
enum named_content_mode { NAMED_UPSERT, NAMED_CREATE, NAMED_REPLACE };
int named_content_bind(named_content_map_t *, const char *, ora_ref_t, enum named_content_mode);
int named_content_put(named_content_map_t *, const char *, const void *, size_t, enum named_content_mode);
ora_ref_t named_content_acquire(named_content_map_t *, const char *);
void named_content_remove(named_content_map_t *, const char *);
int named_content_move(named_content_map_t *, const char *, const char *);
void named_content_destroy(named_content_map_t *);
#endif
