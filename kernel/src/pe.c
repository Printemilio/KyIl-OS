#include <unos/pe.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/boot_requests.h>
#include <unos/compat.h>
#include <unos/fs.h>
#include <unos/pmm.h>
#include <unos/scheduler.h>
#include <unos/vmm.h>

#define PE_PAGE_SIZE 4096ull
#define PE_LOAD_PRIORITY 1u
#define PE_USER_STACK_TOP 0x00007ffffff000ull
#define PE_USER_STACK_PAGES 128ull
#define PE_MACHINE_X86_64 0x8664u
#define PE_OPTIONAL_MAGIC_PE32_PLUS 0x20bu
#define PE_SECTION_MEM_WRITE 0x80000000u

static uint16_t read_u16(const uint8_t *data, size_t size, size_t offset) {
    if (offset + 2u > size) {
        return 0;
    }
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *data, size_t size, size_t offset) {
    if (offset + 4u > size) {
        return 0;
    }
    return (uint32_t)data[offset] |
        ((uint32_t)data[offset + 1u] << 8u) |
        ((uint32_t)data[offset + 2u] << 16u) |
        ((uint32_t)data[offset + 3u] << 24u);
}

static uint64_t read_u64(const uint8_t *data, size_t size, size_t offset) {
    return (uint64_t)read_u32(data, size, offset) | ((uint64_t)read_u32(data, size, offset + 4u) << 32u);
}

static void write_u64(uint8_t *data, uint64_t value) {
    for (size_t i = 0; i < 8u; ++i) {
        data[i] = (uint8_t)(value >> (i * 8u));
    }
}

static uint64_t align_down(uint64_t value) {
    return value & ~(PE_PAGE_SIZE - 1ull);
}

static uint64_t align_up(uint64_t value) {
    return (value + PE_PAGE_SIZE - 1ull) & ~(PE_PAGE_SIZE - 1ull);
}

static int string_equals_casefold(const char *left, const char *right) {
    size_t i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        char a = left[i];
        char b = right[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a + ('a' - 'A'));
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b + ('a' - 'A'));
        }
        if (a != b) {
            return 0;
        }
        ++i;
    }
    return left[i] == right[i];
}

static void zero_page(void *page) {
    uint8_t *bytes = page;
    for (size_t i = 0; i < PE_PAGE_SIZE; ++i) {
        bytes[i] = 0;
    }
}

static uint8_t *mapped_ptr(void *pml4, uint64_t virt) {
    const uint64_t phys = vmm_get_physical(pml4, virt);
    if (phys == 0u || hhdm_request.response == NULL) {
        return NULL;
    }
    return (uint8_t *)(hhdm_request.response->offset + (phys & ~(PE_PAGE_SIZE - 1ull)) + (virt & (PE_PAGE_SIZE - 1ull)));
}

static int write_mapped(void *pml4, uint64_t virt, const void *src, size_t size) {
    const uint8_t *bytes = src;
    for (size_t i = 0; i < size; ++i) {
        uint8_t *dst = mapped_ptr(pml4, virt + i);
        if (dst == NULL) {
            return -1;
        }
        *dst = bytes[i];
    }
    return 0;
}

static int map_range(void *pml4, uint64_t base, uint64_t size, uint64_t flags) {
    const uint64_t start = align_down(base);
    const uint64_t end = align_up(base + size);

    for (uint64_t virt = start; virt < end; virt += PE_PAGE_SIZE) {
        void *phys = pmm_alloc_page();
        if (phys == NULL) {
            return -1;
        }
        uint8_t *page = (uint8_t *)(hhdm_request.response->offset + (uint64_t)phys);
        zero_page(page);
        vmm_map_page(pml4, virt, (uint64_t)phys, flags);
    }
    return 0;
}

static int map_user_stack(void *pml4, uint64_t *stack_top) {
    const uint64_t stack_bottom = PE_USER_STACK_TOP - (PE_USER_STACK_PAGES * PE_PAGE_SIZE);
    if (map_range(pml4, stack_bottom, PE_USER_STACK_PAGES * PE_PAGE_SIZE, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_USER) != 0) {
        return -1;
    }
    *stack_top = PE_USER_STACK_TOP - 8ull;
    return 0;
}

static void build_thunk(uint8_t *out, uint64_t id) {
    size_t i = 0;
    const uint64_t syscall_number = VEX_PE_SYSCALL_CALL;

    out[i++] = 0x4c; out[i++] = 0x8b; out[i++] = 0x5c; out[i++] = 0x24; out[i++] = 0x28; /* mov r11, [rsp+40] */
    out[i++] = 0x48; out[i++] = 0x89; out[i++] = 0xce; /* mov rsi, rcx */
    out[i++] = 0x4d; out[i++] = 0x89; out[i++] = 0xc2; /* mov r10, r8 */
    out[i++] = 0x4d; out[i++] = 0x89; out[i++] = 0xc8; /* mov r8, r9 */
    out[i++] = 0x4d; out[i++] = 0x89; out[i++] = 0xd9; /* mov r9, r11 */
    out[i++] = 0x48; out[i++] = 0xbf; write_u64(out + i, id); i += 8u; /* mov rdi, id */
    out[i++] = 0x48; out[i++] = 0xb8; write_u64(out + i, syscall_number); i += 8u; /* mov rax, syscall */
    out[i++] = 0x0f; out[i++] = 0x05; /* syscall */
    out[i++] = 0xc3; /* ret */
    while (i < VEX_PE_THUNK_SIZE) {
        out[i++] = 0x90;
    }
}

static int install_thunk_id(void *pml4, uint64_t id) {
    uint8_t thunk[VEX_PE_THUNK_SIZE];

    build_thunk(thunk, id);
    return write_mapped(pml4, VEX_PE_THUNK_BASE + id * VEX_PE_THUNK_SIZE, thunk, sizeof(thunk));
}

static int install_thunks(void *pml4) {
    if (map_range(pml4, VEX_PE_THUNK_BASE, VEX_PE_THUNK_SIZE * 32u, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_USER) != 0) {
        return -1;
    }

    for (uint64_t id = VEX_PE_GET_STD_HANDLE; id <= VEX_PE_GET_PROC; ++id) {
        if (install_thunk_id(pml4, id) != 0) {
            return -1;
        }
    }
    return 0;
}

static uint64_t resolve_import(const char *name) {
    if (string_equals_casefold(name, "GetStdHandle")) {
        return VEX_PE_THUNK_BASE + VEX_PE_GET_STD_HANDLE * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "WriteFile")) {
        return VEX_PE_THUNK_BASE + VEX_PE_WRITE_FILE * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "ExitProcess")) {
        return VEX_PE_THUNK_BASE + VEX_PE_EXIT_PROCESS * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "ReadFile")) {
        return VEX_PE_THUNK_BASE + VEX_PE_READ_FILE * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "GetLastError")) {
        return VEX_PE_THUNK_BASE + VEX_PE_GET_LAST_ERROR * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "SetLastError")) {
        return VEX_PE_THUNK_BASE + VEX_PE_SET_LAST_ERROR * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "CreateFileA") || string_equals_casefold(name, "CreateFileW")) {
        return VEX_PE_THUNK_BASE + VEX_PE_CREATE_FILE * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "CloseHandle")) {
        return VEX_PE_THUNK_BASE + VEX_PE_CLOSE_HANDLE * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "VirtualAlloc")) {
        return VEX_PE_THUNK_BASE + VEX_PE_VIRTUAL_ALLOC * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "VirtualFree")) {
        return VEX_PE_THUNK_BASE + VEX_PE_VIRTUAL_FREE * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "CreateProcessA") || string_equals_casefold(name, "CreateProcessW")) {
        return VEX_PE_THUNK_BASE + VEX_PE_CREATE_PROCESS * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "WaitForSingleObject")) {
        return VEX_PE_THUNK_BASE + VEX_PE_WAIT_SINGLE * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "CreateThread")) {
        return VEX_PE_THUNK_BASE + VEX_PE_CREATE_THREAD * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "HeapAlloc")) {
        return VEX_PE_THUNK_BASE + VEX_PE_HEAP_ALLOC * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "HeapFree")) {
        return VEX_PE_THUNK_BASE + VEX_PE_HEAP_FREE * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "GetModuleHandleA") || string_equals_casefold(name, "GetModuleHandleW")) {
        return VEX_PE_THUNK_BASE + VEX_PE_GET_MODULE * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "LoadLibraryA") || string_equals_casefold(name, "LoadLibraryW")) {
        return VEX_PE_THUNK_BASE + VEX_PE_LOAD_LIBRARY * VEX_PE_THUNK_SIZE;
    }
    if (string_equals_casefold(name, "GetProcAddress")) {
        return VEX_PE_THUNK_BASE + VEX_PE_GET_PROC * VEX_PE_THUNK_SIZE;
    }
    return 0;
}

static const char *rva_string(const uint8_t *file, size_t file_size, uint32_t rva, size_t section_table, uint16_t section_count) {
    for (uint16_t i = 0; i < section_count; ++i) {
        const size_t section = section_table + (size_t)i * 40u;
        const uint32_t virtual_size = read_u32(file, file_size, section + 8u);
        const uint32_t virtual_address = read_u32(file, file_size, section + 12u);
        const uint32_t raw_size = read_u32(file, file_size, section + 16u);
        const uint32_t raw_ptr = read_u32(file, file_size, section + 20u);
        const uint32_t span = virtual_size > raw_size ? virtual_size : raw_size;
        if (rva >= virtual_address && rva < virtual_address + span) {
            const uint32_t offset = raw_ptr + (rva - virtual_address);
            if (offset < file_size) {
                return (const char *)(file + offset);
            }
        }
    }
    if (rva < file_size) {
        return (const char *)(file + rva);
    }
    return NULL;
}

static int patch_imports(void *pml4, const uint8_t *file, size_t file_size, uint64_t image_base, uint32_t import_rva, size_t section_table, uint16_t section_count) {
    for (uint32_t descriptor_rva = import_rva; descriptor_rva != 0u; descriptor_rva += 20u) {
        const char *descriptor = rva_string(file, file_size, descriptor_rva, section_table, section_count);
        if (descriptor == NULL) {
            return -1;
        }

        const uint8_t *desc = (const uint8_t *)descriptor;
        const uint32_t original_first_thunk = read_u32(desc, 20u, 0u);
        const uint32_t name_rva = read_u32(desc, 20u, 12u);
        const uint32_t first_thunk = read_u32(desc, 20u, 16u);
        uint32_t lookup_rva = original_first_thunk != 0u ? original_first_thunk : first_thunk;

        if (original_first_thunk == 0u && name_rva == 0u && first_thunk == 0u) {
            return 0;
        }

        for (uint32_t index = 0u;; ++index) {
            const char *lookup_ptr = rva_string(file, file_size, lookup_rva + index * 8u, section_table, section_count);
            if (lookup_ptr == NULL) {
                return -1;
            }
            const uint64_t lookup = read_u64((const uint8_t *)lookup_ptr, 8u, 0u);
            if (lookup == 0u) {
                break;
            }
            if ((lookup >> 63u) != 0u) {
                return -1;
            }

            const char *import_name = rva_string(file, file_size, (uint32_t)lookup + 2u, section_table, section_count);
            const uint64_t target = import_name == NULL ? 0u : resolve_import(import_name);
            if (target == 0u) {
                return -1;
            }

            uint8_t encoded[8];
            write_u64(encoded, target);
            if (write_mapped(pml4, image_base + first_thunk + index * 8u, encoded, sizeof(encoded)) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

static int load_pe_process(const char *path, int wait_for_exit) {
    void *data = NULL;
    size_t size = 0;
    int result = -1;

    if (path == NULL || fs_read(path, &data, &size) != 0 || data == NULL || size < 0x100u) {
        return -1;
    }

    const uint8_t *file = data;
    const uint32_t pe_offset = read_u32(file, size, 0x3cu);
    if (pe_offset + 24u > size || file[0] != 'M' || file[1] != 'Z' ||
        file[pe_offset] != 'P' || file[pe_offset + 1u] != 'E') {
        return -1;
    }

    const size_t coff = pe_offset + 4u;
    const uint16_t machine = read_u16(file, size, coff);
    const uint16_t section_count = read_u16(file, size, coff + 2u);
    const uint16_t optional_size = read_u16(file, size, coff + 16u);
    const size_t optional = coff + 20u;
    const size_t section_table = optional + optional_size;
    if (machine != PE_MACHINE_X86_64 || optional + optional_size > size ||
        read_u16(file, size, optional) != PE_OPTIONAL_MAGIC_PE32_PLUS ||
        section_table + (size_t)section_count * 40u > size) {
        return -1;
    }

    const uint32_t entry_rva = read_u32(file, size, optional + 16u);
    const uint64_t image_base = read_u64(file, size, optional + 24u);
    const uint32_t size_of_image = read_u32(file, size, optional + 56u);
    const uint32_t size_of_headers = read_u32(file, size, optional + 60u);
    const uint32_t import_rva = read_u32(file, size, optional + 112u + 8u);

    void *pml4 = vmm_new_address_space();
    if (pml4 == NULL || map_range(pml4, image_base, size_of_image, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_USER) != 0) {
        goto fail;
    }

    if (write_mapped(pml4, image_base, file, size_of_headers < size ? size_of_headers : size) != 0) {
        goto fail;
    }

    for (uint16_t i = 0; i < section_count; ++i) {
        const size_t section = section_table + (size_t)i * 40u;
        const uint32_t virtual_address = read_u32(file, size, section + 12u);
        const uint32_t raw_size = read_u32(file, size, section + 16u);
        const uint32_t raw_ptr = read_u32(file, size, section + 20u);
        if (raw_size != 0u && raw_ptr <= size && raw_size <= size - raw_ptr) {
            if (write_mapped(pml4, image_base + virtual_address, file + raw_ptr, raw_size) != 0) {
                goto fail;
            }
        }
    }

    if (install_thunks(pml4) != 0 ||
        patch_imports(pml4, file, size, image_base, import_rva, section_table, section_count) != 0) {
        goto fail;
    }

    uint64_t user_stack_top = 0;
    if (map_user_stack(pml4, &user_stack_top) != 0) {
        goto fail;
    }

    const uint64_t cr3 = vmm_address_space_to_cr3(pml4);
    if (cr3 == 0u) {
        goto fail;
    }

    const uint32_t pid = process_create_user_abi((void (*)(void))(image_base + entry_rva), PE_LOAD_PRIORITY, cr3, user_stack_top, VEX_ABI_PE);
    if (pid == 0u) {
        goto fail;
    }

    if (wait_for_exit) {
        process_wait();
    }
    result = 0;
    pml4 = NULL;

fail:
    if (pml4 != NULL) {
        vmm_destroy_address_space(pml4);
    }
    return result;
}

int pe_load(const char *path) {
    return load_pe_process(path, 1);
}

int pe_spawn(const char *path) {
    return load_pe_process(path, 0);
}
