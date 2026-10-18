#ifndef KYIL_PE_IMAGE_H
#define KYIL_PE_IMAGE_H
#include <stddef.h>
#include <stdint.h>
#define PE_MAX_SECTIONS 96
#define PE_PAGE 4096u
#define PE_R 1u
#define PE_W 2u
#define PE_X 4u
typedef struct {
    uint32_t rva, size, raw, raw_size, flags;
} pe_section;
typedef struct {
    uint64_t preferred;
    uint64_t stack_reserve,stack_commit;
    uint32_t size, headers, entry, dirs[16][2];
    uint16_t count, subsystem;
    unsigned dll;
    pe_section sections[PE_MAX_SECTIONS];
} pe_image;
/* Parse immutable file bytes. Error strings are stable diagnostics, not errno. */
const char *pe_parse(const uint8_t *, size_t, pe_image *);
const char *pe_relocate(uint8_t *, const pe_image *, uint64_t);
unsigned pe_page_access(const pe_image *, uint32_t);
int pe_span(uint64_t, uint64_t, uint64_t);
uint16_t pe_u16(const uint8_t *);
uint32_t pe_u32(const uint8_t *);
uint64_t pe_u64(const uint8_t *);
void pe_put64(uint8_t *, uint64_t);
#endif
