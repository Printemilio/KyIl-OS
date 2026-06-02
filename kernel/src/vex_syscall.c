#include <unos/vex_syscall.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/boot_requests.h>
#include <unos/boot_time.h>
#include <unos/debug_log.h>
#include <unos/dhcp.h>
#include <unos/elf.h>
#include <unos/gpu.h>
#include <unos/heap.h>
#include <unos/ipv4.h>
#include <unos/vex_legacy_compat.h>
#include <unos/mouse.h>
#include <unos/mrac.h>
#include <unos/netdev.h>
#include <unos/pmm.h>
#include <unos/process_memory.h>
#include <unos/scheduler.h>
#include <unos/vex_abi.h>
#include <unos/vmm.h>

#define VEX_USER_PAGE_SIZE 4096ull
#define VEX_JIT_ARENA_DEFAULT_QUOTA_BYTES (16ull * 1024ull * 1024ull)

typedef struct vex_jit_arena {
    uint32_t pid;
    uint32_t arena_id;
    uint32_t flags;
    uint32_t page_count;
    uint64_t rw_base;
    uint64_t rx_base;
    uint64_t size;
    uint64_t quota_bytes;
    uint64_t source_namespace_id;
    uint64_t *phys_pages;
    struct vex_jit_arena *next;
} vex_jit_arena_t;

static vex_jit_arena_t *vex_jit_arenas;
static uint32_t vex_next_jit_arena_id = 1u;

static uint64_t align_up_u64(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

static int vex_user_range_is_accessible(process_t *process, uint64_t address, size_t length) {
    void *pml4;
    uint64_t start;
    uint64_t end;
    int in_user_window;

    if (process == NULL || process->user_mode == 0) {
        return 0;
    }
    if (length == 0u) {
        return 1;
    }
    if (length - 1u > UINT64_MAX - address) {
        return 0;
    }

    in_user_window = address >= process->user_alloc_base && address < process->user_alloc_next;
    end = address + (uint64_t)length;
    if (end < address) {
        return 0;
    }
    if ((!in_user_window || end > process->user_alloc_next) &&
        !process_memory_region_contains(process, address, length)) {
        return 0;
    }

    pml4 = vmm_cr3_to_address_space(process->cr3);
    if (pml4 == NULL) {
        return 0;
    }

    start = address & ~(VEX_USER_PAGE_SIZE - 1ull);
    for (uint64_t page = start; page < end; page += VEX_USER_PAGE_SIZE) {
        if (vmm_get_physical(pml4, page) == 0u) {
            return 0;
        }
    }
    return 1;
}

static int vex_user_range_is_writable(process_t *process, uint64_t address, size_t length) {
    return vex_user_range_is_accessible(process, address, length);
}

static int vex_is_jit_requested(int prot) {
    return (prot & PROT_WRITE) != 0 && (prot & PROT_EXEC) != 0;
}

static uint64_t vex_map_user_memory(process_t *process, size_t length, uint64_t flags) {
    uint64_t virt;
    uint64_t page_count;

    (void)flags;

    if (process == NULL || process->user_mode == 0 || length == 0u) {
        return 0;
    }

    virt = align_up_u64(process->user_alloc_next, VEX_USER_PAGE_SIZE);
    page_count = align_up_u64(length, VEX_USER_PAGE_SIZE) / VEX_USER_PAGE_SIZE;
    process->user_alloc_next = virt + page_count * VEX_USER_PAGE_SIZE;
    return virt;
}

static vex_jit_arena_t *vex_find_jit_arena(uint32_t pid, uint32_t arena_id, uint64_t addr) {
    for (vex_jit_arena_t *arena = vex_jit_arenas; arena != NULL; arena = arena->next) {
        const uint64_t end = arena->rw_base + arena->size;
        const uint64_t rx_end = arena->rx_base + arena->size;

        if (arena->pid != pid) {
            continue;
        }
        if (arena_id != 0u && arena->arena_id == arena_id) {
            return arena;
        }
        if (addr != 0u && ((addr >= arena->rw_base && addr < end) || (addr >= arena->rx_base && addr < rx_end))) {
            return arena;
        }
    }

    return NULL;
}

static void vex_unlink_jit_arena(vex_jit_arena_t *arena) {
    vex_jit_arena_t **link = &vex_jit_arenas;

    while (*link != NULL) {
        if (*link == arena) {
            *link = arena->next;
            return;
        }
        link = &(*link)->next;
    }
}

static int vex_destroy_jit_arena_internal(process_t *process, vex_jit_arena_t *arena) {
    void *pml4;

    if (process == NULL || arena == NULL || process->cr3 == 0u) {
        return -1;
    }

    pml4 = vmm_cr3_to_address_space(process->cr3);
    if (pml4 == NULL) {
        return -1;
    }

    for (uint32_t index = 0u; index < arena->page_count; ++index) {
        const uint64_t rw_page = arena->rw_base + ((uint64_t)index * VEX_USER_PAGE_SIZE);
        const uint64_t rx_page = arena->rx_base + ((uint64_t)index * VEX_USER_PAGE_SIZE);

        vmm_unmap_page(pml4, rw_page);
        vmm_unmap_page(pml4, rx_page);
        if (arena->phys_pages[index] != 0u) {
            pmm_free_page((void *)arena->phys_pages[index]);
        }
    }

    if (arena->source_namespace_id != 0u) {
        (void)process_memory_namespace_destroy(process->pid, arena->source_namespace_id, 0u);
    }

    vex_unlink_jit_arena(arena);
    kfree(arena->phys_pages);
    kfree(arena);
    return 0;
}

static uint64_t vex_sys_jit_arena_create(uint64_t arg1) {
    vex_jit_arena_create_req_t *req = (vex_jit_arena_create_req_t *)arg1;
    process_t *process = scheduler_current_process();
    vex_jit_arena_t *arena;
    void *pml4;
    uint64_t current_namespace_id = 0u;
    uint64_t snapshot_namespace_id = 0u;
    uint64_t rw_base;
    uint64_t rx_base;
    uint64_t quota_limit;
    size_t aligned_size;
    uint32_t page_count;

    if (req == NULL || process == NULL || process->user_mode == 0 || req->size == 0u) {
        return (uint64_t)-1;
    }

    aligned_size = (size_t)align_up_u64(req->size, VEX_USER_PAGE_SIZE);
    quota_limit = req->quota_bytes == 0u ? VEX_JIT_ARENA_DEFAULT_QUOTA_BYTES : req->quota_bytes;
    if ((req->flags & (VEX_JIT_ARENA_FLAG_DISABLE_QUOTA | VEX_JIT_ARENA_FLAG_FULL_OPEN)) == 0u &&
        aligned_size > quota_limit) {
        return (uint64_t)-1;
    }

    pml4 = vmm_cr3_to_address_space(process->cr3);
    if (pml4 == NULL) {
        return (uint64_t)-1;
    }

    current_namespace_id = process_memory_current_namespace(process->pid);
    if (current_namespace_id != 0u &&
        process_memory_snapshot_namespace(process->pid, current_namespace_id, &snapshot_namespace_id) != 0) {
        return (uint64_t)-1;
    }

    rw_base = req->preferred_rw_addr != NULL
        ? (uint64_t)(uintptr_t)req->preferred_rw_addr
        : vex_map_user_memory(process, aligned_size, 0u);
    rx_base = req->preferred_rx_addr != NULL
        ? (uint64_t)(uintptr_t)req->preferred_rx_addr
        : vex_map_user_memory(process, aligned_size, 0u);
    if (rw_base == 0u || rx_base == 0u) {
        if (snapshot_namespace_id != 0u) {
            (void)process_memory_namespace_destroy(process->pid, snapshot_namespace_id, 0u);
        }
        return (uint64_t)-1;
    }

    arena = kmalloc(sizeof(*arena));
    if (arena == NULL) {
        if (snapshot_namespace_id != 0u) {
            (void)process_memory_namespace_destroy(process->pid, snapshot_namespace_id, 0u);
        }
        return (uint64_t)-1;
    }

    page_count = (uint32_t)(aligned_size / VEX_USER_PAGE_SIZE);
    arena->phys_pages = kmalloc((size_t)page_count * sizeof(*arena->phys_pages));
    if (arena->phys_pages == NULL) {
        if (snapshot_namespace_id != 0u) {
            (void)process_memory_namespace_destroy(process->pid, snapshot_namespace_id, 0u);
        }
        kfree(arena);
        return (uint64_t)-1;
    }
    memset(arena->phys_pages, 0, (size_t)page_count * sizeof(*arena->phys_pages));

    arena->pid = process->pid;
    arena->arena_id = vex_next_jit_arena_id++;
    arena->flags = req->flags;
    arena->page_count = page_count;
    arena->rw_base = rw_base;
    arena->rx_base = rx_base;
    arena->size = aligned_size;
    arena->quota_bytes = quota_limit;
    arena->source_namespace_id = snapshot_namespace_id;
    arena->next = NULL;

    for (uint32_t index = 0u; index < page_count; ++index) {
        void *phys = pmm_alloc_page();
        uint64_t rw_flags = VMM_PAGE_PRESENT | VMM_PAGE_USER | VMM_PAGE_WRITABLE;
        uint64_t rx_flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;
        uint8_t *dst;

        if (phys == NULL) {
            (void)vex_destroy_jit_arena_internal(process, arena);
            return (uint64_t)-1;
        }

        arena->phys_pages[index] = (uint64_t)phys;
        dst = (uint8_t *)(hhdm_request.response->offset + (uint64_t)phys);
        memset(dst, 0, VEX_USER_PAGE_SIZE);

        if ((req->flags & (VEX_JIT_ARENA_FLAG_DISABLE_LOCKS | VEX_JIT_ARENA_FLAG_FULL_OPEN)) != 0u) {
            rx_flags |= VMM_PAGE_WRITABLE;
        }

        vmm_map_page(pml4, rw_base + ((uint64_t)index * VEX_USER_PAGE_SIZE), (uint64_t)phys, rw_flags);
        vmm_map_page(pml4, rx_base + ((uint64_t)index * VEX_USER_PAGE_SIZE), (uint64_t)phys, rx_flags);
    }

    arena->next = vex_jit_arenas;
    vex_jit_arenas = arena;

    req->arena_id = arena->arena_id;
    req->rw_base = (void *)(uintptr_t)arena->rw_base;
    req->rx_base = (void *)(uintptr_t)arena->rx_base;
    req->source_namespace_id = arena->source_namespace_id;
    return 0;
}

static uint64_t vex_sys_jit_arena_destroy(uint64_t arg1) {
    vex_jit_arena_destroy_req_t *req = (vex_jit_arena_destroy_req_t *)arg1;
    process_t *process = scheduler_current_process();
    vex_jit_arena_t *arena;

    if (req == NULL || process == NULL) {
        return (uint64_t)-1;
    }

    arena = vex_find_jit_arena(process->pid, req->arena_id, (uint64_t)(uintptr_t)req->addr);
    if (arena == NULL) {
        return (uint64_t)-1;
    }

    return (uint64_t)vex_destroy_jit_arena_internal(process, arena);
}

void vex_jit_reap_pid(uint32_t pid, uint64_t cr3) {
    process_t process_stub;
    vex_jit_arena_t *arena = vex_jit_arenas;
    vex_jit_arena_t *next;

    memset(&process_stub, 0, sizeof(process_stub));
    process_stub.pid = pid;
    process_stub.cr3 = cr3;

    while (arena != NULL) {
        next = arena->next;
        if (arena->pid == pid) {
            (void)vex_destroy_jit_arena_internal(&process_stub, arena);
        }
        arena = next;
    }
}

static uint64_t vex_sys_open(uint64_t arg1, uint64_t arg2) {
    return (uint64_t)open((char const *)arg1, (int)arg2);
}

static uint64_t vex_sys_fd_io(uint64_t arg1, uint64_t which) {
    vex_fd_io_req_t *req = (vex_fd_io_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }

    if (which == VEX_SYS_READFD) {
        return (uint64_t)read(req->fd, req->buffer, req->count);
    }

    return (uint64_t)write(req->fd, req->buffer, req->count);
}

static uint64_t vex_sys_close(uint64_t arg1) {
    return (uint64_t)close((int)arg1);
}

static uint64_t vex_sys_pread(uint64_t arg1) {
    vex_rw_offset_req_t *req = (vex_rw_offset_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    return (uint64_t)pread(req->fd, req->buffer, req->count, req->offset);
}

static uint64_t vex_sys_pwrite(uint64_t arg1) {
    vex_rw_offset_req_t *req = (vex_rw_offset_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    return (uint64_t)pwrite(req->fd, req->buffer, req->count, req->offset);
}

static uint64_t vex_sys_lseek(uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    return (uint64_t)lseek((int)arg1, (off_t)arg2, (int)arg3);
}

static uint64_t vex_sys_fsync(uint64_t arg1) {
    return (uint64_t)fsync((int)arg1);
}

static uint64_t vex_sys_stat_like(uint64_t arg1, uint64_t which) {
    vex_path_stat_req_t *req = (vex_path_stat_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }

    if (which == VEX_SYS_STAT) {
        return (uint64_t)stat(req->path, req->st);
    }
    if (which == VEX_SYS_LSTAT) {
        return (uint64_t)lstat(req->path, req->st);
    }
    return (uint64_t)fstat((int)(uintptr_t)req->path, req->st);
}

static uint64_t vex_sys_access(uint64_t arg1, uint64_t arg2) {
    return (uint64_t)access((char const *)arg1, (int)arg2);
}

static uint64_t vex_sys_unlink(uint64_t arg1) {
    return (uint64_t)unlink((char const *)arg1);
}

static uint64_t vex_sys_rename(uint64_t arg1) {
    vex_rename_req_t *req = (vex_rename_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    return (uint64_t)rename(req->old_path, req->new_path);
}

static uint64_t vex_sys_mkdir(uint64_t arg1) {
    vex_mkdir_req_t *req = (vex_mkdir_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    return (uint64_t)mkdir(req->path, req->mode);
}

static uint64_t vex_sys_getcwd(uint64_t arg1) {
    vex_getcwd_req_t *req = (vex_getcwd_req_t *)arg1;
    if (req == NULL) {
        return 0;
    }
    return (uint64_t)(uintptr_t)getcwd(req->buffer, req->size);
}

static uint64_t vex_sys_ioctl(uint64_t arg1) {
    vex_ioctl_req_t *req = (vex_ioctl_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    return (uint64_t)ioctl(req->fd, req->request, req->arg);
}

static uint64_t vex_sys_pipe(uint64_t arg1) {
    vex_pipe_req_t *req = (vex_pipe_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    return (uint64_t)pipe(req->pipefd);
}

static uint64_t vex_sys_mmap(uint64_t arg1) {
    vex_mmap_req_t *req = (vex_mmap_req_t *)arg1;
    process_t *process;
    uint64_t addr;
    uint8_t *buffer;
    size_t aligned_length;
    vex_jit_arena_create_req_t jit_req;

    if (req == NULL) {
        return (uint64_t)MAP_FAILED;
    }

    process = scheduler_current_process();
    if (process == NULL || process->user_mode == 0) {
        return (uint64_t)MAP_FAILED;
    }

    if (vex_is_jit_requested(req->prot)) {
        jit_req.preferred_rw_addr = req->addr;
        jit_req.preferred_rx_addr = NULL;
        jit_req.size = req->length;
        jit_req.quota_bytes = 0u;
        jit_req.flags = 0u;
        jit_req.arena_id = 0u;
        jit_req.rw_base = NULL;
        jit_req.rx_base = NULL;
        jit_req.source_namespace_id = 0u;
        if (vex_sys_jit_arena_create((uint64_t)&jit_req) != 0) {
            return (uint64_t)MAP_FAILED;
        }
        return (uint64_t)(uintptr_t)jit_req.rw_base;
    }

    aligned_length = (size_t)align_up_u64(req->length, VEX_USER_PAGE_SIZE);
    addr = req->addr != NULL ? (uint64_t)(uintptr_t)req->addr : vex_map_user_memory(process, aligned_length, 0u);
    if (addr == 0) {
        return (uint64_t)MAP_FAILED;
    }

    buffer = kmalloc(aligned_length);
    if (buffer == NULL) {
        return (uint64_t)MAP_FAILED;
    }
    memset(buffer, 0, aligned_length);

    if ((req->flags & MAP_ANONYMOUS) == 0 && req->fd >= 0) {
        if (pread(req->fd, buffer, req->length, req->offset) < 0) {
            kfree(buffer);
            return (uint64_t)MAP_FAILED;
        }
    }

    if (process_memory_register_region(process->pid, addr, aligned_length, req->prot, req->flags, 0u, buffer) != 0 ||
        process_memory_materialize_region(process->pid, process->cr3, addr) != 0) {
        kfree(buffer);
        return (uint64_t)MAP_FAILED;
    }

    kfree(buffer);

    return addr;
}

static uint64_t vex_sys_memory_op(uint64_t arg1, uint64_t which) {
    vex_memory_req_t *req = (vex_memory_req_t *)arg1;
    process_t *process;
    void *pml4;
    uint64_t flags;
    vex_jit_arena_t *jit_arena;

    if (req == NULL) {
        return (uint64_t)-1;
    }

    process = scheduler_current_process();
    if (process == NULL || process->user_mode == 0) {
        return (uint64_t)-1;
    }

    pml4 = vmm_cr3_to_address_space(process->cr3);
    if (pml4 == NULL) {
        return (uint64_t)-1;
    }

    jit_arena = vex_find_jit_arena(process->pid, 0u, (uint64_t)(uintptr_t)req->addr);
    if (jit_arena != NULL) {
        if (which == VEX_SYS_MUNMAP) {
            return (uint64_t)vex_destroy_jit_arena_internal(process, jit_arena);
        }
        if (which == VEX_SYS_MPROTECT) {
            for (uint32_t index = 0u; index < jit_arena->page_count; ++index) {
                uint64_t rw_flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;
                uint64_t rx_flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;

                if ((req->prot_or_advice & PROT_WRITE) != 0) {
                    rw_flags |= VMM_PAGE_WRITABLE;
                }
                if ((jit_arena->flags & (VEX_JIT_ARENA_FLAG_DISABLE_LOCKS | VEX_JIT_ARENA_FLAG_FULL_OPEN)) != 0u) {
                    rx_flags |= VMM_PAGE_WRITABLE;
                }

                if (vmm_set_page_flags(pml4, jit_arena->rw_base + ((uint64_t)index * VEX_USER_PAGE_SIZE), rw_flags) != 0 ||
                    vmm_set_page_flags(pml4, jit_arena->rx_base + ((uint64_t)index * VEX_USER_PAGE_SIZE), rx_flags) != 0) {
                    return (uint64_t)-1;
                }
            }
            return 0;
        }
    }

    if (which == VEX_SYS_MUNMAP) {
        return (uint64_t)process_memory_remove_region(process->pid, process->cr3, (uint64_t)(uintptr_t)req->addr, req->length);
    }
    if (which == VEX_SYS_MPROTECT) {
        const uint64_t start = (uint64_t)(uintptr_t)req->addr & ~(VEX_USER_PAGE_SIZE - 1ull);
        const uint64_t end = align_up_u64((uint64_t)(uintptr_t)req->addr + req->length, VEX_USER_PAGE_SIZE);

        flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;
        if ((req->prot_or_advice & PROT_WRITE) != 0) {
            flags |= VMM_PAGE_WRITABLE;
        }

        for (uint64_t page = start; page < end; page += VEX_USER_PAGE_SIZE) {
            if (vmm_set_page_flags(pml4, page, flags) != 0) {
                return (uint64_t)-1;
            }
        }
        if (process_memory_protect_region(process->pid, (uint64_t)(uintptr_t)req->addr, req->length, req->prot_or_advice) != 0) {
            return (uint64_t)-1;
        }
        return 0;
    }
    return 0;
}

static uint64_t vex_sys_heap_alloc(uint64_t arg1) {
    uint64_t addr;
    vex_heap_req_t record;
    process_t *process = scheduler_current_process();
    size_t size = (size_t)arg1;
    size_t aligned_size;
    uint8_t *buffer;

    if (process == NULL || size == 0u) {
        return 0;
    }

    aligned_size = (size_t)align_up_u64(size, VEX_USER_PAGE_SIZE);
    addr = vex_map_user_memory(process, aligned_size, 0u);
    if (addr == 0) {
        return 0;
    }

    buffer = kmalloc(aligned_size);
    if (buffer == NULL) {
        return 0;
    }
    memset(buffer, 0, aligned_size);
    if (process_memory_register_region(process->pid, addr, aligned_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, PROCESS_MEMORY_FLAG_HEAP, buffer) != 0 ||
        process_memory_materialize_region(process->pid, process->cr3, addr) != 0) {
        kfree(buffer);
        return 0;
    }
    kfree(buffer);

    record.ptr = (void *)(uintptr_t)addr;
    record.size = size;
    (void)MRAC_store_with_hash(process->pid, addr, &record, sizeof(record));
    return addr;
}

static uint64_t vex_sys_heap_free(uint64_t arg1) {
    vex_heap_req_t *req = (vex_heap_req_t *)arg1;
    process_t *process = scheduler_current_process();

    if (process == NULL || req == NULL || req->ptr == NULL) {
        return (uint64_t)-1;
    }

    MRAC_remove(process->pid, (uint64_t)(uintptr_t)req->ptr);
    return (uint64_t)process_memory_remove_region(process->pid, process->cr3, (uint64_t)(uintptr_t)req->ptr, req->size);
}

static uint64_t vex_sys_heap_realloc(uint64_t arg1) {
    vex_heap_req_t *req = (vex_heap_req_t *)arg1;
    process_t *process = scheduler_current_process();
    vex_heap_req_t *old_record;
    vex_heap_req_t new_record;
    process_memory_region_t old_region;
    void *old_backing;
    uint8_t *new_buffer;
    uint64_t addr;
    size_t aligned_size;
    size_t copy_size;

    if (process == NULL || req == NULL) {
        return 0;
    }
    if (req->size == 0u) {
        (void)vex_sys_heap_free(arg1);
        return 0;
    }
    if (req->ptr == NULL) {
        return vex_sys_heap_alloc(req->size);
    }

    if (process_memory_snapshot_region(process->pid, process->cr3, (uint64_t)(uintptr_t)req->ptr) != 0) {
        return 0;
    }

    {
        const uint64_t region_key = process_memory_region_key(process->pid, (uint64_t)(uintptr_t)req->ptr);
        process_memory_region_t *region = MRAC_lookup(process->pid, region_key);
        if (region == NULL) {
            return 0;
        }
        memcpy(&old_region, region, sizeof(old_region));
        MRAC_release(process->pid, region_key);
    }

    if (old_region.magic != PROCESS_MEMORY_REGION_MAGIC) {
        return 0;
    }

    old_record = MRAC_lookup(process->pid, (uint64_t)(uintptr_t)req->ptr);
    old_backing = MRAC_lookup(process->pid, old_region.backing_key);
    aligned_size = (size_t)align_up_u64(req->size, VEX_USER_PAGE_SIZE);
    addr = vex_map_user_memory(process, aligned_size, 0u);
    if (addr == 0) {
        if (old_backing != NULL) {
            MRAC_release(process->pid, old_region.backing_key);
        }
        if (old_record != NULL) {
            MRAC_release(process->pid, (uint64_t)(uintptr_t)req->ptr);
        }
        return 0;
    }

    new_buffer = kmalloc(aligned_size);
    if (new_buffer == NULL) {
        if (old_backing != NULL) {
            MRAC_release(process->pid, old_region.backing_key);
        }
        if (old_record != NULL) {
            MRAC_release(process->pid, (uint64_t)(uintptr_t)req->ptr);
        }
        return 0;
    }
    memset(new_buffer, 0, aligned_size);

    copy_size = req->size;
    if (old_record != NULL && old_record->size < copy_size) {
        copy_size = old_record->size;
    }
    if (old_backing != NULL && copy_size != 0u) {
        memcpy(new_buffer, old_backing, copy_size);
    }

    if (old_backing != NULL) {
        MRAC_release(process->pid, old_region.backing_key);
    }
    if (old_record != NULL) {
        MRAC_release(process->pid, (uint64_t)(uintptr_t)req->ptr);
    }

    if (process_memory_register_region(process->pid, addr, aligned_size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, PROCESS_MEMORY_FLAG_HEAP, new_buffer) != 0 ||
        process_memory_materialize_region(process->pid, process->cr3, addr) != 0) {
        kfree(new_buffer);
        return 0;
    }
    kfree(new_buffer);

    MRAC_remove(process->pid, (uint64_t)(uintptr_t)req->ptr);
    (void)process_memory_remove_region(process->pid, process->cr3, (uint64_t)(uintptr_t)req->ptr, 0u);

    new_record.ptr = (void *)(uintptr_t)addr;
    new_record.size = req->size;
    (void)MRAC_store_with_hash(process->pid, addr, &new_record, sizeof(new_record));
    return addr;
}

static uint64_t vex_sys_mrac_put(uint64_t arg1) {
    vex_mrac_put_req_t *req = (vex_mrac_put_req_t *)arg1;
    process_t *process = scheduler_current_process();
    void *stored;

    if (process == NULL || req == NULL || req->data == NULL || req->size == 0u) {
        return (uint64_t)-1;
    }

    stored = MRAC_store_with_hash(process->pid, req->key_hash, (void *)req->data, req->size);
    if (stored == NULL) {
        return (uint64_t)-1;
    }

    req->version_out = MRAC_current_version(process->pid, req->key_hash);
    return 0;
}

static uint64_t vex_sys_mrac_get(uint64_t arg1) {
    vex_mrac_get_req_t *req = (vex_mrac_get_req_t *)arg1;
    process_t *process = scheduler_current_process();
    void *data;
    uint32_t version;
    size_t size;

    if (process == NULL || req == NULL) {
        return (uint64_t)-1;
    }

    version = req->version == VEX_MRAC_VERSION_CURRENT
        ? MRAC_current_version(process->pid, req->key_hash)
        : req->version;
    if (version == 0u) {
        return (uint64_t)-1;
    }

    data = MRAC_lookup_version(process->pid, req->key_hash, version);
    if (data == NULL) {
        return (uint64_t)-1;
    }

    req->resolved_version = version;
    req->size_out = 0u;
    size = 0u;
    {
        MRAC_version_info_t info;
        if (MRAC_version_info_at(process->pid, req->key_hash, 0u, &info) == 0 && info.version == version) {
            size = info.size;
        } else {
            const size_t count = MRAC_version_count(process->pid, req->key_hash);
            for (size_t i = 0; i < count; ++i) {
                if (MRAC_version_info_at(process->pid, req->key_hash, i, &info) == 0 && info.version == version) {
                    size = info.size;
                    break;
                }
            }
        }
    }

    if (size == 0u) {
        MRAC_release_version(process->pid, req->key_hash, version);
        return (uint64_t)-1;
    }

    req->size_out = size;
    if (req->buffer == NULL || req->buffer_size < size) {
        MRAC_release_version(process->pid, req->key_hash, version);
        return (uint64_t)-1;
    }

    memcpy(req->buffer, data, size);
    MRAC_release_version(process->pid, req->key_hash, version);
    return 0;
}

static uint64_t vex_sys_mrac_remove(uint64_t arg1) {
    vex_mrac_remove_req_t *req = (vex_mrac_remove_req_t *)arg1;
    process_t *process = scheduler_current_process();

    if (process == NULL || req == NULL) {
        return (uint64_t)-1;
    }

    if ((req->flags & VEX_MRAC_REMOVE_ALL) != 0u) {
        MRAC_remove(process->pid, req->key_hash);
        return 0;
    }

    if ((req->flags & VEX_MRAC_REMOVE_CURRENT) != 0u || req->version == VEX_MRAC_VERSION_CURRENT) {
        const uint32_t version = MRAC_current_version(process->pid, req->key_hash);
        return version != 0u && MRAC_remove_version(process->pid, req->key_hash, version) == 0 ? 0 : (uint64_t)-1;
    }

    return MRAC_remove_version(process->pid, req->key_hash, req->version) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t vex_sys_mrac_version_info(uint64_t arg1) {
    vex_mrac_version_info_req_t *req = (vex_mrac_version_info_req_t *)arg1;
    process_t *process = scheduler_current_process();
    MRAC_version_info_t info;

    if (process == NULL || req == NULL || req->info == NULL) {
        return (uint64_t)-1;
    }

    if (MRAC_version_info_at(process->pid, req->key_hash, req->index, &info) != 0) {
        return (uint64_t)-1;
    }

    req->info->version = info.version;
    req->info->size = info.size;
    req->info->is_current = info.is_current;
    return 0;
}

static uint64_t vex_sys_poll(uint64_t arg1) {
    vex_poll_req_t *req = (vex_poll_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    return (uint64_t)poll(req->fds, req->count, req->timeout);
}

static uint64_t vex_sys_time(uint64_t arg1, uint64_t which) {
    if (which == VEX_SYS_GETTIMEOFDAY) {
        vex_gettimeofday_req_t *req = (vex_gettimeofday_req_t *)arg1;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        return (uint64_t)gettimeofday(req->tv, req->tz);
    }

    if (which == VEX_SYS_CLOCK_GETTIME) {
        vex_clock_gettime_req_t *req = (vex_clock_gettime_req_t *)arg1;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        return (uint64_t)clock_gettime(req->clock_id, req->ts);
    }

    {
        vex_nanosleep_req_t *req = (vex_nanosleep_req_t *)arg1;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        return (uint64_t)nanosleep(req->requested, req->remaining);
    }
}

static uint64_t vex_sys_path_buffer(uint64_t arg1, uint64_t which) {
    if (which == VEX_SYS_READLINK) {
        vex_readlink_req_t *req = (vex_readlink_req_t *)arg1;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        return (uint64_t)readlink(req->path, req->buffer, req->size);
    }

    if (which == VEX_SYS_SYMLINK) {
        vex_symlink_req_t *req = (vex_symlink_req_t *)arg1;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        return (uint64_t)symlink(req->target, req->link_path);
    }

    {
        vex_realpath_req_t *req = (vex_realpath_req_t *)arg1;
        if (req == NULL) {
            return 0;
        }
        return (uint64_t)(uintptr_t)realpath(req->path, req->resolved_path);
    }
}

static uint64_t vex_sys_env(uint64_t arg1, uint64_t arg2, uint64_t which) {
    (void)arg2;

    if (which == VEX_SYS_GETENV) {
        return (uint64_t)(uintptr_t)getenv((char const *)arg1);
    }
    if (which == VEX_SYS_SETENV) {
        vex_setenv_req_t *req = (vex_setenv_req_t *)arg1;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        return (uint64_t)setenv(req->name, req->value, req->overwrite);
    }
    if (which == VEX_SYS_UNSETENV) {
        return (uint64_t)unsetenv((char const *)arg1);
    }
    if (which == VEX_SYS_SYSCONF) {
        return (uint64_t)sysconf((int)arg1);
    }
    return (uint64_t)uname((void *)arg1);
}

static uint64_t vex_sys_getrandom(uint64_t arg1) {
    vex_getrandom_req_t *req = (vex_getrandom_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    return (uint64_t)getrandom(req->buffer, req->length, req->flags);
}

static uint64_t vex_sys_sockaddr(uint64_t arg1, uint64_t which) {
    vex_sockaddr_req_t *req = (vex_sockaddr_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }

    if (which == VEX_SYS_CONNECT) {
        return (uint64_t)connect(req->sockfd, req->addr, req->addrlen);
    }
    return (uint64_t)bind(req->sockfd, req->addr, req->addrlen);
}

static uint64_t vex_sys_send_recv(uint64_t arg1, uint64_t which) {
    vex_send_recv_req_t *req = (vex_send_recv_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }

    if (which == VEX_SYS_SEND) {
        return (uint64_t)send(req->sockfd, req->buffer, req->length, req->flags);
    }
    return (uint64_t)recv(req->sockfd, req->buffer, req->length, req->flags);
}

static uint64_t vex_sys_connect_hostname(uint64_t arg1) {
    vex_connect_hostname_req_t *req = (vex_connect_hostname_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    int result = ladybird_connect_hostname(req->sockfd, req->hostname, req->port);
    if (result < 0) {
        int err = *ladybird_errno_location();
        if (err <= 0) {
            err = ECONNREFUSED;
        }
        return (uint64_t)(-err);
    }
    return (uint64_t)result;
}

static uint64_t vex_sys_accept(uint64_t arg1) {
    vex_accept_req_t *req = (vex_accept_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    return (uint64_t)accept(req->sockfd, req->addr, req->addrlen);
}

static uint64_t vex_sys_sockopt(uint64_t arg1, uint64_t which) {
    vex_sockopt_req_t *req = (vex_sockopt_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }

    if (which == VEX_SYS_SETSOCKOPT) {
        uint32_t option_len = req->option_len == NULL ? 0u : *req->option_len;
        return (uint64_t)setsockopt(req->sockfd, req->level, req->option_name, req->option_value, option_len);
    }

    return (uint64_t)getsockopt(req->sockfd, req->level, req->option_name, req->option_value, req->option_len);
}

static uint64_t vex_sys_socketpair(uint64_t arg1) {
    vex_socketpair_req_t *req = (vex_socketpair_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    return (uint64_t)socketpair(req->domain, req->type, req->protocol, req->sv);
}

static uint64_t vex_sys_display_present(uint64_t arg1) {
    vex_display_present_req_t *req_user = (vex_display_present_req_t *)arg1;
    vex_display_present_req_t req;
    process_t *process = scheduler_current_process();
    ladybird_display_surface_t *surface;
    vex_rect_t dirty;
    uint32_t copy_x = 0u;
    uint32_t copy_y = 0u;
    uint32_t copy_w;
    uint32_t copy_h;
    size_t source_bytes;
    uint8_t use_partial = 0u;

    debug_log_puts("KRN: display_present enter\n");

    if (process == NULL) {
        debug_log_puts("KRN: dp err=no_process\n");
        return (uint64_t)-1;
    }
    if (req_user == NULL) {
        debug_log_puts("KRN: dp err=req_null\n");
        return (uint64_t)-1;
    }
    if (!vex_user_range_is_accessible(process, (uint64_t)(uintptr_t)req_user, sizeof(req))) {
        debug_log_puts("KRN: dp err=req_inaccessible\n");
        return (uint64_t)-1;
    }

    memcpy(&req, req_user, sizeof(req));

    if (req.pixels == NULL) {
        debug_log_puts("KRN: dp err=pixels_null\n");
        return (uint64_t)-1;
    }
    if (req.width == 0u || req.height == 0u || req.stride < req.width) {
        debug_log_puts("KRN: dp err=dimensions_invalid\n");
        return (uint64_t)-1;
    }
    if ((uint64_t)req.stride > SIZE_MAX / sizeof(uint32_t) || (uint64_t)req.height > SIZE_MAX / ((size_t)req.stride * sizeof(uint32_t))) {
        debug_log_puts("KRN: dp err=dimensions_overflow\n");
        return (uint64_t)-1;
    }

    source_bytes = (size_t)req.stride * (size_t)req.height * sizeof(uint32_t);
    if (!vex_user_range_is_accessible(process, (uint64_t)(uintptr_t)req.pixels, source_bytes)) {
        debug_log_puts("KRN: dp err=pixels_inaccessible\n");
        return (uint64_t)-1;
    }

    if (ladybird_display_open(req.width, req.height) != 0) {
        debug_log_puts("KRN: dp err=display_open_failed\n");
        return (uint64_t)-1;
    }

    surface = ladybird_display_surface();
    if (surface == NULL || surface->pixels == NULL) {
        debug_log_puts("KRN: dp err=surface_unavailable\n");
        return (uint64_t)-1;
    }

    copy_w = req.width;
    copy_h = req.height;

    if (req.dirty != NULL) {
        if (!vex_user_range_is_accessible(process, (uint64_t)(uintptr_t)req.dirty, sizeof(dirty))) {
            debug_log_puts("KRN: dp err=dirty_inaccessible\n");
        } else {
            memcpy(&dirty, req.dirty, sizeof(dirty));
        if (dirty.x >= 0 &&
            dirty.y >= 0 &&
            dirty.w > 0 &&
            dirty.h > 0 &&
            (uint32_t)dirty.x < req.width &&
            (uint32_t)dirty.y < req.height &&
            (uint32_t)dirty.w <= req.width - (uint32_t)dirty.x &&
            (uint32_t)dirty.h <= req.height - (uint32_t)dirty.y) {
            copy_x = (uint32_t)dirty.x;
            copy_y = (uint32_t)dirty.y;
            copy_w = (uint32_t)dirty.w;
            copy_h = (uint32_t)dirty.h;
            use_partial = 1u;
        } else {
            debug_log_puts("KRN: dp err=dirty_invalid\n");
        }
        }
    }

    if (use_partial) {
        for (uint32_t row = copy_y; row < copy_y + copy_h; ++row) {
            memcpy(
                surface->pixels + (size_t)row * surface->stride + copy_x,
                req.pixels + (size_t)row * req.stride + copy_x,
                (size_t)copy_w * sizeof(uint32_t)
            );
        }
        ladybird_display_set_pending_dirty(copy_x, copy_y, copy_w, copy_h);
        debug_log_puts("display_present: partial\n");
    } else {
        for (uint32_t y = 0; y < req.height; ++y) {
            memcpy(
                surface->pixels + (size_t)y * surface->stride,
                req.pixels + (size_t)y * req.stride,
                (size_t)req.width * sizeof(uint32_t)
            );
        }
        ladybird_display_clear_pending_dirty();
    }

    ladybird_display_present();
    debug_log_puts("KRN: display_present exit ok\n");
    return 0;
}

static uint64_t vex_sys_gpu_query_info(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_gpu_info_t info;

    if (!vex_user_range_is_writable(process, arg1, sizeof(info))) {
        return (uint64_t)-1;
    }
    if (gpu_query_info(&info) != 0) {
        return (uint64_t)-1;
    }

    memcpy((void *)(uintptr_t)arg1, &info, sizeof(info));
    return 0;
}

static uint64_t vex_sys_gpu_buffer_create(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_gpu_buffer_create_req_t *req_user = (vex_gpu_buffer_create_req_t *)arg1;
    vex_gpu_buffer_create_req_t req;

    if (process == NULL || !vex_user_range_is_writable(process, arg1, sizeof(req))) {
        return (uint64_t)-1;
    }

    memcpy(&req, req_user, sizeof(req));
    if (gpu_buffer_create(process->pid, &req) != 0) {
        return (uint64_t)-1;
    }

    memcpy(req_user, &req, sizeof(req));
    return 0;
}

static uint64_t vex_sys_gpu_buffer_destroy(uint64_t arg1) {
    process_t *process = scheduler_current_process();

    if (process == NULL) {
        return (uint64_t)-1;
    }

    return gpu_buffer_destroy(process->pid, (uint32_t)arg1) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t vex_sys_gpu_queue_submit(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_gpu_submit_req_t *req_user = (vex_gpu_submit_req_t *)arg1;
    vex_gpu_submit_req_t req;
    size_t commands_size;

    if (process == NULL || !vex_user_range_is_accessible(process, arg1, sizeof(req))) {
        return (uint64_t)-1;
    }

    memcpy(&req, req_user, sizeof(req));
    if (req.command_count == 0u || req.commands == NULL) {
        return (uint64_t)-1;
    }

    commands_size = (size_t)req.command_count * sizeof(vex_gpu_command_t);
    if (!vex_user_range_is_accessible(process, (uint64_t)(uintptr_t)req.commands, commands_size)) {
        return (uint64_t)-1;
    }

    for (uint32_t i = 0u; i < req.command_count; ++i) {
        const vex_gpu_command_t *command = &req.commands[i];
        if (command->op != VEX_GPU_CMD_BLIT || command->src_pixels == NULL) {
            continue;
        }
        if (command->src_stride < command->src_width) {
            return (uint64_t)-1;
        }
        if (command->src_height == 0u) {
            return (uint64_t)-1;
        }
        if (!vex_user_range_is_accessible(process,
                (uint64_t)(uintptr_t)command->src_pixels,
                (size_t)command->src_stride * (size_t)command->src_height * sizeof(uint32_t))) {
            return (uint64_t)-1;
        }
    }

    return gpu_queue_submit(process->pid, &req, req.commands) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t vex_sys_gpu_present(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_gpu_present_req_t *req_user = (vex_gpu_present_req_t *)arg1;
    vex_gpu_present_req_t req;

    if (process == NULL || !vex_user_range_is_accessible(process, arg1, sizeof(req))) {
        return (uint64_t)-1;
    }

    memcpy(&req, req_user, sizeof(req));
    return gpu_present_buffer(process->pid, &req) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t vex_sys_browser_command_read(uint64_t arg1) {
    vex_browser_command_req_t *req = (vex_browser_command_req_t *)arg1;
    if (req == NULL || req->buffer == NULL || req->size == 0u) {
        return (uint64_t)-1;
    }
    return (uint64_t)ladybird_browser_command_pop(req->buffer, req->size);
}

static uint64_t vex_sys_browser_results(uint64_t arg1) {
    vex_browser_result_req_t *req = (vex_browser_result_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    if (req->op == VEX_BROWSER_RESULTS_BEGIN) {
        ladybird_browser_results_begin(req->query);
        return 0;
    }
    if (req->op == VEX_BROWSER_RESULTS_ADD) {
        ladybird_browser_results_add(req->title, req->url, req->snippet);
        return 0;
    }
    if (req->op == VEX_BROWSER_RESULTS_SHOW) {
        ladybird_browser_results_show();
        return 0;
    }
    if (req->op == VEX_BROWSER_RESULTS_HIDE) {
        ladybird_browser_results_hide();
        return 0;
    }
    return (uint64_t)-1;
}

static uint64_t vex_sys_debug_log(uint64_t arg1) {
    debug_log_puts((const char *)arg1);
    return 0;
}

static uint64_t vex_sys_debug_log_read(uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    process_t *process = scheduler_current_process();
    char *buffer = (char *)(uintptr_t)arg1;
    uint32_t *seq_ptr = (uint32_t *)(uintptr_t)arg3;
    uint32_t seq;
    char stack_buffer[256];
    size_t total_copied = 0u;
    size_t max_len = (size_t)arg2;

    if (process == NULL || process->user_mode == 0 || buffer == NULL || seq_ptr == NULL) {
        return (uint64_t)-1;
    }
    if (max_len == 0u) {
        return 0u;
    }
    if (!vex_user_range_is_writable(process, (uint64_t)(uintptr_t)buffer, max_len) ||
        !vex_user_range_is_writable(process, (uint64_t)(uintptr_t)seq_ptr, sizeof(*seq_ptr))) {
        return (uint64_t)-1;
    }

    seq = *seq_ptr;
    while (total_copied < max_len) {
        size_t chunk_size = max_len - total_copied;
        size_t copied;

        if (chunk_size > sizeof(stack_buffer)) {
            chunk_size = sizeof(stack_buffer);
        }

        copied = debug_log_copy_recent(stack_buffer, chunk_size, &seq);
        if (copied == 0u) {
            break;
        }

        memcpy(buffer + total_copied, stack_buffer, copied);
        total_copied += copied;
    }

    *seq_ptr = seq;
    return (uint64_t)total_copied;
}

static uint64_t vex_sys_mouse_poll(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_mouse_packet_t *packet_user = (vex_mouse_packet_t *)(uintptr_t)arg1;
    vex_mouse_packet_t packet;
    int dx = 0;
    int dy = 0;
    uint8_t buttons = 0;

    if (process == NULL || process->user_mode == 0 || packet_user == NULL) {
        return (uint64_t)-1;
    }
    if (!vex_user_range_is_writable(process, (uint64_t)(uintptr_t)packet_user, sizeof(packet))) {
        return (uint64_t)-1;
    }
    if (!mouse_get_event(&dx, &dy, &buttons)) {
        return 0u;
    }

    packet.dx = (int32_t)dx;
    packet.dy = (int32_t)dy;
    packet.buttons = buttons;
    packet.reserved0 = 0u;
    packet.reserved1 = 0u;
    memcpy(packet_user, &packet, sizeof(packet));
    return 1u;
}

static void copy_fixed_string(char *dst, size_t capacity, const char *src) {
    size_t i = 0;

    if (dst == NULL || capacity == 0u) {
        return;
    }

    while (src != NULL && src[i] != '\0' && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static uint64_t vex_sys_bulk(uint64_t arg1) {
    vex_bulk_req_t *req = (vex_bulk_req_t *)arg1;
    vex_bulk_result_t *out;
    process_t *process;

    if (req == NULL || req->result == NULL) {
        return (uint64_t)-1;
    }

    out = req->result;
    memset(out, 0, sizeof(*out));
    process = scheduler_current_process();

    if ((req->mask & VEX_BULK_PID) != 0u) {
        out->pid = getpid();
        out->valid_mask |= VEX_BULK_PID;
    }
    if ((req->mask & VEX_BULK_PPID) != 0u) {
        out->ppid = getppid();
        out->valid_mask |= VEX_BULK_PPID;
    }
    if ((req->mask & VEX_BULK_UID) != 0u) {
        out->uid = getuid();
        out->is_admin = scheduler_current_is_admin();
        out->valid_mask |= VEX_BULK_UID;
    }
    if ((req->mask & VEX_BULK_EUID) != 0u) {
        out->euid = geteuid();
        out->valid_mask |= VEX_BULK_EUID;
    }
    if ((req->mask & VEX_BULK_TIME) != 0u) {
        out->monotonic_ms = boot_time_ms();
        out->valid_mask |= VEX_BULK_TIME;
    }
    if ((req->mask & VEX_BULK_CLOCK) != 0u) {
        out->scheduler_ticks = scheduler_ticks_now();
        out->valid_mask |= VEX_BULK_CLOCK;
    }
    if ((req->mask & VEX_BULK_MEMORY) != 0u) {
        out->total_pages = pmm_total_page_count();
        out->used_pages = pmm_used_page_count();
        out->free_pages = pmm_free_page_count();
        out->valid_mask |= VEX_BULK_MEMORY;
    }
    if ((req->mask & VEX_BULK_SCHEDULER) != 0u) {
        out->process_count = scheduler_process_count();
        out->valid_mask |= VEX_BULK_SCHEDULER;
    }
    if ((req->mask & VEX_BULK_NETWORK) != 0u) {
        out->local_ipv4 = ipv4_get_local_ip();
        out->gateway_ipv4 = dhcp_gateway();
        out->netmask_ipv4 = dhcp_netmask();
        out->link_up = (uint32_t)netdev_link_up();
        out->valid_mask |= VEX_BULK_NETWORK;
    }
    if ((req->mask & VEX_BULK_SYSTEM) != 0u) {
        copy_fixed_string(out->kernel_name, sizeof(out->kernel_name), "Vex-OS");
        copy_fixed_string(out->machine, sizeof(out->machine), "x86_64");
        out->valid_mask |= VEX_BULK_SYSTEM;
    }
    if ((req->mask & VEX_BULK_ABI) != 0u) {
        out->abi = scheduler_current_abi();
        out->user_mode = process == NULL ? 0u : process->user_mode;
        out->valid_mask |= VEX_BULK_ABI;
    }

    return 0;
}

static uint64_t vex_sys_process_spawn(uint64_t arg1) {
    vex_process_spawn_req_t *req = (vex_process_spawn_req_t *)arg1;
    int spawned;

    if (req == NULL || req->path == NULL) {
        debug_log_puts("spawn: invalid request\n");
        return (uint64_t)-1;
    }

    debug_log_puts("spawn: ");
    debug_log_puts(req->path);
    debug_log_puts("\n");
    spawned = elf_spawn_pid(req->path, &req->pid);
    if (spawned != 0) {
        debug_log_puts("spawn: failed ");
        debug_log_puts(req->path);
        debug_log_puts("\n");
        return (uint64_t)-1;
    }

    debug_log_puts("spawn: ok\n");
    return 0;
}

static uint64_t vex_sys_process_wait(uint64_t arg1) {
    vex_process_wait_req_t *req = (vex_process_wait_req_t *)arg1;
    uint32_t reaped_pid = 0u;

    if (req == NULL) {
        return (uint64_t)-1;
    }

    return (uint64_t)process_wait_pid((int32_t)req->pid, req->status, req->flags, &reaped_pid);
}

static uint64_t vex_sys_process_exec(uint64_t arg1) {
    vex_process_exec_req_t *req = (vex_process_exec_req_t *)arg1;

    if (req == NULL || req->path == NULL) {
        return (uint64_t)-1;
    }

    (void)req->argv;
    (void)req->envp;
    return (uint64_t)elf_exec_current(req->path);
}

uint64_t vex_syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    switch (number) {
    case VEX_SYS_BULK:
        return vex_sys_bulk(arg1);
    case VEX_SYS_OPEN:
        return vex_sys_open(arg1, arg2);
    case VEX_SYS_READFD:
    case VEX_SYS_WRITEFD:
        return vex_sys_fd_io(arg1, number);
    case VEX_SYS_CLOSE:
        return vex_sys_close(arg1);
    case VEX_SYS_PREAD:
        return vex_sys_pread(arg1);
    case VEX_SYS_PWRITE:
        return vex_sys_pwrite(arg1);
    case VEX_SYS_LSEEK:
        return vex_sys_lseek(arg1, arg2, arg3);
    case VEX_SYS_FSYNC:
        return vex_sys_fsync(arg1);
    case VEX_SYS_STAT:
    case VEX_SYS_LSTAT:
        return vex_sys_stat_like(arg1, number);
    case VEX_SYS_FSTAT:
        if (arg2 == 0u) {
            return (uint64_t)-1;
        }
        return (uint64_t)fstat((int)arg1, (struct stat *)arg2);
    case VEX_SYS_ACCESS:
        return vex_sys_access(arg1, arg2);
    case VEX_SYS_UNLINK:
        return vex_sys_unlink(arg1);
    case VEX_SYS_RENAME:
        return vex_sys_rename(arg1);
    case VEX_SYS_MKDIR:
        return vex_sys_mkdir(arg1);
    case VEX_SYS_RMDIR:
        return (uint64_t)rmdir((char const *)arg1);
    case VEX_SYS_CHDIR:
        return (uint64_t)chdir((char const *)arg1);
    case VEX_SYS_GETCWD:
        return vex_sys_getcwd(arg1);
    case VEX_SYS_GETPID:
        return (uint64_t)getpid();
    case VEX_SYS_GETPPID:
        return (uint64_t)scheduler_current_ppid();
    case VEX_SYS_GETUID:
        return (uint64_t)getuid();
    case VEX_SYS_GETEUID:
        return (uint64_t)geteuid();
    case VEX_SYS_ISATTY:
        return (uint64_t)isatty((int)arg1);
    case VEX_SYS_IOCTL:
        return vex_sys_ioctl(arg1);
    case VEX_SYS_FCNTL:
        return (uint64_t)fcntl((int)arg1, (int)arg2, (int)arg3);
    case VEX_SYS_DUP:
        return (uint64_t)dup((int)arg1);
    case VEX_SYS_DUP2:
        return (uint64_t)dup2((int)arg1, (int)arg2);
    case VEX_SYS_PIPE:
        return vex_sys_pipe(arg1);
    case VEX_SYS_MMAP:
        return vex_sys_mmap(arg1);
    case VEX_SYS_MUNMAP:
    case VEX_SYS_MPROTECT:
    case VEX_SYS_MADVISE:
        return vex_sys_memory_op(arg1, number);
    case VEX_SYS_HEAP_ALLOC:
        return vex_sys_heap_alloc(arg1);
    case VEX_SYS_HEAP_FREE:
        return vex_sys_heap_free(arg1);
    case VEX_SYS_HEAP_REALLOC:
        return vex_sys_heap_realloc(arg1);
    case VEX_SYS_MRAC_PUT:
        return vex_sys_mrac_put(arg1);
    case VEX_SYS_MRAC_GET:
        return vex_sys_mrac_get(arg1);
    case VEX_SYS_MRAC_REMOVE:
        return vex_sys_mrac_remove(arg1);
    case VEX_SYS_MRAC_CURRENT_VERSION:
        return (uint64_t)MRAC_current_version(scheduler_current_pid(), arg1);
    case VEX_SYS_MRAC_VERSION_COUNT:
        return (uint64_t)MRAC_version_count(scheduler_current_pid(), arg1);
    case VEX_SYS_MRAC_VERSION_INFO:
        return vex_sys_mrac_version_info(arg1);
    case VEX_SYS_JIT_ARENA_CREATE:
        return vex_sys_jit_arena_create(arg1);
    case VEX_SYS_JIT_ARENA_DESTROY:
        return vex_sys_jit_arena_destroy(arg1);
    case VEX_SYS_BROWSER_COMMAND_READ:
        return vex_sys_browser_command_read(arg1);
    case VEX_SYS_DEBUG_LOG:
        return vex_sys_debug_log(arg1);
    case VEX_SYS_DEBUG_LOG_READ:
        return vex_sys_debug_log_read(arg1, arg2, arg3);
    case VEX_SYS_BROWSER_RESULTS:
        return vex_sys_browser_results(arg1);
    case VEX_SYS_MOUSE_POLL:
        return vex_sys_mouse_poll(arg1);
    case VEX_SYS_PROCESS_SPAWN:
        return vex_sys_process_spawn(arg1);
    case VEX_SYS_PROCESS_WAIT:
        return vex_sys_process_wait(arg1);
    case VEX_SYS_PROCESS_KILL:
        return (uint64_t)process_kill((uint32_t)arg1);
    case VEX_SYS_PROCESS_EXIT:
        process_exit_status((int)arg1);
        return 0;
    case VEX_SYS_PROCESS_YIELD:
    case VEX_SYS_THREAD_YIELD:
        process_yield();
        return 0;
    case VEX_SYS_PROCESS_FORK:
        {
            const uint32_t child_pid = process_fork_current();
            return child_pid == 0u ? (uint64_t)-1 : (uint64_t)child_pid;
        }
    case VEX_SYS_PROCESS_EXEC:
        return vex_sys_process_exec(arg1);
    case VEX_SYS_THREAD_CREATE:
    case VEX_SYS_THREAD_JOIN:
    case VEX_SYS_THREAD_EXIT:
        return (uint64_t)-1;
    case VEX_SYS_POLL:
        return vex_sys_poll(arg1);
    case VEX_SYS_GETTIMEOFDAY:
    case VEX_SYS_CLOCK_GETTIME:
    case VEX_SYS_NANOSLEEP:
        return vex_sys_time(arg1, number);
    case VEX_SYS_SLEEP:
        return (uint64_t)sleep((unsigned int)arg1);
    case VEX_SYS_USLEEP:
        return (uint64_t)usleep((unsigned int)arg1);
    case VEX_SYS_FTRUNCATE:
        return (uint64_t)ftruncate((int)arg1, (off_t)arg2);
    case VEX_SYS_CHMOD:
        return (uint64_t)chmod((char const *)arg1, (uint32_t)arg2);
    case VEX_SYS_READLINK:
    case VEX_SYS_SYMLINK:
    case VEX_SYS_REALPATH:
        return vex_sys_path_buffer(arg1, number);
    case VEX_SYS_GETENV:
    case VEX_SYS_SETENV:
    case VEX_SYS_UNSETENV:
    case VEX_SYS_SYSCONF:
    case VEX_SYS_UNAME:
        return vex_sys_env(arg1, arg2, number);
    case VEX_SYS_GETRANDOM:
        return vex_sys_getrandom(arg1);
    case VEX_SYS_SOCKET:
        return (uint64_t)socket((int)arg1, (int)arg2, (int)arg3);
    case VEX_SYS_CONNECT:
    case VEX_SYS_BIND:
        return vex_sys_sockaddr(arg1, number);
    case VEX_SYS_SEND:
    case VEX_SYS_RECV:
        return vex_sys_send_recv(arg1, number);
    case VEX_SYS_CONNECT_HOSTNAME:
        return vex_sys_connect_hostname(arg1);
    case VEX_SYS_LISTEN:
        return (uint64_t)listen((int)arg1, (int)arg2);
    case VEX_SYS_ACCEPT:
        return vex_sys_accept(arg1);
    case VEX_SYS_SETSOCKOPT:
    case VEX_SYS_GETSOCKOPT:
        return vex_sys_sockopt(arg1, number);
    case VEX_SYS_SOCKETPAIR:
        return vex_sys_socketpair(arg1);
    case VEX_SYS_DISPLAY_OPEN:
        return (uint64_t)ladybird_display_open((uint32_t)arg1, (uint32_t)arg2);
    case VEX_SYS_DISPLAY_RESIZE:
        ladybird_display_resize((uint32_t)arg1, (uint32_t)arg2);
        return 0;
    case VEX_SYS_DISPLAY_PRESENT:
        return vex_sys_display_present(arg1);
    case VEX_SYS_DISPLAY_CLOSE:
        ladybird_display_close();
        return 0;
    case VEX_SYS_GPU_QUERY_INFO:
        return vex_sys_gpu_query_info(arg1);
    case VEX_SYS_GPU_BUFFER_CREATE:
        return vex_sys_gpu_buffer_create(arg1);
    case VEX_SYS_GPU_BUFFER_DESTROY:
        return vex_sys_gpu_buffer_destroy(arg1);
    case VEX_SYS_GPU_QUEUE_SUBMIT:
        return vex_sys_gpu_queue_submit(arg1);
    case VEX_SYS_GPU_PRESENT:
        return vex_sys_gpu_present(arg1);
    default:
        return 0;
    }
}
