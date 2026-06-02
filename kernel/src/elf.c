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
#define ELF_PT_INTERP 3u
#define ELF_PF_W 0x2u
#define ELF_PAGE_SIZE 4096ull
#define ELF_LOAD_PRIORITY 1u
#define ELF_USER_ALLOC_BASE 0x0000000100000000ull
#define ELF_INTERP_ALLOC_BASE 0x0000000200000000ull
#define ELF_USER_STACK_TOP 0x00007ffffff000ull
#define ELF_USER_STACK_PAGES 128ull
#define ELF_PATH_MAX 160u

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

        prot = PROT_READ | PROT_EXEC;
        if ((phdrs[i].flags & ELF_PF_W) != 0u) {
            prot |= PROT_WRITE;
        }

        mapped_addr = load_bias + phdrs[i].vaddr;
        if (add_region_spec(spec, region_index, mapped_addr, (size_t)phdrs[i].memsz, (size_t)phdrs[i].filesz,
                prot, MAP_PRIVATE, 0u, (uint8_t *)file + phdrs[i].offset, 0u) != 0) {
            return -1;
        }

        segment_end = align_up(mapped_addr + phdrs[i].memsz, ELF_PAGE_SIZE);
        if (segment_end > *highest_non_stack_end) {
            *highest_non_stack_end = segment_end;
        }
    }

    return 0;
}

static uint8_t *elf_find_stack_region(elf_image_spec_t *spec, uint64_t *stack_bottom_out, size_t *stack_size_out) {
    if (spec == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < spec->region_count; ++i) {
        if ((spec->regions[i].kind_flags & PROCESS_MEMORY_FLAG_STACK) == 0u) {
            continue;
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

static int elf_build_linux_stack(elf_image_spec_t *spec, const char *argv0) {
    static const uint8_t random_bytes[16] = {
        0x56, 0x65, 0x78, 0x4f, 0x53, 0x57, 0x65, 0x62,
        0x4b, 0x69, 0x74, 0x4a, 0x49, 0x54, 0x21, 0x00
    };
    elf_auxv_entry_t auxv[12];
    uint64_t stack_bottom = 0u;
    size_t stack_size = 0u;
    uint8_t *stack = elf_find_stack_region(spec, &stack_bottom, &stack_size);
    size_t argv0_len;
    size_t auxc = 0u;
    uint64_t sp;
    uint64_t random_addr;
    uint64_t argv0_addr;
    uint64_t uid;
    uint64_t gid;

    if (spec == NULL || argv0 == NULL || stack == NULL || stack_size < 512u) {
        return -1;
    }

    uid = scheduler_current_uid();
    gid = uid;
    argv0_len = strlen(argv0) + 1u;
    if (argv0_len > 256u) {
        argv0_len = 256u;
    }

    sp = stack_bottom + stack_size;

    sp -= sizeof(random_bytes);
    memcpy(stack + (sp - stack_bottom), random_bytes, sizeof(random_bytes));
    random_addr = sp;

    sp -= argv0_len;
    memcpy(stack + (sp - stack_bottom), argv0, argv0_len);
    argv0_addr = sp;

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

    sp -= sizeof(uint64_t);
    *(uint64_t *)(stack + (sp - stack_bottom)) = 0u; /* envp terminator */
    sp -= sizeof(uint64_t);
    *(uint64_t *)(stack + (sp - stack_bottom)) = 0u; /* argv terminator */
    sp -= sizeof(uint64_t);
    *(uint64_t *)(stack + (sp - stack_bottom)) = argv0_addr;
    sp -= sizeof(uint64_t);
    *(uint64_t *)(stack + (sp - stack_bottom)) = 1u; /* argc */

    spec->user_stack_top = sp;
    return 0;
}

static int load_elf_image_spec(const char *path, elf_image_spec_t *out) {
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
    size_t interp_load_region_count = 0u;
    size_t region_index = 0u;
    uint64_t highest_non_stack_end = ELF_USER_ALLOC_BASE;
    uint8_t has_interp = 0u;
    uint64_t load_bias = 0u;
    uint64_t interp_load_bias = 0u;

    if (out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    if (inst_prepare_for_execution(path, launch_path, sizeof(launch_path)) != 0) {
        debug_log_puts("elf: inst_prepare failed\n");
        return -1;
    }

    out->abi = inst_abi_for_path(launch_path);
    if ((out->abi == VEX_ABI_NATIVE && !starts_with(launch_path, "/home/root/bin/")) ||
        fs_read(launch_path, &data, &size) != 0) {
        debug_log_puts("elf: fs_read main failed ");
        debug_log_puts(launch_path);
        debug_log_puts("\n");
        return -1;
    }

    file = data;
    header = (const elf64_header_t *)file;
    if (!valid_elf64(header, size)) {
        debug_log_puts("elf: invalid elf\n");
        return -1;
    }

    phdrs = (const elf64_program_header_t *)(file + header->phoff);
    for (uint16_t i = 0; i < header->phnum; ++i) {
        if (phdrs[i].type == ELF_PT_INTERP) {
            has_interp = 1u;
            if (phdrs[i].filesz == 0u || phdrs[i].offset > size || phdrs[i].filesz > size - phdrs[i].offset || phdrs[i].filesz >= sizeof(interp_path)) {
                return -1;
            }
            memcpy(interp_path, file + phdrs[i].offset, (size_t)phdrs[i].filesz);
            interp_path[phdrs[i].filesz - 1u] = '\0';
        }
    }
    load_region_count = elf_count_load_regions(header, phdrs);
    load_bias = elf_compute_load_bias(header, phdrs, ELF_USER_ALLOC_BASE);

    if (out->abi == VEX_ABI_LINUX && header->type == ELF_TYPE_DYN) {
        tty_puts("elf: linux ET_DYN detected\n");
    }
    if (out->abi == VEX_ABI_LINUX && has_interp != 0u) {
        if (fs_read(interp_path, &interp_data, &interp_size) != 0) {
            debug_log_puts("elf: fs_read interp failed ");
            debug_log_puts(interp_path);
            debug_log_puts("\n");
            return -1;
        }
        interp_file = interp_data;
        interp_header = (const elf64_header_t *)interp_file;
        if (!valid_elf64(interp_header, interp_size)) {
            debug_log_puts("elf: invalid interp elf\n");
            return -1;
        }
        interp_phdrs = (const elf64_program_header_t *)(interp_file + interp_header->phoff);
        interp_load_region_count = elf_count_load_regions(interp_header, interp_phdrs);
        interp_load_bias = elf_compute_load_bias(interp_header, interp_phdrs, ELF_INTERP_ALLOC_BASE);
        tty_puts("elf: PT_INTERP detected; loading dynamic interpreter\n");
    }

    out->regions = kmalloc((load_region_count + interp_load_region_count + 1u) * sizeof(*out->regions));
    if (out->regions == NULL) {
        return -1;
    }
    memset(out->regions, 0, (load_region_count + interp_load_region_count + 1u) * sizeof(*out->regions));
    out->region_count = load_region_count + interp_load_region_count + 1u;

    if (elf_append_load_regions(out, &region_index, file, size, header, phdrs, load_bias, &highest_non_stack_end) != 0) {
        free_elf_image_spec(out);
        return -1;
    }
    if (interp_header != NULL &&
        elf_append_load_regions(out, &region_index, interp_file, interp_size, interp_header, interp_phdrs, interp_load_bias, &highest_non_stack_end) != 0) {
        free_elf_image_spec(out);
        return -1;
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
    }

    out->program_entry = load_bias + header->entry;
    out->entry = interp_header != NULL ? (interp_load_bias + interp_header->entry) : out->program_entry;
    out->interp_base = interp_load_bias;
    out->phdr_addr = load_bias + header->phoff;
    out->phent_size = header->phentsize;
    out->phnum = header->phnum;
    out->next_alloc_hint = align_up(highest_non_stack_end, ELF_PAGE_SIZE);
    if (out->abi == VEX_ABI_LINUX) {
        if (elf_build_linux_stack(out, launch_path) != 0) {
            free_elf_image_spec(out);
            return -1;
        }
    } else {
        out->user_stack_top = ELF_USER_STACK_TOP - 8ull;
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

static int load_elf_process(const char *path, int wait_for_exit, uint32_t *pid_out) {
    elf_image_spec_t spec;
    void *pml4 = NULL;
    uint64_t cr3 = 0u;
    uint64_t namespace_id = 0u;
    uint32_t created_pid = 0u;
    int result = -1;

    if (load_elf_image_spec(path, &spec) != 0) {
        debug_log_puts("elf: load image spec failed\n");
        return -1;
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

    created_pid = process_create_user_abi((void (*)(void))spec.entry, ELF_LOAD_PRIORITY, cr3, spec.user_stack_top, spec.abi);
    if (created_pid == 0u) {
        debug_log_puts("elf: process_create_user_abi failed\n");
        goto out;
    }

    if (process_memory_namespace_begin(created_pid, spec.next_alloc_hint, &namespace_id) != 0) {
        debug_log_puts("elf: namespace begin failed\n");
        process_kill(created_pid);
        goto out;
    }
    if (register_elf_image_spec(created_pid, namespace_id, &spec) != 0) {
        debug_log_puts("elf: register image spec failed\n");
        if (namespace_id != 0u) {
            (void)process_memory_namespace_destroy(created_pid, namespace_id, cr3);
        }
        process_kill(created_pid);
        goto out;
    }
    if (process_memory_materialize_namespace(created_pid, namespace_id, cr3) != 0) {
        debug_log_puts("elf: namespace materialize failed\n");
        if (namespace_id != 0u) {
            (void)process_memory_namespace_destroy(created_pid, namespace_id, cr3);
        }
        process_kill(created_pid);
        goto out;
    }
    if (process_memory_namespace_commit(created_pid, namespace_id) != 0) {
        debug_log_puts("elf: namespace commit failed\n");
        if (namespace_id != 0u) {
            (void)process_memory_namespace_destroy(created_pid, namespace_id, cr3);
        }
        process_kill(created_pid);
        goto out;
    }

    scheduler_set_process_user_alloc_next(created_pid, process_memory_next_alloc_hint(created_pid, spec.next_alloc_hint));

    if (pid_out != NULL) {
        *pid_out = created_pid;
    }
    if (wait_for_exit) {
        process_wait();
    }

    result = 0;

out:
    free_elf_image_spec(&spec);
    if (result != 0 && created_pid == 0u && pml4 != NULL) {
        vmm_destroy_address_space(pml4);
    }
    return result;
}

int elf_load(const char *path) {
    if (inst_abi_for_path(path) == VEX_ABI_PE) {
        return pe_load(path);
    }
    return load_elf_process(path, 1, NULL);
}

int elf_spawn(const char *path) {
    return elf_spawn_pid(path, NULL);
}

int elf_spawn_pid(const char *path, uint32_t *pid_out) {
    if (inst_abi_for_path(path) == VEX_ABI_PE) {
        return pe_spawn(path);
    }
    return load_elf_process(path, 0, pid_out);
}

int elf_exec_current(const char *path) {
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

    if (inst_abi_for_path(path) == VEX_ABI_PE) {
        return -1;
    }
    if (process == NULL) {
        return -1;
    }
    if (load_elf_image_spec(path, &spec) != 0) {
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

    vex_jit_reap_pid(process->pid, process->cr3);

    old_namespace_id = process_memory_current_namespace(process->pid);
    if (process_memory_namespace_begin(process->pid, spec.next_alloc_hint, &new_namespace_id) != 0) {
        free_elf_image_spec(&spec);
        vmm_destroy_address_space(pml4);
        return -1;
    }

    if (register_elf_image_spec(process->pid, new_namespace_id, &spec) != 0 ||
        process_memory_materialize_namespace(process->pid, new_namespace_id, cr3) != 0 ||
        process_memory_namespace_commit(process->pid, new_namespace_id) != 0) {
        (void)process_memory_namespace_destroy(process->pid, new_namespace_id, cr3);
        free_elf_image_spec(&spec);
        vmm_destroy_address_space(pml4);
        return -1;
    }

    if (old_namespace_id != 0u) {
        (void)process_memory_namespace_destroy(process->pid, old_namespace_id, 0u);
    }

    entry = spec.entry;
    user_stack_top = spec.user_stack_top;
    abi = spec.abi;
    alloc_hint = process_memory_next_alloc_hint(process->pid, spec.next_alloc_hint);
    free_elf_image_spec(&spec);
    return process_exec_image(cr3, entry, user_stack_top, abi, alloc_hint);
}
