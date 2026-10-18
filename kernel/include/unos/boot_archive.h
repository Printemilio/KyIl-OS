#ifndef KYIL_BOOT_ARCHIVE_H
#define KYIL_BOOT_ARCHIVE_H
#include <stddef.h>
#include <stdint.h>
#define BOOT_ARCHIVE_PATH 256u
typedef struct __attribute__((packed)) boot_archive_header {
    uint8_t magic[8]; uint32_t version,count; uint64_t size;
    uint8_t sha256[32]; uint64_t reserved;
} boot_archive_header_t;
typedef struct __attribute__((packed)) boot_archive_file {
    char path[BOOT_ARCHIVE_PATH]; uint64_t offset,size;
} boot_archive_file_t;
_Static_assert(sizeof(boot_archive_header_t)==64,"boot archive header");
_Static_assert(sizeof(boot_archive_file_t)==272,"boot archive file");
/* Full validation precedes the first visitor; visitor failure stops import. */
int boot_archive_visit(const void *,size_t,int (*)(const boot_archive_file_t *,void *),void *);
#endif
