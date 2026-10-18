#include <unos/elf.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/compat.h>
#include <unos/debug_log.h>
#include <unos/fs.h>
#include <unos/heap.h>
#include <unos/inst.h>
#include <unos/vex_legacy_compat.h>
#include <unos/pe.h>
#include <unos/process_memory.h>
#include <unos/random.h>
#include <unos/scheduler.h>
#include <unos/tty.h>
#include <unos/vex_syscall.h>
#include <unos/vmm.h>

#define ELF_MAGIC0 0x7fu
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'
#define ELF_CLASS_64 2u
#define ELF_DATA_LITTLE 1u
#define ELF_TYPE_EXEC 2u
#define ELF_TYPE_DYN  3u
#define ELF_MACHINE_X86_64 0x3eu
#define ELF_PT_LOAD 1u
#define ELF_PT_DYNAMIC 2u
#define ELF_PT_INTERP 3u
#define ELF_PT_TLS 7u
#define ELF_PT_PHDR 6u
#define ELF_PF_X 0x1u
#define ELF_PF_R 0x4u
#define ELF_PF_W 0x2u
#define ELF_PAGE_SIZE 4096ull
#define ELF_LOAD_PRIORITY 1u
#define ELF_USER_ALLOC_BASE 0x0000000100000000ull
#define ELF_INTERP_ALLOC_BASE 0x0000000200000000ull
#define ELF_LIBRARY_ALLOC_BASE 0x0000000300000000ull
#define ELF_TLS_ALLOC_BASE 0x0000000400000000ull
#define ELF_USER_STACK_TOP 0x00007ffffff000ull
#define ELF_USER_STACK_PAGES 128ull                 /* populated at load */
#define ELF_USER_STACK_RESERVE (8ull * 1024ull * 1024ull) /* total, grows lazily */
#define ELF_PATH_MAX 160u
#define ELF_MAX_EXEC_ARGS 128u

#define LINUX_AT_NULL   0ull
#define LINUX_AT_PHDR   3ull
#define LINUX_AT_PHENT  4ull
#define LINUX_AT_PHNUM  5ull
#define LINUX_AT_PAGESZ 6ull
#define LINUX_AT_BASE   7ull
#define LINUX_AT_ENTRY  9ull
#define LINUX_AT_UID    11ull
#define LINUX_AT_EUID   12ull
#define LINUX_AT_GID    13ull
#define LINUX_AT_EGID   14ull
#define LINUX_AT_SECURE 23ull
#define LINUX_AT_RANDOM 25ull
#define LINUX_AT_EXECFN  31ull

#define ELF_DT_NULL 0ull
#define ELF_DT_NEEDED 1ull
#define ELF_DT_PLTRELSZ 2ull
#define ELF_DT_PLTREL 20ull
#define ELF_DT_JMPREL 23ull
#define ELF_DT_HASH 4ull
#define ELF_DT_STRTAB 5ull
#define ELF_DT_SYMTAB 6ull
#define ELF_DT_RELA 7ull
#define ELF_DT_RELASZ 8ull
#define ELF_DT_RELAENT 9ull
#define ELF_DT_STRSZ 10ull
#define ELF_DT_SYMENT 11ull
#define ELF_DT_PLTGOT 3ull
#define ELF_DT_GNU_HASH 0x6ffffef5ull
#define ELF_DT_SONAME 14ull
#define ELF_DT_FLAGS 30ull
#define ELF_DT_FLAGS_1 0x6ffffffbull
#define ELF_DT_RELR 36ull
#define ELF_DT_RELRSZ 35ull
#define ELF_DT_RELRENT 37ull
#define ELF_R_X86_64_RELATIVE 8ull
#define ELF_R_X86_64_64 1ull
#define ELF_R_X86_64_GLOB_DAT 6ull
#define ELF_R_X86_64_JUMP_SLOT 7ull
#define ELF_R_X86_64_DTPMOD64 16ull
#define ELF_R_X86_64_DTPOFF64 17ull
#define ELF_R_X86_64_TPOFF64 18ull
#define ELF_SHN_UNDEF 0u
#define ELF_STB_WEAK 2u
#define ELF_MAX_OBJECTS 96u
#define ELF_MAX_NEEDED 16u

typedef struct {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} elf64_header_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} elf64_program_header_t;

typedef struct {
    int64_t tag;
    uint64_t value;
} elf64_dynamic_entry_t;

typedef struct {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} elf64_rela_t;

typedef struct {
    uint32_t name;
    uint8_t info;
    uint8_t other;
    uint16_t shndx;
    uint64_t value;
    uint64_t size;
} elf64_symbol_t;

/* Metadata for one independently mapped ELF object.  The backing buffer is
 * always a private kernel copy of fs_read_view() data; relocations therefore never
 * mutate a shared filesystem/MRAC buffer. */
typedef struct {
    uint8_t *file;
    size_t size;
    const elf64_header_t *header;
    const elf64_program_header_t *phdrs;
    uint64_t load_bias;
    uint8_t owns_file;
    char path[ELF_PATH_MAX];
    uint64_t dyn_symtab;
    uint64_t dyn_strtab;
    uint64_t dyn_strsz;
    uint64_t dyn_syment;
    uint64_t dyn_hash;
    uint64_t dyn_gnu_hash;
    uint64_t dyn_rela;
    uint64_t dyn_relasz;
    uint64_t dyn_relaent;
    uint64_t dyn_jmprel;
    uint64_t dyn_pltrelsz;
    uint64_t dyn_pltrel;
    uint64_t dyn_relr;
    uint64_t dyn_relrsz;
    uint64_t dyn_relrent;
    uint64_t tls_vaddr;
    uint64_t tls_filesz;
    uint64_t tls_memsz;
    uint64_t tls_align;
    int64_t tls_offset;
    uint32_t module_id;
    uint8_t is_interpreter;
} elf_object_t;

typedef struct {
    uint64_t addr;
    size_t length;
    size_t data_size;
    int prot;
    int flags;
    uint32_t kind_flags;
    uint8_t *data;
    uint8_t owns_data;
} elf_region_spec_t;

typedef struct {
    uint8_t abi;
    uint64_t entry;
    uint64_t program_entry;
    uint64_t interp_base;
    uint64_t phdr_addr;
    uint64_t phent_size;
    uint64_t phnum;
    uint64_t user_stack_top;
    uint64_t next_alloc_hint;
    size_t region_count;
    elf_region_spec_t *regions;
    uint8_t *image_data;
    size_t image_size;
    uint8_t *interp_data;
    size_t interp_size;
    elf_object_t *objects;
    size_t object_count;
    uint8_t defer_dynamic_linker;
    uint64_t tls_addr;
    uint64_t tls_size;
    uint64_t tls_fs_base;
    uint8_t *tls_data;
} elf_image_spec_t;

typedef struct {
    uint64_t key;
    uint64_t value;
} elf_auxv_entry_t;

static int starts_with(const char *str, const char *prefix) {
    while (*prefix != '\0') {
        if (*str != *prefix) {
            return 0;
        }
        ++str;
        ++prefix;
    }

    return 1;
}

static void copy_string_limited(char *dst, size_t capacity, const char *src) {
    size_t i = 0u;

    if (dst == NULL || capacity == 0u) {
        return;
    }

    while (src != NULL && src[i] != '\0' && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }

    dst[i] = '\0';
}

static const char *path_basename(const char *path) {
    const char *name = path;

    if (path == NULL) {
        return "";
    }

    for (const char *cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' && cursor[1] != '\0') {
            name = cursor + 1;
        }
    }

    return name;
}

static int resolve_launch_path(const char *requested_path, char resolved_path[ELF_PATH_MAX]) {
    const char *basename;

    if (requested_path == NULL || resolved_path == NULL) {
        return -1;
    }

    if (fs_exists(requested_path)) {
        copy_string_limited(resolved_path, ELF_PATH_MAX, requested_path);
        return 0;
    }

    if (!starts_with(requested_path, "/home/root/bin/")) {
        return -1;
    }

    basename = path_basename(requested_path);
    if (basename[0] == '\0') {
        return -1;
    }

    resolved_path[0] = '/';
    copy_string_limited(resolved_path + 1u, ELF_PATH_MAX - 1u, basename);
    return fs_exists(resolved_path) ? 0 : -1;
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1ull);
}

static int valid_elf64(const elf64_header_t *header, size_t file_size) {
    if (header == NULL || file_size < sizeof(*header)) {
        return 0;
    }

    if (header->ident[0] != ELF_MAGIC0 ||
        header->ident[1] != ELF_MAGIC1 ||
        header->ident[2] != ELF_MAGIC2 ||
        header->ident[3] != ELF_MAGIC3) {
        return 0;
    }

    if (header->ident[4] != ELF_CLASS_64 ||
        header->ident[5] != ELF_DATA_LITTLE ||
        (header->type != ELF_TYPE_EXEC && header->type != ELF_TYPE_DYN) ||
        header->machine != ELF_MACHINE_X86_64 ||
        header->phentsize != sizeof(elf64_program_header_t) ||
        header->phnum == 0) {
        return 0;
    }

    return header->phoff <= file_size &&
        ((uint64_t)header->phnum * header->phentsize) <= file_size - header->phoff;
}

static void free_elf_image_spec(elf_image_spec_t *spec) {
    if (spec == NULL) {
        return;
    }

    if (spec->regions != NULL) {
        for (size_t index = 0u; index < spec->region_count; ++index) {
            if (spec->regions[index].owns_data != 0u && spec->regions[index].data != NULL) {
                kfree(spec->regions[index].data);
            }
        }
        kfree(spec->regions);
    }

    if (spec->image_data != NULL) {
        kfree(spec->image_data);
    }
    if (spec->interp_data != NULL) {
        kfree(spec->interp_data);
    }

    for (size_t index = 0u; index < spec->object_count; ++index) {
        if (spec->objects[index].owns_file != 0u && spec->objects[index].file != NULL) {
            kfree(spec->objects[index].file);
        }
    }

    spec->regions = NULL;
    spec->region_count = 0u;
    spec->next_alloc_hint = 0u;
    spec->entry = 0u;
    spec->program_entry = 0u;
    spec->interp_base = 0u;
    spec->phdr_addr = 0u;
    spec->phent_size = 0u;
    spec->phnum = 0u;
    spec->user_stack_top = 0u;
    spec->abi = VEX_ABI_NATIVE;
    spec->image_data = NULL;
    spec->image_size = 0u;
    spec->interp_data = NULL;
    spec->interp_size = 0u;
    if (spec->objects != NULL) {
        kfree(spec->objects);
    }
    spec->objects = NULL;
    spec->object_count = 0u;
    spec->tls_addr = 0u;
    spec->tls_size = 0u;
    spec->tls_fs_base = 0u;
    if (spec->tls_data != NULL) {
        kfree(spec->tls_data);
    }
    spec->tls_data = NULL;
}

static int add_region_spec(elf_image_spec_t *spec, size_t *index, uint64_t addr, size_t length, size_t data_size, int prot, int flags, uint32_t kind_flags, uint8_t *data, uint8_t owns_data) {
    if (spec == NULL || index == NULL || *index >= spec->region_count) {
        return -1;
    }
    if (data_size > length) {
        return -1;
    }

    spec->regions[*index].addr = addr;
    spec->regions[*index].length = length;
    spec->regions[*index].data_size = data_size;
    spec->regions[*index].prot = prot;
    spec->regions[*index].flags = flags;
    spec->regions[*index].kind_flags = kind_flags;
    spec->regions[*index].data = data;
    spec->regions[*index].owns_data = owns_data;
    ++(*index);
    return 0;
}

static size_t elf_count_load_regions(const elf64_header_t *header, const elf64_program_header_t *phdrs) {
    size_t count = 0u;

    for (uint16_t i = 0; i < header->phnum; ++i) {
        if (phdrs[i].type == ELF_PT_LOAD && phdrs[i].memsz != 0u) {
            ++count;
        }
    }

    return count;
}

/* AT_PHDR is the run-time address of the program headers: PT_PHDR when
   present, otherwise their position inside the PT_LOAD segment that maps
   e_phoff. (load_bias + e_phoff is only right when the first segment maps
   file offset 0 at the bias, i.e. for position-independent images.) */
static uint64_t elf_phdr_address(const elf64_header_t *header,
                                 const elf64_program_header_t *phdrs,
                                 uint64_t load_bias) {
    for (uint16_t i = 0u; i < header->phnum; ++i)
        if (phdrs[i].type == ELF_PT_PHDR) return load_bias + phdrs[i].vaddr;
    for (uint16_t i = 0u; i < header->phnum; ++i)
        if (phdrs[i].type == ELF_PT_LOAD && header->phoff >= phdrs[i].offset &&
            header->phoff - phdrs[i].offset < phdrs[i].filesz)
            return load_bias + phdrs[i].vaddr + (header->phoff - phdrs[i].offset);
    return load_bias + header->phoff;
}

static uint64_t elf_compute_load_bias(const elf64_header_t *header, const elf64_program_header_t *phdrs, uint64_t preferred_base) {
    uint64_t lowest_vaddr = UINT64_MAX;

    if (header->type != ELF_TYPE_DYN) {
        return 0u;
    }

    for (uint16_t i = 0; i < header->phnum; ++i) {
        if (phdrs[i].type != ELF_PT_LOAD || phdrs[i].memsz == 0u) {
            continue;
        }
        if (phdrs[i].vaddr < lowest_vaddr) {
            lowest_vaddr = phdrs[i].vaddr;
        }
    }

    if (lowest_vaddr == UINT64_MAX) {
        return preferred_base;
    }

    return preferred_base - align_down(lowest_vaddr, ELF_PAGE_SIZE);
}

static int elf_vaddr_to_file_offset(const elf64_header_t *header, const elf64_program_header_t *phdrs, uint64_t vaddr, uint64_t size_needed, uint64_t *offset_out) {
    if (header == NULL || phdrs == NULL || offset_out == NULL) {
        return -1;
    }

    for (uint16_t i = 0; i < header->phnum; ++i) {
        const uint64_t start = phdrs[i].vaddr;
        uint64_t end;
        uint64_t file_offset;

        if (phdrs[i].type != ELF_PT_LOAD || phdrs[i].filesz == 0u || vaddr < start) {
            continue;
        }
        if (start > UINT64_MAX - phdrs[i].filesz) {
            continue;
        }
        end = start + phdrs[i].filesz;
        if (vaddr <= end && size_needed <= end - vaddr) {
            if (phdrs[i].offset > UINT64_MAX - (vaddr - start)) {
                return -1;
            }
            file_offset = phdrs[i].offset + (vaddr - start);
            *offset_out = file_offset;
            return 0;
        }
    }

    return -1;
}

static void elf_log_unsupported_relocation(uint64_t type) {
    static const char digits[] = "0123456789abcdef";
    char text[19];
    text[0] = '0';
    text[1] = 'x';
    for (unsigned int i = 0u; i < 16u; ++i) {
        text[2u + i] = digits[(type >> (60u - i * 4u)) & 0xfu];
    }
    text[18] = '\0';
    debug_log_puts("elf: unsupported dynamic relocation ");
    debug_log_puts(text);
    debug_log_puts("\n");
}

static int elf_object_parse_dynamic(elf_object_t *object) {
    const elf64_program_header_t *dynamic_phdr = NULL;
    const elf64_dynamic_entry_t *dynamic;
    size_t dynamic_count;

    if (object == NULL || object->file == NULL || object->header == NULL || object->phdrs == NULL) {
        return -1;
    }

    for (uint16_t i = 0u; i < object->header->phnum; ++i) {
        if (object->phdrs[i].type == ELF_PT_DYNAMIC) {
            dynamic_phdr = &object->phdrs[i];
            break;
        }
    }
    for (uint16_t i = 0u; i < object->header->phnum; ++i) {
        if (object->phdrs[i].type == ELF_PT_TLS) {
            object->tls_vaddr = object->phdrs[i].vaddr;
            object->tls_filesz = object->phdrs[i].filesz;
            object->tls_memsz = object->phdrs[i].memsz;
            object->tls_align = object->phdrs[i].align == 0u ? 1u : object->phdrs[i].align;
            if (object->tls_filesz > object->tls_memsz ||
                object->phdrs[i].offset > object->size ||
                object->tls_filesz > object->size - object->phdrs[i].offset) {
                return -1;
            }
            break;
        }
    }
    if (dynamic_phdr == NULL) {
        return 0;
    }
    if (dynamic_phdr->offset > object->size ||
        dynamic_phdr->filesz > object->size - dynamic_phdr->offset ||
        (dynamic_phdr->filesz % sizeof(elf64_dynamic_entry_t)) != 0u) {
        return -1;
    }

    dynamic = (const elf64_dynamic_entry_t *)(const void *)(object->file + dynamic_phdr->offset);
    dynamic_count = (size_t)(dynamic_phdr->filesz / sizeof(*dynamic));
    for (size_t i = 0u; i < dynamic_count; ++i) {
        if (dynamic[i].tag == (int64_t)ELF_DT_NULL) {
            break;
        }
        switch ((uint64_t)dynamic[i].tag) {
        case ELF_DT_SYMTAB: object->dyn_symtab = dynamic[i].value; break;
        case ELF_DT_STRTAB: object->dyn_strtab = dynamic[i].value; break;
        case ELF_DT_STRSZ: object->dyn_strsz = dynamic[i].value; break;
        case ELF_DT_SYMENT: object->dyn_syment = dynamic[i].value; break;
        case ELF_DT_HASH: object->dyn_hash = dynamic[i].value; break;
        case ELF_DT_GNU_HASH: object->dyn_gnu_hash = dynamic[i].value; break;
        case ELF_DT_RELA: object->dyn_rela = dynamic[i].value; break;
        case ELF_DT_RELASZ: object->dyn_relasz = dynamic[i].value; break;
        case ELF_DT_RELAENT: object->dyn_relaent = dynamic[i].value; break;
        case ELF_DT_JMPREL: object->dyn_jmprel = dynamic[i].value; break;
        case ELF_DT_PLTRELSZ: object->dyn_pltrelsz = dynamic[i].value; break;
        case ELF_DT_PLTREL: object->dyn_pltrel = dynamic[i].value; break;
        case ELF_DT_RELR: object->dyn_relr = dynamic[i].value; break;
        case ELF_DT_RELRSZ: object->dyn_relrsz = dynamic[i].value; break;
        case ELF_DT_RELRENT: object->dyn_relrent = dynamic[i].value; break;
        default: break;
        }
    }
    if (object->dyn_relaent == 0u) {
        object->dyn_relaent = sizeof(elf64_rela_t);
    }
    if (object->dyn_syment == 0u) {
        object->dyn_syment = sizeof(elf64_symbol_t);
    }
    return 0;
}

static int elf_object_vaddr_to_offset(const elf_object_t *object, uint64_t vaddr,
                                      uint64_t size_needed, uint64_t *offset_out) {
    return object == NULL ? -1 : elf_vaddr_to_file_offset(object->header, object->phdrs,
                                                           vaddr, size_needed, offset_out);
}

static size_t elf_object_symbol_count(const elf_object_t *object) {
    uint64_t offset;
    if (object == NULL) {
        return 0u;
    }
    if (object->dyn_hash != 0u &&
        elf_object_vaddr_to_offset(object, object->dyn_hash, 8u, &offset) == 0 &&
        object->size >= 8u && offset <= object->size - 8u) {
        const uint32_t *hash = (const uint32_t *)(const void *)(object->file + offset);
        return hash[1] > 0x100000u ? 0u : (size_t)hash[1];
    }
    if (object->dyn_gnu_hash != 0u &&
        elf_object_vaddr_to_offset(object, object->dyn_gnu_hash, 16u, &offset) == 0 &&
        object->size >= 16u && offset <= object->size - 16u) {
        const uint32_t *head = (const uint32_t *)(const void *)(object->file + offset);
        const uint32_t buckets = head[0];
        const uint32_t symoffset = head[1];
        const uint32_t bloom_size = head[2];
        uint64_t bucket_offset;
        uint64_t chain_offset;
        uint32_t max_symbol = symoffset;
        if (buckets == 0u || bloom_size > 0x100000u ||
            symoffset > 0x100000u ||
            (uint64_t)16u + (uint64_t)bloom_size * 8u + (uint64_t)buckets * 4u > object->size - offset) {
            return 0u;
        }
        bucket_offset = offset + 16u + (uint64_t)bloom_size * 8u;
        chain_offset = bucket_offset + (uint64_t)buckets * 4u;
        const uint32_t *bucket = (const uint32_t *)(const void *)(object->file + bucket_offset);
        for (uint32_t i = 0u; i < buckets; ++i) {
            if (bucket[i] >= max_symbol) {
                uint32_t symbol = bucket[i];
                while (symbol >= symoffset) {
                    uint64_t chain_entry = chain_offset + (uint64_t)(symbol - symoffset) * 4u;
                    if (chain_entry > object->size - 4u) {
                        return 0u;
                    }
                    max_symbol = symbol + 1u;
                    if ((((const uint32_t *)(const void *)(object->file + chain_entry))[0] & 1u) != 0u) {
                        break;
                    }
                    ++symbol;
                }
            }
        }
        return max_symbol;
    }
    return 0u;
}

static int elf_object_symbol(const elf_object_t *object, uint32_t index, elf64_symbol_t *out) {
    uint64_t symbol_vaddr;
    uint64_t offset;
    size_t count;
    if (object == NULL || out == NULL || object->dyn_symtab == 0u || object->dyn_syment != sizeof(elf64_symbol_t)) {
        return -1;
    }
    count = elf_object_symbol_count(object);
    if ((size_t)index >= count || index > (UINT64_MAX - object->dyn_symtab) / object->dyn_syment) {
        return -1;
    }
    symbol_vaddr = object->dyn_symtab + (uint64_t)index * object->dyn_syment;
    if (elf_object_vaddr_to_offset(object, symbol_vaddr, sizeof(*out), &offset) != 0 ||
        offset > object->size || sizeof(*out) > object->size - offset) {
        return -1;
    }
    memcpy(out, object->file + offset, sizeof(*out));
    return 0;
}

static const char *elf_object_string(const elf_object_t *object, uint32_t offset) {
    uint64_t file_offset;
    uint64_t string_end;
    if (object == NULL || object->dyn_strtab == 0u || (uint64_t)offset >= object->dyn_strsz ||
        object->dyn_strtab > UINT64_MAX - offset ||
        elf_object_vaddr_to_offset(object, object->dyn_strtab + offset, 1u, &file_offset) != 0 ||
        file_offset >= object->size) {
        return NULL;
    }
    if (object->dyn_strsz > UINT64_MAX - file_offset) {
        return NULL;
    }
    string_end = file_offset + object->dyn_strsz;
    if (string_end > object->size) {
        string_end = object->size;
    }
    for (uint64_t i = file_offset; i < string_end; ++i) {
        if (object->file[i] == '\0') {
            return (const char *)(const void *)(object->file + file_offset);
        }
    }
    return NULL;
}

static int elf_symbol_is_defined(const elf64_symbol_t *symbol) {
    return symbol != NULL && symbol->shndx != ELF_SHN_UNDEF;
}

static int elf_resolve_symbol(const elf_image_spec_t *spec, const elf_object_t *requester,
                              uint32_t symbol_index, uint64_t *value_out,
                              elf64_symbol_t *request_symbol_out) {
    elf64_symbol_t request_symbol;
    const char *name;

    if (spec == NULL || requester == NULL || value_out == NULL ||
        elf_object_symbol(requester, symbol_index, &request_symbol) != 0) {
        return -1;
    }
    name = elf_object_string(requester, request_symbol.name);
    if (name == NULL) {
        return -1;
    }
    if (request_symbol_out != NULL) {
        *request_symbol_out = request_symbol;
    }

    for (size_t i = 0u; i < spec->object_count; ++i) {
        const elf_object_t *candidate = &spec->objects[i];
        size_t count = elf_object_symbol_count(candidate);
        for (size_t index = 0u; index < count; ++index) {
            elf64_symbol_t symbol;
            const char *candidate_name;
            if (elf_object_symbol(candidate, (uint32_t)index, &symbol) != 0 ||
                !elf_symbol_is_defined(&symbol)) {
                continue;
            }
            candidate_name = elf_object_string(candidate, symbol.name);
            if (candidate_name == NULL || strcmp(candidate_name, name) != 0) {
                continue;
            }
            *value_out = candidate->load_bias + symbol.value;
            return 0;
        }
    }
    if ((request_symbol.info >> 4u) == ELF_STB_WEAK) {
        *value_out = 0u;
        return 0;
    }
    return -1;
}

static int elf_write_relocation(const elf_object_t *object, const elf64_rela_t *rela, uint64_t value) {
    uint64_t target_offset;
    if (object == NULL || rela == NULL ||
        elf_object_vaddr_to_offset(object, rela->offset, sizeof(value), &target_offset) != 0 ||
        target_offset > object->size || sizeof(value) > object->size - target_offset) {
        return -1;
    }
    memcpy(object->file + target_offset, &value, sizeof(value));
    return 0;
}

static int elf_apply_relr(const elf_object_t *object) {
    uint64_t offset;
    uint64_t cursor = 0u;
    if (object == NULL || object->dyn_relr == 0u || object->dyn_relrsz == 0u) {
        return 0;
    }
    if (object->dyn_relrent != 0u && object->dyn_relrent != sizeof(uint64_t)) {
        return -1;
    }
    if ((object->dyn_relrsz % sizeof(uint64_t)) != 0u ||
        elf_object_vaddr_to_offset(object, object->dyn_relr, object->dyn_relrsz, &offset) != 0 ||
        offset > object->size || object->dyn_relrsz > object->size - offset) {
        return -1;
    }
    for (size_t i = 0u; i < object->dyn_relrsz / sizeof(uint64_t); ++i) {
        uint64_t entry;
        memcpy(&entry, object->file + offset + i * sizeof(uint64_t), sizeof(entry));
        if ((entry & 1u) == 0u) {
            cursor = entry;
            uint64_t value = 0u;
            uint64_t target_offset;
            if (elf_object_vaddr_to_offset(object, cursor, sizeof(value), &target_offset) != 0 ||
                target_offset > object->size || sizeof(value) > object->size - target_offset) {
                return -1;
            }
            memcpy(&value, object->file + target_offset, sizeof(value));
            value += object->load_bias;
            memcpy(object->file + target_offset, &value, sizeof(value));
            cursor += sizeof(uint64_t);
        } else {
            for (uint32_t bit = 1u; bit < 64u; ++bit) {
                if ((entry & (1ull << bit)) != 0u) {
                    uint64_t target = cursor + (uint64_t)(bit - 1u) * sizeof(uint64_t);
                    uint64_t target_offset;
                    uint64_t value;
                    if (elf_object_vaddr_to_offset(object, target, sizeof(value), &target_offset) != 0 ||
                        target_offset > object->size || sizeof(value) > object->size - target_offset) {
                        return -1;
                    }
                    memcpy(&value, object->file + target_offset, sizeof(value));
                    value += object->load_bias;
                    memcpy(object->file + target_offset, &value, sizeof(value));
                }
            }
            cursor += 63u * sizeof(uint64_t);
        }
    }
    return 0;
}

static int elf_apply_object_relocations(const elf_image_spec_t *spec, const elf_object_t *object) {
    const uint64_t tables[2] = { object == NULL ? 0u : object->dyn_rela, object == NULL ? 0u : object->dyn_jmprel };
    const uint64_t sizes[2] = { object == NULL ? 0u : object->dyn_relasz, object == NULL ? 0u : object->dyn_pltrelsz };
    if (object == NULL || (object->dyn_relaent != 0u && object->dyn_relaent != sizeof(elf64_rela_t)) ||
        (object->dyn_relrent != 0u && object->dyn_relrent != sizeof(uint64_t))) {
        return -1;
    }
    for (size_t table = 0u; table < 2u; ++table) {
        uint64_t offset;
        if (tables[table] == 0u || sizes[table] == 0u) {
            continue;
        }
        if ((sizes[table] % sizeof(elf64_rela_t)) != 0u ||
            (table == 1u && object->dyn_pltrel != 0u && object->dyn_pltrel != ELF_DT_RELA) ||
            elf_object_vaddr_to_offset(object, tables[table], sizes[table], &offset) != 0 ||
            offset > object->size || sizes[table] > object->size - offset) {
            return -1;
        }
        for (size_t i = 0u; i < sizes[table] / sizeof(elf64_rela_t); ++i) {
            elf64_rela_t rela;
            uint64_t value = 0u;
            uint64_t symbol_value = 0u;
            elf64_symbol_t request_symbol;
            memcpy(&rela, object->file + offset + i * sizeof(rela), sizeof(rela));
            const uint64_t type = rela.info & 0xffffffffull;
            const uint32_t symbol_index = (uint32_t)(rela.info >> 32u);

            if (type == ELF_R_X86_64_RELATIVE) {
                if (symbol_index != 0u) {
                    return -1;
                }
                value = object->load_bias + (uint64_t)rela.addend;
            } else if (type == ELF_R_X86_64_64 || type == ELF_R_X86_64_GLOB_DAT ||
                       type == ELF_R_X86_64_JUMP_SLOT) {
                if (elf_resolve_symbol(spec, object, symbol_index, &symbol_value, &request_symbol) != 0) {
                    debug_log_puts("elf: unresolved dynamic symbol\n");
                    return -1;
                }
                value = symbol_value + (uint64_t)rela.addend;
            } else if (type == ELF_R_X86_64_DTPMOD64) {
                if (symbol_index != 0u && elf_object_symbol(object, symbol_index, &request_symbol) != 0) {
                    return -1;
                }
                value = object->module_id;
            } else if (type == ELF_R_X86_64_DTPOFF64) {
                if (symbol_index != 0u && elf_object_symbol(object, symbol_index, &request_symbol) != 0) {
                    return -1;
                }
                value = (symbol_index == 0u ? 0u : request_symbol.value) + (uint64_t)rela.addend;
            } else if (type == ELF_R_X86_64_TPOFF64) {
                if (symbol_index != 0u && elf_object_symbol(object, symbol_index, &request_symbol) != 0) {
                    return -1;
                }
                value = (uint64_t)object->tls_offset + (symbol_index == 0u ? 0u : request_symbol.value) + (uint64_t)rela.addend;
            } else {
                elf_log_unsupported_relocation(type);
                return -1;
            }
            if (elf_write_relocation(object, &rela, value) != 0) {
                return -1;
            }
        }
    }
    return elf_apply_relr(object);
}

static int elf_append_load_regions(elf_image_spec_t *spec, size_t *region_index, const uint8_t *file, size_t size, const elf64_header_t *header, const elf64_program_header_t *phdrs, uint64_t load_bias, uint64_t *highest_non_stack_end) {
    for (uint16_t i = 0; i < header->phnum; ++i) {
        int prot;
        uint64_t mapped_addr;
        uint64_t segment_end;

        if (phdrs[i].type != ELF_PT_LOAD || phdrs[i].memsz == 0u) {
            continue;
        }
        if (phdrs[i].memsz < phdrs[i].filesz || phdrs[i].offset > size || phdrs[i].filesz > size - phdrs[i].offset) {
            return -1;
        }

        prot = 0;
        if ((phdrs[i].flags & ELF_PF_R) != 0u) {
            prot |= PROT_READ;
        }
        if ((phdrs[i].flags & ELF_PF_W) != 0u) {
            prot |= PROT_WRITE;
        }
        if ((phdrs[i].flags & ELF_PF_X) != 0u) {
            prot |= PROT_EXEC;
        }
        if ((prot & PROT_WRITE) != 0 && (prot & PROT_EXEC) != 0) {
            debug_log_puts("elf: writable executable segment rejected\n");
            return -1;
        }

        if (load_bias > UINT64_MAX - phdrs[i].vaddr) {
            return -1;
        }
        mapped_addr = load_bias + phdrs[i].vaddr;
        if (mapped_addr == 0u || mapped_addr >= VMM_USER_ADDRESS_LIMIT ||
            phdrs[i].memsz > VMM_USER_ADDRESS_LIMIT - mapped_addr) {
            debug_log_puts("elf: load segment outside user address space\n");
            return -1;
        }
        if (add_region_spec(spec, region_index, mapped_addr, (size_t)phdrs[i].memsz, (size_t)phdrs[i].filesz,
                prot, MAP_PRIVATE, PROCESS_MEMORY_FLAG_NO_DIRECT,
                (uint8_t *)file + phdrs[i].offset, 0u) != 0) {
            return -1;
        }

        segment_end = align_up(mapped_addr + phdrs[i].memsz, ELF_PAGE_SIZE);
        if (segment_end > *highest_non_stack_end) {
            *highest_non_stack_end = segment_end;
        }
    }

    return 0;
}

static int elf_object_bind(elf_object_t *object, uint8_t *file, size_t size,
                           uint64_t load_bias, uint8_t owns_file, const char *path) {
    const elf64_header_t *header;
    if (object == NULL || file == NULL || !valid_elf64((const elf64_header_t *)(const void *)file, size)) {
        return -1;
    }
    header = (const elf64_header_t *)(const void *)file;
    object->file = file;
    object->size = size;
    object->header = header;
    object->phdrs = (const elf64_program_header_t *)(const void *)(file + header->phoff);
    object->load_bias = load_bias;
    object->owns_file = owns_file;
    copy_string_limited(object->path, sizeof(object->path), path == NULL ? "" : path);
    if (elf_object_parse_dynamic(object) != 0) {
        return -1;
    }
    return 0;
}

static int elf_object_has_path(const elf_image_spec_t *spec, const char *path) {
    if (spec == NULL || path == NULL) {
        return 0;
    }
    for (size_t i = 0u; i < spec->object_count; ++i) {
        if (strcmp(spec->objects[i].path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

static void elf_parent_directory(const char *path, char *out, size_t capacity) {
    size_t length = 0u;
    if (out == NULL || capacity == 0u) {
        return;
    }
    out[0] = '\0';
    if (path == NULL) {
        return;
    }
    while (path[length] != '\0' && length + 1u < capacity) {
        ++length;
    }
    while (length > 0u && path[length - 1u] != '/') {
        --length;
    }
    if (length == 0u) {
        copy_string_limited(out, capacity, "/");
        return;
    }
    if (length >= capacity) {
        length = capacity - 1u;
    }
    memcpy(out, path, length);
    out[length] = '\0';
}

static int elf_try_dependency_path(const char *needed, const char *requester_path,
                                   char resolved[ELF_PATH_MAX]) {
    char directory[ELF_PATH_MAX];
    if (needed == NULL || resolved == NULL) {
        return -1;
    }
    if (needed[0] == '/') {
        copy_string_limited(resolved, ELF_PATH_MAX, needed);
        return fs_exists(resolved) ? 0 : -1;
    }
    elf_parent_directory(requester_path, directory, sizeof(directory));
    if (directory[0] != '\0') {
        size_t length = 0u;
        copy_string_limited(resolved, ELF_PATH_MAX, directory);
        while (resolved[length] != '\0' && length + 1u < ELF_PATH_MAX) {
            ++length;
        }
        if (length + 1u < ELF_PATH_MAX && resolved[length - 1u] != '/') {
            resolved[length++] = '/';
            resolved[length] = '\0';
        }
        copy_string_limited(resolved + length, ELF_PATH_MAX - length, needed);
        if (fs_exists(resolved)) {
            return 0;
        }
    }
    const char *prefixes[] = { "/lib/", "/lib64/", "/usr/lib/", "/usr/local/lib/" };
    for (size_t i = 0u; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        copy_string_limited(resolved, ELF_PATH_MAX, prefixes[i]);
        size_t length = 0u;
        while (resolved[length] != '\0' && length + 1u < ELF_PATH_MAX) {
            ++length;
        }
        copy_string_limited(resolved + length, ELF_PATH_MAX - length, needed);
        if (fs_exists(resolved)) {
            return 0;
        }
    }
    return -1;
}

static int elf_load_dependency(elf_image_spec_t *spec, const elf_object_t *requester,
                               const char *needed) {
    ora_view_t fs_lease_0 __attribute__((cleanup(ora_view_release)))={0};
    char path[ELF_PATH_MAX];
    void *data = NULL;
    size_t size = 0u;
    uint8_t *copy;
    elf_object_t *object;
    uint64_t preferred_base;

    if (spec == NULL || requester == NULL || needed == NULL) {
        return -1;
    }
    if (spec->object_count >= ELF_MAX_OBJECTS) {
        debug_log_puts("elf: object limit while loading ");
        debug_log_puts(needed);
        debug_log_puts("\n");
        return -1;
    }
    if (elf_try_dependency_path(needed, requester->path, path) != 0) {
        debug_log_puts("elf: missing dependency ");
        debug_log_puts(needed);
        debug_log_puts("\n");
        return -1;
    }
    if (elf_object_has_path(spec, path)) {
        return 0;
    }
    if (fs_read_view(path,&data,&size, &fs_lease_0) != 0 || data == NULL || size == 0u || (copy = kmalloc(size)) == NULL) {
        return -1;
    }
    memcpy(copy, data, size);
    scheduler_safe_point(); /* private loader copy, no inventory mutation held */
    preferred_base = ELF_LIBRARY_ALLOC_BASE + (uint64_t)spec->object_count * 0x10000000ull;
    object = &spec->objects[spec->object_count];
    memset(object, 0, sizeof(*object));
    if (elf_object_bind(object, copy, size,
                        elf_compute_load_bias((const elf64_header_t *)(const void *)copy,
                                              (const elf64_program_header_t *)(const void *)(copy + ((const elf64_header_t *)(const void *)copy)->phoff),
                                              preferred_base),
                        1u, path) != 0) {
        kfree(copy);
        return -1;
    }
    object->module_id = (uint32_t)spec->object_count + 1u;
    ++spec->object_count;
    return 0;
}

static int elf_load_needed_objects(elf_image_spec_t *spec) {
    for (size_t index = 0u; index < spec->object_count; ++index) {
        elf_object_t *object = &spec->objects[index];
        const elf64_program_header_t *dynamic_phdr = NULL;
        if (object->header == NULL) {
            continue;
        }
        for (uint16_t i = 0u; i < object->header->phnum; ++i) {
            if (object->phdrs[i].type == ELF_PT_DYNAMIC) {
                dynamic_phdr = &object->phdrs[i];
                break;
            }
        }
        if (dynamic_phdr == NULL || dynamic_phdr->offset > object->size ||
            dynamic_phdr->filesz > object->size - dynamic_phdr->offset) {
            continue;
        }
        const elf64_dynamic_entry_t *dynamic = (const elf64_dynamic_entry_t *)(const void *)(object->file + dynamic_phdr->offset);
        size_t count = (size_t)(dynamic_phdr->filesz / sizeof(*dynamic));
        for (size_t i = 0u; i < count; ++i) {
            if (dynamic[i].tag == (int64_t)ELF_DT_NULL) {
                break;
            }
            if (dynamic[i].tag == (int64_t)ELF_DT_NEEDED) {
                const char *needed = elf_object_string(object, (uint32_t)dynamic[i].value);
                if (needed == NULL || elf_load_dependency(spec, object, needed) != 0) {
                    debug_log_puts("elf: required shared object unavailable\n");
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int elf_prepare_tls(elf_image_spec_t *spec) {
    uint64_t total = 0u;
    uint64_t cursor;
    if (spec == NULL) {
        return -1;
    }
    for (size_t i = 0u; i < spec->object_count; ++i) {
        const elf_object_t *object = &spec->objects[i];
        uint64_t aligned_total;
        if (object->tls_memsz == 0u) {
            continue;
        }
        if (object->tls_align == 0u || (object->tls_align & (object->tls_align - 1u)) != 0u) {
            return -1;
        }
        if (total > UINT64_MAX - (object->tls_align - 1u)) {
            return -1;
        }
        aligned_total = align_up(total, object->tls_align);
        if (aligned_total < total || object->tls_memsz > UINT64_MAX - aligned_total) {
            return -1;
        }
        total = aligned_total + object->tls_memsz;
    }
    if (total == 0u) {
        return 0;
    }
    total = align_up(total, ELF_PAGE_SIZE);
    spec->tls_addr = ELF_TLS_ALLOC_BASE;
    spec->tls_size = total;
    spec->tls_fs_base = spec->tls_addr + total;
    uint8_t *tls = kmalloc((size_t)total);
    if (tls == NULL) {
        return -1;
    }
    memset(tls, 0, (size_t)total);
    cursor = total;
    for (size_t i = spec->object_count; i > 0u; --i) {
        elf_object_t *object = &spec->objects[i - 1u];
        uint64_t block_end;
        if (object->tls_memsz == 0u) {
            object->tls_offset = 0;
            continue;
        }
        if (object->tls_memsz > cursor) {
            return -1;
        }
        cursor = align_down(cursor - object->tls_memsz, object->tls_align);
        object->tls_offset = (int64_t)cursor - (int64_t)total;
        block_end = cursor + object->tls_memsz;
        if (object->tls_filesz != 0u) {
            uint64_t tls_file_offset;
            if (elf_object_vaddr_to_offset(object, object->tls_vaddr, object->tls_filesz, &tls_file_offset) != 0 ||
                cursor > total || object->tls_filesz > total - cursor) {
                kfree(tls);
                return -1;
            }
            memcpy(tls + cursor, object->file + tls_file_offset, (size_t)object->tls_filesz);
        }
        if (block_end > total) {
            kfree(tls);
            return -1;
        }
    }
    /* Keep the allocation as one ordinary private user region.  The caller
     * adds it to the process-memory namespace and frees it with the image. */
    spec->tls_data = tls;
    return 0;
}

static uint8_t *elf_find_stack_region(elf_image_spec_t *spec, uint64_t *stack_bottom_out, size_t *stack_size_out) {
    if (spec == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < spec->region_count; ++i) {
        if ((spec->regions[i].kind_flags & PROCESS_MEMORY_FLAG_STACK) == 0u ||
            spec->regions[i].data == NULL) {
            continue; /* the lazy growth reserve holds no initial data */
        }

        if (stack_bottom_out != NULL) {
            *stack_bottom_out = spec->regions[i].addr;
        }
        if (stack_size_out != NULL) {
            *stack_size_out = spec->regions[i].length;
        }
        return spec->regions[i].data;
    }

    return NULL;
}

/* The System V x86-64 process initialization stack: argc, argv, envp and
   the auxiliary vector (whose AT_* numbers the LINUX_AT_* names spell). Both
   the native Vex ABI and the Linux personality start with this layout. */
static int elf_build_initial_stack(elf_image_spec_t *spec,
                                  const char *const *argv, size_t argc,
                                  const char *const *envp, size_t envc,
                                  const char *default_argv0) {
    uint8_t random_bytes[16];
    elf_auxv_entry_t auxv[13];
    uint64_t argv_addresses[ELF_MAX_EXEC_ARGS];
    uint64_t env_addresses[ELF_MAX_EXEC_ARGS];
    uint64_t stack_bottom = 0u;
    size_t stack_size = 0u;
    uint8_t *stack = elf_find_stack_region(spec, &stack_bottom, &stack_size);
    size_t auxc = 0u;
    uint64_t sp;
    uint64_t random_addr;
    uint64_t execfn_addr;
    uint64_t uid;
    uint64_t gid;

    if (spec == NULL || default_argv0 == NULL || stack == NULL ||
        stack_size < 512u || argc > ELF_MAX_EXEC_ARGS || envc > ELF_MAX_EXEC_ARGS) {
        return -1;
    }

    /* A NULL/empty argv is retained as a native compatibility convenience,
       but real execve vectors are copied verbatim into the new stack. */
    if (argc == 0u) {
        argv = &default_argv0;
        argc = 1u;
    }

    uid = scheduler_current_uid();
    gid = uid;

    sp = stack_bottom + stack_size;

    if (kernel_random_try_fill(random_bytes, sizeof(random_bytes)) != 0) return -1;
    if (sp < stack_bottom + sizeof(random_bytes)) {
        return -1;
    }
    sp -= sizeof(random_bytes);
    random_addr = sp;
    memcpy(stack + (sp - stack_bottom), random_bytes, sizeof(random_bytes));

    for (size_t index = envc; index > 0u; --index) {
        const char *value = envp[index - 1u];
        const size_t length = strlen(value) + 1u;
        if (length > (size_t)(sp - stack_bottom)) {
            return -1;
        }
        sp -= length;
        memcpy(stack + (sp - stack_bottom), value, length);
        env_addresses[index - 1u] = sp;
    }

    for (size_t index = argc; index > 0u; --index) {
        const char *value = argv[index - 1u];
        const size_t length = strlen(value) + 1u;
        if (length > (size_t)(sp - stack_bottom)) {
            return -1;
        }
        sp -= length;
        memcpy(stack + (sp - stack_bottom), value, length);
        argv_addresses[index - 1u] = sp;
    }

    {
        const size_t length = strlen(default_argv0) + 1u;
        if (length > (size_t)(sp - stack_bottom)) {
            return -1;
        }
        sp -= length;
        memcpy(stack + (sp - stack_bottom), default_argv0, length);
        execfn_addr = sp;
    }

    sp &= ~0xFul;

    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_PHDR, spec->phdr_addr };
    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_PHENT, spec->phent_size };
    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_PHNUM, spec->phnum };
    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_PAGESZ, ELF_PAGE_SIZE };
    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_BASE, spec->interp_base };
    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_ENTRY, spec->program_entry };
    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_UID, uid };
    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_EUID, uid };
    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_GID, gid };
    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_EGID, gid };
    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_SECURE, 0u };
    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_RANDOM, random_addr };
    auxv[auxc++] = (elf_auxv_entry_t) { LINUX_AT_EXECFN, execfn_addr };

    /* The complete initial block must end on a 16-byte boundary while
     * preserving the required argc/argv/envp adjacency.  If its qword count
     * has odd parity, put one padding qword below the strings, before auxv;
     * inserting it next to argc would change the ABI-visible vector layout. */
    {
        const size_t initial_words = (auxc * 2u + 2u) + (envc + 1u) +
            (argc + 1u) + 1u;
        if ((initial_words & 1u) != 0u) {
            if (sp < stack_bottom + sizeof(uint64_t)) {
                return -1;
            }
            sp -= sizeof(uint64_t);
        }
    }

    if (sp < stack_bottom + (auxc * 2u + 2u) * sizeof(uint64_t)) {
        return -1;
    }
    sp -= (auxc * 2u + 2u) * sizeof(uint64_t);
    {
        uint64_t *words = (uint64_t *)(stack + (sp - stack_bottom));
        for (size_t i = 0; i < auxc; ++i) {
            words[i * 2u] = auxv[i].key;
            words[i * 2u + 1u] = auxv[i].value;
        }
        words[auxc * 2u] = LINUX_AT_NULL;
        words[auxc * 2u + 1u] = 0u;
    }

    if (sp < stack_bottom + (envc + 1u) * sizeof(uint64_t) ||
        sp - (envc + 1u) * sizeof(uint64_t) < stack_bottom) {
        return -1;
    }
    sp -= (envc + 1u) * sizeof(uint64_t);
    {
        uint64_t *words = (uint64_t *)(stack + (sp - stack_bottom));
        for (size_t index = 0u; index < envc; ++index) {
            words[index] = env_addresses[index];
        }
        words[envc] = 0u;
    }

    if (sp < stack_bottom + (argc + 1u) * sizeof(uint64_t)) {
        return -1;
    }
    sp -= (argc + 1u) * sizeof(uint64_t);
    {
        uint64_t *words = (uint64_t *)(stack + (sp - stack_bottom));
        for (size_t index = 0u; index < argc; ++index) {
            words[index] = argv_addresses[index];
        }
        words[argc] = 0u;
    }

    if (sp < stack_bottom + sizeof(uint64_t)) {
        return -1;
    }
    sp -= sizeof(uint64_t);
    *(uint64_t *)(stack + (sp - stack_bottom)) = argc;

    /* The dynamic loader is entered directly from the initial user frame and
     * calls its first C helper before it has a chance to realign %rsp.  The
     * SysV x86-64 entry contract therefore requires the initial stack itself
     * to be 16-byte aligned.  Padding, when needed, was placed below the
     * strings so argc/argv/envp remain contiguous. */
    spec->user_stack_top = sp;
    return 0;
}

static int load_elf_image_spec_source(const char *path,
                               const char *const *argv, size_t argc,
                               const char *const *envp, size_t envc,
                               elf_image_spec_t *out,const void *source,size_t source_size) {
    ora_view_t fs_lease_1 __attribute__((cleanup(ora_view_release)))={0};
    ora_view_t fs_lease_2 __attribute__((cleanup(ora_view_release)))={0};
    void *data = NULL;
    size_t size = 0u;
    char launch_path[ELF_PATH_MAX];
    char interp_path[ELF_PATH_MAX];
    void *interp_data = NULL;
    size_t interp_size = 0u;
    const uint8_t *file;
    const elf64_header_t *header;
    const elf64_program_header_t *phdrs;
    const uint8_t *interp_file = NULL;
    const elf64_header_t *interp_header = NULL;
    const elf64_program_header_t *interp_phdrs = NULL;
    size_t load_region_count = 0u;
    size_t region_index = 0u;
    uint64_t highest_non_stack_end = ELF_USER_ALLOC_BASE;
    uint8_t has_interp = 0u;
    uint64_t load_bias = 0u;
    uint64_t interp_load_bias = 0u;

    if (out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    out->objects = kmalloc(ELF_MAX_OBJECTS * sizeof(*out->objects));
    if (out->objects == NULL) {
        return -1;
    }
    memset(out->objects, 0, ELF_MAX_OBJECTS * sizeof(*out->objects));

    if(source) {
        if(inst_abi_for_bytes(source,source_size)!=VEX_ABI_NATIVE || strlen(path)>=sizeof(launch_path)) {
            free_elf_image_spec(out);return -1;
        }
        memcpy(launch_path,path,strlen(path)+1);out->abi=VEX_ABI_NATIVE;data=(void *)source;size=source_size;
    }else {
    if (inst_prepare_for_execution(path, launch_path, sizeof(launch_path)) != 0) {
        debug_log_puts("elf: inst_prepare failed\n");
        return -1;
    }
    }

    if(!source)out->abi = inst_abi_for_path(launch_path);
    if (!source && (resolve_launch_path(launch_path, launch_path) != 0 ||
        fs_read_view(launch_path,&data,&size, &fs_lease_1) != 0)) {
        debug_log_puts("elf: fs_read main failed ");
        debug_log_puts(launch_path);
        debug_log_puts("\n");
        return -1;
    }

    if (size == 0u || (out->image_data = kmalloc(size)) == NULL) {
        return -1;
    }
    memcpy(out->image_data, data, size);
    out->image_size = size;
    file = out->image_data;
    header = (const elf64_header_t *)file;
    if (!valid_elf64(header, size)) {
        debug_log_puts("elf: invalid elf\n");
        free_elf_image_spec(out);
        return -1;
    }

    phdrs = (const elf64_program_header_t *)(file + header->phoff);
    out->object_count = 1u;
    memset(&out->objects[0], 0, sizeof(out->objects[0]));
    if (elf_object_bind(&out->objects[0], out->image_data, out->image_size,
                        elf_compute_load_bias(header, phdrs, ELF_USER_ALLOC_BASE),
                        0u, launch_path) != 0) {
        debug_log_puts("elf: invalid dynamic metadata\n");
        free_elf_image_spec(out);
        return -1;
    }
    out->objects[0].module_id = 1u;
    for (uint16_t i = 0; i < header->phnum; ++i) {
        if (phdrs[i].type == ELF_PT_INTERP) {
            if(source){free_elf_image_spec(out);return -1;}
            has_interp = 1u;
            if (phdrs[i].filesz == 0u || phdrs[i].offset > size || phdrs[i].filesz > size - phdrs[i].offset || phdrs[i].filesz >= sizeof(interp_path)) {
                free_elf_image_spec(out);
                return -1;
            }
            memcpy(interp_path, file + phdrs[i].offset, (size_t)phdrs[i].filesz);
            interp_path[phdrs[i].filesz - 1u] = '\0';
            out->defer_dynamic_linker =
                starts_with(interp_path, "/lib64/ld-linux") ||
                starts_with(interp_path, "/lib/ld-linux");
        }
    }
    load_region_count = elf_count_load_regions(header, phdrs);
    load_bias = elf_compute_load_bias(header, phdrs, ELF_USER_ALLOC_BASE);

    if (out->abi == VEX_ABI_LINUX && header->type == ELF_TYPE_DYN) {
        debug_log_puts("elf: linux ET_DYN detected\n");
    }
    if (out->abi == VEX_ABI_LINUX && has_interp != 0u) {
        if (fs_read_view(interp_path,&interp_data,&interp_size, &fs_lease_2) != 0) {
            debug_log_puts("elf: fs_read interp failed ");
            debug_log_puts(interp_path);
            debug_log_puts("\n");
            free_elf_image_spec(out);
            return -1;
        }
        if (interp_size == 0u || (out->interp_data = kmalloc(interp_size)) == NULL) {
            free_elf_image_spec(out);
            return -1;
        }
        memcpy(out->interp_data, interp_data, interp_size);
        out->interp_size = interp_size;
        interp_file = out->interp_data;
        interp_header = (const elf64_header_t *)interp_file;
        if (!valid_elf64(interp_header, interp_size)) {
            debug_log_puts("elf: invalid interp elf\n");
            free_elf_image_spec(out);
            return -1;
        }
        interp_phdrs = (const elf64_program_header_t *)(interp_file + interp_header->phoff);
        interp_load_bias = elf_compute_load_bias(interp_header, interp_phdrs, ELF_INTERP_ALLOC_BASE);
        if (out->object_count >= ELF_MAX_OBJECTS) {
            free_elf_image_spec(out);
            return -1;
        }
        memset(&out->objects[out->object_count], 0, sizeof(out->objects[0]));
        if (elf_object_bind(&out->objects[out->object_count], out->interp_data, out->interp_size,
                            interp_load_bias, 0u, interp_path) != 0) {
            debug_log_puts("elf: invalid interpreter dynamic metadata\n");
            free_elf_image_spec(out);
            return -1;
        }
        out->objects[out->object_count].is_interpreter = 1u;
        out->objects[out->object_count].module_id = (uint32_t)out->object_count + 1u;
        ++out->object_count;
        debug_log_puts("elf: PT_INTERP detected; loading dynamic interpreter\n");
    }

    if (out->defer_dynamic_linker == 0u) {
        if(source)for(uint16_t h=0;h<header->phnum;h++)if(phdrs[h].type==ELF_PT_DYNAMIC) {
            const elf64_program_header_t *d=&phdrs[h];
            if(d->offset>size || d->filesz>size-d->offset){free_elf_image_spec(out);return -1;}
            const elf64_dynamic_entry_t *entries=(const void *)(file+d->offset);
            for(size_t j=0;j<d->filesz/sizeof(*entries) && entries[j].tag!=ELF_DT_NULL;j++)
                if(entries[j].tag==ELF_DT_NEEDED){free_elf_image_spec(out);return -1;}
        }
        if (elf_load_needed_objects(out) != 0) {
            debug_log_puts("elf: dependency loading failed\n");
            free_elf_image_spec(out);
            return -1;
        }
    } else {
        debug_log_puts("elf: deferring dependency loading to ld-linux\n");
    }

    if (out->defer_dynamic_linker == 0u) {
        if (elf_prepare_tls(out) != 0) {
            debug_log_puts("elf: TLS image preparation failed\n");
            free_elf_image_spec(out);
            return -1;
        }
    }
    for (size_t object_index = 0u;
         out->defer_dynamic_linker == 0u && object_index < out->object_count;
         ++object_index) {
        /* A real PT_INTERP image is itself a dynamic linker.  Its bootstrap
         * relocator must see the original RELA/RELR records, in particular
         * R_X86_64_IRELATIVE resolvers; executing those resolvers in the
         * kernel while the user address space is not active is unsafe. */
        if (out->objects[object_index].is_interpreter != 0u) {
            continue;
        }
        if (elf_apply_object_relocations(out, &out->objects[object_index]) != 0) {
            debug_log_puts("elf: dynamic relocations failed\n");
            free_elf_image_spec(out);
            return -1;
        }
    }

    load_region_count = 0u;
    for (size_t object_index = 0u; object_index < out->object_count; ++object_index) {
        load_region_count += elf_count_load_regions(out->objects[object_index].header,
                                                    out->objects[object_index].phdrs);
    }
    const size_t extra_region_count = 2u + (out->tls_data != NULL ? 1u : 0u);
    out->regions = kmalloc((load_region_count + extra_region_count) * sizeof(*out->regions));
    if (out->regions == NULL) {
        free_elf_image_spec(out);
        return -1;
    }
    memset(out->regions, 0, (load_region_count + extra_region_count) * sizeof(*out->regions));
    out->region_count = load_region_count + extra_region_count;

    if (elf_append_load_regions(out, &region_index, file, size, header, phdrs, load_bias, &highest_non_stack_end) != 0) {
        free_elf_image_spec(out);
        return -1;
    }
    for (size_t object_index = 1u; object_index < out->object_count; ++object_index) {
        scheduler_safe_point(); /* image spec is still unpublished */
        elf_object_t *object = &out->objects[object_index];
        if (elf_append_load_regions(out, &region_index, object->file, object->size,
                                    object->header, object->phdrs, object->load_bias,
                                    &highest_non_stack_end) != 0) {
            free_elf_image_spec(out);
            return -1;
        }
    }
    if (out->tls_data != NULL) {
        if (add_region_spec(out, &region_index, out->tls_addr, (size_t)out->tls_size,
                            (size_t)out->tls_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, 0u, out->tls_data, 0u) != 0) {
            free_elf_image_spec(out);
            return -1;
        }
        highest_non_stack_end = align_up(out->tls_addr + out->tls_size, ELF_PAGE_SIZE) > highest_non_stack_end
            ? align_up(out->tls_addr + out->tls_size, ELF_PAGE_SIZE) : highest_non_stack_end;
    }

    {
        const uint64_t stack_bottom = ELF_USER_STACK_TOP - (ELF_USER_STACK_PAGES * ELF_PAGE_SIZE);
        const size_t stack_size = (size_t)(ELF_USER_STACK_TOP - stack_bottom);
        uint8_t *stack = kmalloc(stack_size);

        if (stack == NULL) {
            free_elf_image_spec(out);
            return -1;
        }

        memset(stack, 0, stack_size);
        if (add_region_spec(out, &region_index, stack_bottom, stack_size, stack_size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, PROCESS_MEMORY_FLAG_STACK, stack, 1u) != 0) {
            kfree(stack);
            free_elf_image_spec(out);
            return -1;
        }
        /* The rest of the main thread's stack (ELF_USER_STACK_RESERVE in
           all, as the usual 8 MiB RLIMIT_STACK) is reserved below and
           backed page by page on first touch. Assemblers, linkers and deep
           recursion need far more than the initial pages. */
        if (add_region_spec(out, &region_index, ELF_USER_STACK_TOP - ELF_USER_STACK_RESERVE,
                (size_t)(ELF_USER_STACK_RESERVE - stack_size), 0u, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                PROCESS_MEMORY_FLAG_STACK | PROCESS_MEMORY_FLAG_VMM_DIRECT | PROCESS_MEMORY_FLAG_LAZY,
                NULL, 0u) != 0) {
            free_elf_image_spec(out);
            return -1;
        }
    }

    out->program_entry = load_bias + header->entry;
    out->entry = interp_header != NULL ? (interp_load_bias + interp_header->entry) : out->program_entry;
    out->interp_base = interp_load_bias;
    out->phdr_addr = elf_phdr_address(header, phdrs, load_bias);
    out->phent_size = header->phentsize;
    out->phnum = header->phnum;
    out->next_alloc_hint = align_up(highest_non_stack_end, ELF_PAGE_SIZE);
    if (elf_build_initial_stack(out, argv, argc, envp, envc, launch_path) != 0) {
        free_elf_image_spec(out);
        return -1;
    }
    return 0;
}

static int register_elf_image_spec(uint32_t pid, uint64_t namespace_id, const elf_image_spec_t *spec) {
    if (spec == NULL) {
        return -1;
    }

    for (size_t index = 0u; index < spec->region_count; ++index) {
        const elf_region_spec_t *region = &spec->regions[index];

        if (process_memory_register_region_in_namespace_partial(pid, namespace_id, region->addr, region->length, region->prot,
                region->flags, region->kind_flags, region->data, region->data_size) != 0) {
            return -1;
        }
    }

    return 0;
}

static int load_elf_image_spec(const char *path,const char *const *argv,size_t argc,
    const char *const *envp,size_t envc,elf_image_spec_t *out) {
    return load_elf_image_spec_source(path,argv,argc,envp,envc,out,NULL,0);
}
static int load_elf_process_source(const char *path, const char *const *argv, size_t argc,
                                 const char *const *envp, size_t envc,
                                 int wait_for_exit, int *status_out, uint32_t *pid_out, uint32_t caps,
                                 int runtime_abi,const void *source,size_t source_size,
                                 int (*prepare)(uint32_t,void *),void *context) {
    elf_image_spec_t spec;
    void *pml4 = NULL;
    uint64_t cr3 = 0u;
    uint64_t namespace_id = 0u;
    uint32_t created_pid = 0u;
    int result = -1;

    if (pid_out != NULL) {
        *pid_out = 0u;
    }

    if (load_elf_image_spec_source(path, argv, argc, envp, envc, &spec,source,source_size) != 0) {
        debug_log_puts("elf: load image spec failed\n");
        return -1;
    }
    /* Trusted executable-format dispatch may select an existing wire domain
       for a native userspace runtime. Not derived from a pointer or argv. */
    if(runtime_abi>=0 && (runtime_abi!=VEX_ABI_PE || spec.abi!=VEX_ABI_NATIVE)) {
        free_elf_image_spec(&spec);return -1;
    }

    pml4 = vmm_new_address_space();
    if (pml4 == NULL) {
        debug_log_puts("elf: new address space failed\n");
        goto out;
    }

    cr3 = vmm_address_space_to_cr3(pml4);
    if (cr3 == 0u) {
        debug_log_puts("elf: cr3 alloc failed\n");
        goto out;
    }

    created_pid = process_prepare_user_abi_caps((void (*)(void))spec.entry, ELF_LOAD_PRIORITY, cr3, spec.user_stack_top,
                                               runtime_abi<0?spec.abi:(uint8_t)runtime_abi, caps);
    if (created_pid == 0u) {
        debug_log_puts("elf: process_create_user_abi failed\n");
        goto out;
    }
    (void)scheduler_set_process_name(created_pid, path);

    if (process_memory_namespace_begin(created_pid, spec.next_alloc_hint, &namespace_id) != 0) {
        debug_log_puts("elf: namespace begin failed\n");
        (void)scheduler_abort_process(created_pid);
        goto out;
    }
    if (register_elf_image_spec(created_pid, namespace_id, &spec) != 0) {
        debug_log_puts("elf: register image spec failed\n");
        if (namespace_id != 0u) {
            (void)process_memory_namespace_destroy(created_pid, namespace_id, cr3);
        }
        (void)scheduler_abort_process(created_pid);
        goto out;
    }
    if (process_memory_materialize_namespace(created_pid, namespace_id, cr3) != 0) {
        debug_log_puts("elf: namespace materialize failed\n");
        if (namespace_id != 0u) {
            (void)process_memory_namespace_destroy(created_pid, namespace_id, cr3);
        }
        (void)scheduler_abort_process(created_pid);
        goto out;
    }
    if (process_memory_namespace_commit(created_pid, namespace_id) != 0) {
        debug_log_puts("elf: namespace commit failed\n");
        if (namespace_id != 0u) {
            (void)process_memory_namespace_destroy(created_pid, namespace_id, cr3);
        }
        (void)scheduler_abort_process(created_pid);
        goto out;
    }

    scheduler_set_process_user_alloc_next(created_pid, process_memory_next_alloc_hint(created_pid, spec.next_alloc_hint));
    scheduler_set_process_fs_base(created_pid, spec.tls_fs_base);

    if(prepare && prepare(created_pid,context)) {(void)scheduler_abort_process(created_pid);goto out;}
    if(scheduler_publish_process(created_pid)!=0) {
        (void)scheduler_abort_process(created_pid);goto out;
    }

    if (pid_out != NULL) {
        *pid_out = created_pid;
    }
    if (wait_for_exit) {
        /* A wakeup is not an exit notification. Wait for this exact child
         * and collect it, so native run does not retain its dead Process. */
        if (process_wait_pid((int32_t)created_pid, status_out, 0u, NULL) !=
            (int)created_pid) {
            goto out;
        }
    }

    result = 0;

out:
    free_elf_image_spec(&spec);
    if (result != 0 && created_pid == 0u && pml4 != NULL) {
        vmm_destroy_address_space(pml4);
    }
    return result;
}

static int load_elf_process_args(const char *path,const char *const *argv,size_t argc,
    const char *const *envp,size_t envc,int wait,int *status,uint32_t *pid,uint32_t caps,int runtime_abi) {
    return load_elf_process_source(path,argv,argc,envp,envc,wait,status,pid,caps,runtime_abi,NULL,0,NULL,NULL);
}
int elf_spawn_native_snapshot(const void *data,size_t length,const char *const *argv,size_t argc,
    const char *const *envp,size_t envc,int (*prepare)(uint32_t,void *),void *context,uint32_t *pid) {
    return load_elf_process_source(argc?argv[0]:"native-resource",argv,argc,envp,envc,0,NULL,pid,0,-1,data,length,prepare,context);
}
static int load_elf_process(const char *path, int wait_for_exit, uint32_t *pid_out, uint32_t caps) {
    return load_elf_process_args(path, NULL, 0u, NULL, 0u, wait_for_exit, NULL, pid_out, caps,-1);
}

int elf_load_args(const char *path, const char *const *argv, size_t argc,
                  const char *const *envp, size_t envc, int *status_out) {
    if (inst_abi_for_path(path) == VEX_ABI_PE) {
        return pe_load_args(path, argv, argc, envp, envc, status_out);
    }
    return load_elf_process_args(path, argv, argc, envp, envc, 1, status_out, NULL, 0u,-1);
}

int elf_load(const char *path) {
    if (inst_abi_for_path(path) == VEX_ABI_PE) {
        return pe_load(path);
    }
    return load_elf_process(path, 1, NULL, 0u);
}

int elf_spawn(const char *path) {
    return elf_spawn_pid(path, NULL);
}

int elf_spawn_pid(const char *path, uint32_t *pid_out) {
    if (pid_out != NULL) {
        *pid_out = 0u;
    }
    if (inst_abi_for_path(path) == VEX_ABI_PE) {
        return pe_spawn_pid(path, pid_out);
    }
    return load_elf_process(path, 0, pid_out, 0u);
}

int elf_spawn_pid_caps(const char *path, uint32_t *pid_out, uint32_t caps) {
    if(pid_out)*pid_out=0;
    if(inst_abi_for_path(path)==VEX_ABI_PE)return pe_spawn_pid_caps(path,pid_out,caps);
    return load_elf_process(path,0,pid_out,caps);
}

int elf_run_native_runtime(const char *path, const char *const *argv, size_t argc,
                          const char *const *envp, size_t envc, uint32_t *pid, uint32_t caps,
                          int wait, int *status, uint8_t abi) {
    if(pid)*pid=0;
    /* Internal helper used to launch native runtimes with a kernel-owned argv.
       PE interpretation never takes place in this function. */
    if(inst_abi_for_path(path)==VEX_ABI_PE)return -1;
    return load_elf_process_args(path,argv,argc,envp,envc,wait,status,pid,caps,abi);
}

static int exec_elf_process_args(const char *path,
                          const char *const *argv, size_t argc,
                          const char *const *envp, size_t envc,int runtime_abi) {
    elf_image_spec_t spec;
    process_t *process = scheduler_current_process();
    void *pml4 = NULL;
    uint64_t cr3 = 0u;
    uint64_t old_namespace_id;
    uint64_t new_namespace_id = 0u;
    uint64_t alloc_hint;
    uint64_t entry;
    uint64_t user_stack_top;
    uint8_t abi;

    if (process == NULL) {
        return -1;
    }
    if (load_elf_image_spec(path, argv, argc, envp, envc, &spec) != 0) {
        return -1;
    }
    if(runtime_abi>=0 && (runtime_abi!=VEX_ABI_PE || spec.abi!=VEX_ABI_NATIVE)) {
        free_elf_image_spec(&spec);return -1;
    }

    /* A clone3 posix_spawn child temporarily borrows the parent's process
       group/address space.  Split the process identity before creating the
       new namespace so MRAC/process_memory and FD ownership are keyed by the
       child PID while the parent keeps running. */
    if (process_prepare_exec_identity() != 0) {
        free_elf_image_spec(&spec);
        return -1;
    }

    pml4 = vmm_new_address_space();
    if (pml4 == NULL) {
        free_elf_image_spec(&spec);
        return -1;
    }

    cr3 = vmm_address_space_to_cr3(pml4);
    if (cr3 == 0u) {
        free_elf_image_spec(&spec);
        vmm_destroy_address_space(pml4);
        return -1;
    }

    vex_jit_reap_pid(scheduler_current_process_id(), process->cr3);

    old_namespace_id = process_memory_current_namespace(scheduler_current_process_id());
    if (process_memory_namespace_begin(scheduler_current_process_id(), spec.next_alloc_hint, &new_namespace_id) != 0) {
        free_elf_image_spec(&spec);
        vmm_destroy_address_space(pml4);
        return -1;
    }

    if (register_elf_image_spec(scheduler_current_process_id(), new_namespace_id, &spec) != 0 ||
        process_memory_materialize_namespace(scheduler_current_process_id(), new_namespace_id, cr3) != 0 ||
        process_memory_namespace_commit(scheduler_current_process_id(), new_namespace_id) != 0) {
        (void)process_memory_namespace_destroy(scheduler_current_process_id(), new_namespace_id, cr3);
        free_elf_image_spec(&spec);
        vmm_destroy_address_space(pml4);
        return -1;
    }

    if (old_namespace_id != 0u) {
        /* The old CR3 still owns its user PTEs until the exec trampoline
         * tears it down.  Release the namespace first so ORA_DIRECT/shared
         * pages are unmapped exactly once before VMM walks the tables. */
        (void)process_memory_namespace_destroy(scheduler_current_process_id(), old_namespace_id, process->cr3);
    }

    entry = spec.entry;
    user_stack_top = spec.user_stack_top;
    abi = runtime_abi<0?spec.abi:(uint8_t)runtime_abi;
    alloc_hint = process_memory_next_alloc_hint(scheduler_current_process_id(), spec.next_alloc_hint);
    scheduler_set_current_fs_base(spec.tls_fs_base);
    (void)scheduler_set_process_name(scheduler_current_pid(), path);
    free_elf_image_spec(&spec);
    return process_exec_image(cr3, entry, user_stack_top, abi, alloc_hint);
}

int elf_exec_current_args(const char *path, const char *const *argv, size_t argc,
                          const char *const *envp, size_t envc) {
    if(inst_abi_for_path(path)==VEX_ABI_PE)return pe_exec_args(path,argv,argc,envp,envc);
    return exec_elf_process_args(path,argv,argc,envp,envc,-1);
}
int elf_exec_native_runtime(const char *path, const char *const *argv, size_t argc,
                            const char *const *envp, size_t envc,uint8_t abi) {
    return exec_elf_process_args(path,argv,argc,envp,envc,abi);
}
int elf_exec_current(const char *path) {
    return elf_exec_current_args(path, NULL, 0u, NULL, 0u);
}
