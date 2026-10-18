#include <unos/vex_syscall.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/app_surface.h>
#include <unos/console.h>
#include <unos/compat.h>
#include <unos/boot_requests.h>
#include <unos/boot_time.h>
#include <unos/compositor_surface.h>
#include <unos/debug_log.h>
#include <unos/events.h>
#include <unos/dhcp.h>
#include <unos/dns.h>
#include <unos/elf.h>
#include <unos/elevation.h>
#include <unos/event_counter.h>
#include <unos/gpu.h>
#include <unos/fs.h>
#include <unos/fs_root.h>
#include <unos/fs_root_provider.h>
#include <unos/rendezvous.h>
#include <unos/process_resource.h>
#include <unos/gui.h>
#include <unos/heap.h>
#include <unos/kernel_allocator.h>
#include <unos/ipv4.h>
#include <unos/vex_legacy_compat.h>
#include <unos/mouse.h>
#include <unos/message_ipc.h>
#include <unos/versioned_store.h>
#include <unos/netdev.h>
#include <unos/pmm.h>
#include <unos/process_memory.h>
#include <unos/usercopy.h>
#include <unos/random.h>
#include <unos/scheduler.h>
#include <unos/vex_abi.h>
#include <unos/vmm.h>
#include <unos/irq.h>
#include <unos/smp.h>
#include <unos/users.h>
#include <stdio.h>
#include <unos/browser_viewport.h>
#include <unos/browser_downloads.h>
#include <unos/profiler.h>
#include <unos/sched_clock.h>

static void vex_mmap_debug_hex(const char *label, uint64_t value);

#define VEX_USER_PAGE_SIZE 4096ull
#define VEX_JIT_ARENA_DEFAULT_QUOTA_BYTES (16ull * 1024ull * 1024ull)
#define VEX_TRACKED_MMAP_MAX_BYTES (1024ull * 1024ull)
#define VEX_NATIVE_THREAD_STACK_SIZE (128ull * 1024ull)
#define VEX_MAP_FIXED 0x10
#define VEX_TRACE_BROWSER_PRESENT 0
#define VEX_EXEC_MAX_ARGS 128u
#define VEX_EXEC_MAX_STRING 4096u

typedef struct vex_jit_arena {
    uint32_t pid;
    uint32_t arena_id;
    uint32_t flags;
    uint32_t page_count;
    uint64_t rw_base;
    uint64_t rx_base;
    uint64_t size;
    uint64_t quota_bytes;
    uint64_t committed_bytes;
    uint32_t execute_faults;
    uint64_t source_namespace_id;
    /* Both aliases are views of this sparse, refcounted backing.  The arena
       owns the mappings; process_memory owns the backing lifetime. */
    uint64_t shared_id;
    struct vex_jit_arena *next;
} vex_jit_arena_t;

static vex_jit_arena_t *vex_jit_arenas;
static uint32_t vex_next_jit_arena_id = 1u;
static int vex_dispatch_user_string_is_valid(process_t *process,
                                              uint64_t address,
                                              size_t max_length);
static int vex_jit_materialize_for_process(process_t *process, vex_jit_arena_t *arena,
                                           uint64_t cr3, uint64_t address,
                                           uint64_t page_fault_error);

typedef struct vex_named_shm {
    /* Internal process-memory backing id.  This value never crosses the
     * syscall boundary; callers receive an opaque per-process capability. */
    uint64_t id;
    size_t size;
    int prot;
    uint32_t creator_pid;
    uint32_t handle_refs;
    char name[64];
    struct vex_named_shm *next;
} vex_named_shm_t;

static vex_named_shm_t *vex_named_shm_objects;

/* Named lookup is the explicit sharing step used by the launcher.  Keep it
 * bounded to the creator and a process it directly spawned; the returned
 * random capability is still checked on every subsequent operation. */
static int vex_shm_open_authorized(const vex_named_shm_t *object,
                                   uint32_t caller_pid) {
    return object != NULL &&
        (object->creator_pid == caller_pid || scheduler_current_ppid() == object->creator_pid);
}

typedef struct vex_shm_capability {
    uint64_t token;
    uint64_t backing_id;
    uint32_t owner_pid;
    uint32_t rights;
    struct vex_shm_capability *next;
} vex_shm_capability_t;

/* Capability tokens are deliberately independent from the monotonically
 * allocated process-memory backing ids.  A token is valid only in the owner
 * process and is never accepted as a backing id by process_memory. */
static vex_shm_capability_t *vex_shm_capabilities;

static int vex_shm_token_in_use(uint64_t token) {
    for (vex_shm_capability_t *cap = vex_shm_capabilities; cap != NULL;
         cap = cap->next) {
        if (cap->token == token) {
            return 1;
        }
    }
    return 0;
}

static vex_shm_capability_t *vex_find_shm_capability(uint32_t owner_pid,
                                                      uint64_t token) {
    for (vex_shm_capability_t *cap = vex_shm_capabilities; cap != NULL;
         cap = cap->next) {
        if (cap->owner_pid == owner_pid && cap->token == token) {
            return cap;
        }
    }
    return NULL;
}

static uint64_t vex_new_shm_token(void) {
    uint64_t token;

    do {
        if (kernel_random_try_fill(&token,sizeof(token)) != 0) return 0;
    } while (token == 0u || vex_shm_token_in_use(token));
    return token;
}

static void vex_reap_named_shm_if_unused(vex_named_shm_t *object) {
    vex_named_shm_t **link;

    if (object == NULL || object->handle_refs != 0u ||
        process_memory_shared_size(object->id) != 0u) {
        return;
    }
    for (link = &vex_named_shm_objects; *link != NULL; link = &(*link)->next) {
        if (*link == object) {
            *link = object->next;
            kfree(object);
            return;
        }
    }
}

static int vex_shm_grant_capability(vex_named_shm_t *object,
                                     uint32_t owner_pid,
                                     uint64_t *token_out) {
    vex_shm_capability_t *cap;

    if (object == NULL || owner_pid == 0u || token_out == NULL ||
        object->handle_refs == UINT32_MAX ||
        process_memory_shared_retain(object->id) != 0) {
        return -1;
    }
    cap = kmalloc(sizeof(*cap));
    if (cap == NULL) {
        process_memory_shared_release(object->id);
        return -1;
    }
    cap->token = vex_new_shm_token();
    if (!cap->token) { kfree(cap);process_memory_shared_release(object->id);return -1; }
    cap->backing_id = object->id;
    cap->owner_pid = owner_pid;
    cap->rights = (uint32_t)object->prot;
    cap->next = vex_shm_capabilities;
    vex_shm_capabilities = cap;
    ++object->handle_refs;
    *token_out = cap->token;
    return 0;
}

static int vex_shm_revoke_capability(vex_shm_capability_t *cap) {
    vex_shm_capability_t **link;
    vex_named_shm_t *object = NULL;
    int found = 0;

    if (cap == NULL) {
        return -1;
    }
    for (link = &vex_shm_capabilities; *link != NULL; link = &(*link)->next) {
        if (*link == cap) {
            *link = cap->next;
            found = 1;
            break;
        }
    }
    if (!found) {
        /* The capability was not linked; do not release an unrelated
         * backing.  This is defensive against a future double-reap path. */
        return -1;
    }
    for (vex_named_shm_t *candidate = vex_named_shm_objects; candidate != NULL;
         candidate = candidate->next) {
        if (candidate->id == cap->backing_id) {
            object = candidate;
            break;
        }
    }
    if (object != NULL && object->handle_refs != 0u) {
        --object->handle_refs;
    }
    process_memory_shared_release(cap->backing_id);
    if (object != NULL) {
        vex_reap_named_shm_if_unused(object);
    }
    kfree(cap);
    return 0;
}

static vex_named_shm_t *vex_find_named_shm(const char *name) {
    vex_named_shm_t **link = &vex_named_shm_objects;
    while (*link != NULL) {
        vex_named_shm_t *object = *link;
        if (process_memory_shared_size(object->id) == 0u) {
            *link = object->next;
            kfree(object);
            continue;
        }
        if (strcmp(object->name, name) == 0) {
            return object;
        }
        link = &object->next;
    }
    return NULL;
}

static int vex_copy_shm_name(process_t *process, const char *user_name,
                             char out[64]) {
    size_t length = 0u;
    if (user_name == NULL || !vex_dispatch_user_string_is_valid(process,
            (uint64_t)(uintptr_t)user_name, sizeof(out[0]) * 64u - 1u)) {
        return -1;
    }
    while (length + 1u < 64u && user_name[length] != '\0') {
        const char c = user_name[length];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
            return -1;
        }
        out[length++] = c;
    }
    if (length == 0u || user_name[length] != '\0') {
        return -1;
    }
    out[length] = '\0';
    return 0;
}

static uint64_t align_up_u64(uint64_t value, uint64_t alignment) {
    if (alignment == 0u || value > UINT64_MAX - (alignment - 1u)) {
        return 0u;
    }
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
    if (address >= VMM_USER_ADDRESS_LIMIT) {
        return 0;
    }
    if (length == 0u) {
        return 1;
    }
    if ((uint64_t)length > VMM_USER_ADDRESS_LIMIT - address) {
        return 0;
    }
    end = address + (uint64_t)length;

    in_user_window = address >= process->user_alloc_base && address < process->user_alloc_next;
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
        uint64_t page_flags = vmm_get_page_flags(pml4, page);
        if ((page_flags & VMM_PAGE_PRESENT) == 0u &&
            process_memory_region_is_lazy(process, page, VEX_USER_PAGE_SIZE) != 0) {
            const int jit_result = vex_jit_materialize_page(
                scheduler_current_memory_owner_pid(), process->cr3, page, 0x4u);
            if (jit_result < 0 || (jit_result == 0 &&
                    process_memory_materialize_page(scheduler_current_memory_owner_pid(),
                        process->cr3, page, 0x4u) != 0)) {
                return 0;
            }
            page_flags = vmm_get_page_flags(pml4, page);
        }
        if ((page_flags & (VMM_PAGE_PRESENT | VMM_PAGE_USER)) !=
            (VMM_PAGE_PRESENT | VMM_PAGE_USER)) {
            return 0;
        }
    }
    return 1;
}

static int vex_user_virtual_range_valid(uint64_t address, size_t length) {
    if (address >= VMM_USER_ADDRESS_LIMIT || length == 0u) {
        return length == 0u && address < VMM_USER_ADDRESS_LIMIT;
    }
    return (uint64_t)length <= VMM_USER_ADDRESS_LIMIT - address;
}

static int vex_user_mapping_is_free(process_t *process, uint64_t address, size_t length) {
    void *pml4;
    uint64_t end;

    if (process == NULL || !vex_user_virtual_range_valid(address, length)) {
        return 0;
    }
    if (!process_memory_range_is_free(process, address, length)) {
        return 0;
    }
    pml4 = vmm_cr3_to_address_space(process->cr3);
    if (pml4 == NULL) {
        return 0;
    }
    end = address + (uint64_t)length;
    /* Every user mapping is represented in process_memory.  For large
     * reservations, the inventory check above is authoritative; scanning
     * tens of millions of empty PTEs would defeat lazy mmap's purpose. */
    if (length > (64u * 1024u * 1024u)) {
        return 1;
    }
    for (uint64_t page = address & ~(VEX_USER_PAGE_SIZE - 1ull);
         page < end; page += VEX_USER_PAGE_SIZE) {
        if (vmm_get_page_flags(pml4, page) != 0u ||
            vmm_get_physical_any(pml4, page) != 0u) {
            return 0;
        }
    }
    return 1;
}

static int vex_user_range_is_writable(process_t *process, uint64_t address, size_t length) {
    if (!vex_user_range_is_accessible(process, address, length)) {
        return 0;
    }

    if (length != 0u) {
        const uint64_t end = address + (uint64_t)length;
        void *pml4 = vmm_cr3_to_address_space(process->cr3);
        const uint64_t start = address & ~(VEX_USER_PAGE_SIZE - 1ull);
        for (uint64_t page = start; page < end; page += VEX_USER_PAGE_SIZE) {
            if ((vmm_get_page_flags(pml4, page) & VMM_PAGE_WRITABLE) == 0u) {
                return 0;
            }
        }
    }
    return 1;
}

/* Compatibility shims build request records on a kernel stack and then call
   the same native handlers.  A ring-3 call is stopped by the dispatch gate
   before reaching them; nested pointers are therefore allowed to be trusted
   kernel pointers only when they already live in the kernel half. */
static int vex_dispatch_range_is_accessible(process_t *process,
                                             uint64_t address, size_t length) {
    return address >= VMM_USER_ADDRESS_LIMIT
        ? (process == NULL || process->in_user_syscall == 0u)
        : vex_user_range_is_accessible(process, address, length);
}
/* Caller holds process_memory's update transaction. The inventory grants
 * logical R/W access; existing usercopy validates/materializes USER PTEs. */
static int vex_memory_view_access(process_t *process,uint64_t address,size_t length,unsigned permissions) {
    if(!process || address>=VMM_USER_ADDRESS_LIMIT || length>VMM_USER_ADDRESS_LIMIT-address)return 0;
    uint64_t end=address+length,cursor=address;
    while(cursor<end) {
        vex_memory_view_info_t view;
        if(process_memory_query_view(scheduler_current_memory_owner_pid(),cursor,&view) ||
           view.kind==VEX_MEMORY_VIEW_FREE || (permissions&~view.permissions) || !view.length)return 0;
        uint64_t next=view.base+view.length;if(next<=cursor)return 0;cursor=next<end?next:end;
    }
    return (permissions&2)?vex_user_range_is_writable(process,address,length):vex_user_range_is_accessible(process,address,length);
}

static int vex_dispatch_range_is_writable(process_t *process,
                                           uint64_t address, size_t length) {
    return address >= VMM_USER_ADDRESS_LIMIT
        ? (process == NULL || process->in_user_syscall == 0u)
        : vex_user_range_is_writable(process, address, length);
}

/* Only typed top-level record checks use these helpers. Payload checks never
   accept a request snapshot as authority for a kernel pointer. */
static int vex_request_readable(process_t *p,uint64_t address,size_t size) {
    if(p && address==(uint64_t)(uintptr_t)p->native_request_snapshot && size<=p->native_request_snapshot_size)return 1;
    return vex_dispatch_range_is_accessible(p,address,size);
}
static int vex_request_writable(process_t *p,uint64_t address,size_t size) {
    if(p && address==(uint64_t)(uintptr_t)p->native_request_snapshot && size<=p->native_request_snapshot_size)return 1;
    return vex_dispatch_range_is_writable(p,address,size);
}
static int vex_user_string_is_valid(process_t *process, uint64_t address,
                                    size_t max_length) {
    if (process == NULL || address == 0u || max_length == 0u ||
        address >= VMM_USER_ADDRESS_LIMIT) {
        return 0;
    }
    size_t scanned = 0u;
    while (scanned < max_length) {
        if ((uint64_t)scanned >= VMM_USER_ADDRESS_LIMIT - address) {
            return 0;
        }
        const uint64_t cursor = address + (uint64_t)scanned;
        size_t chunk = VEX_USER_PAGE_SIZE - (cursor & (VEX_USER_PAGE_SIZE - 1ull));
        if (chunk > max_length - scanned) chunk = max_length - scanned;
        if ((uint64_t)chunk > VMM_USER_ADDRESS_LIMIT - cursor)
            chunk = (size_t)(VMM_USER_ADDRESS_LIMIT - cursor);
        /* Validate only this page: a NUL at its end must not require a
         * mapping (or materialize a lazy backing) on the following page. */
        if (!vex_user_range_is_accessible(process, cursor, chunk)) return 0;
        const volatile char *bytes = (const volatile char *)(uintptr_t)cursor;
        for (size_t i = 0u; i < chunk; ++i) {
            if (bytes[i] == '\0') return 1;
        }
        scanned += chunk;
    }
    return 0;
}

static int vex_dispatch_user_string_is_valid(process_t *process,
                                              uint64_t address,
                                              size_t max_length) {
    return process == NULL || process->user_mode == 0u ||
        (address >= VMM_USER_ADDRESS_LIMIT && process->in_user_syscall == 0u) ||
        vex_user_string_is_valid(process, address, max_length);
}

int vex_syscall_user_range_valid(uint64_t address, size_t length, int writable) {
    process_t *process = scheduler_current_process();
    if (process == NULL || process->user_mode == 0u) {
        return 0;
    }
    int valid = writable != 0
        ? vex_user_range_is_writable(process, address, length)
        : vex_user_range_is_accessible(process, address, length);
#ifdef VEX_BROWSER_DIAGNOSTIC
    static unsigned failures;
    if(!valid && failures++<40){
        vex_mmap_debug_hex("user range refused pid=",scheduler_current_pid());
        vex_mmap_debug_hex(" addr=",address);vex_mmap_debug_hex(" len=",length);
        vex_mmap_debug_hex(" write=",writable);debug_log_puts("\n");
    }
#endif
    return valid;
}

int vex_syscall_user_string_valid(uint64_t address, size_t max_length) {
    return vex_user_string_is_valid(scheduler_current_process(), address, max_length);
}

static size_t vex_syscall_request_size(uint64_t number) {
    switch (number) {
    case VEX_SYS_BULK: return sizeof(vex_bulk_req_t);
    case VEX_SYS_THREAD_START: return sizeof(vex_thread_start_req_t);
    case VEX_SYS_THREAD_CONTEXT: return sizeof(vex_thread_context_req_t);
    case VEX_SYS_THREAD_CONTROL: return sizeof(vex_thread_control_req_t);
    case VEX_SYS_THREAD_STOP: return sizeof(vex_thread_stop_req_t);
    case VEX_SYS_USER_IDENTITY: return sizeof(vex_user_identity_t);
    case VEX_SYS_FS_ROOT: return sizeof(vex_fs_root_req_t);
    case VEX_SYS_EXECUTION_GATE: return sizeof(vex_execution_req_t);
    case VEX_SYS_RENDEZVOUS: return sizeof(vex_rendezvous_req_t);
    case VEX_SYS_PROCESS_RESOURCE: return sizeof(vex_process_resource_req_t);
    case VEX_SYS_MEMORY_VIEW: return sizeof(vex_memory_view_req_t);
    case VEX_SYS_THREAD_EXIT_UNMAP: return sizeof(vex_thread_exit_unmap_req_t);
    case VEX_SYS_FUTEX: return sizeof(vex_futex_req_t);
    case VEX_SYS_STAT_EX: return sizeof(vex_stat_ex_req_t);
    case VEX_SYS_OPEN_EX: return sizeof(vex_open_ex_req_t);
    case VEX_SYS_RESOLVE_AT: return sizeof(vex_resolve_at_req_t);
    case VEX_SYS_GETDENTS: return sizeof(vex_getdents_req_t);
    case VEX_SYS_FILE_RANGE: return sizeof(vex_file_range_req_t);
    case VEX_SYS_EVENT_ALT_STACK: return sizeof(vex_event_alt_stack_req_t);
    case VEX_SYS_SOCKET_OP: return sizeof(vex_socket_op_req_t);
    case VEX_SYS_RESOLVE: return sizeof(vex_resolve_req_t);
    case VEX_SYS_THREAD_NAME: return sizeof(vex_thread_name_req_t);
    case VEX_SYS_SET_TIMES: return sizeof(vex_set_times_req_t);
    case VEX_SYS_FCHMOD: return sizeof(vex_fchmod_req_t);
    case VEX_SYS_DIRECTORY_LIST: return sizeof(vex_directory_list_req_t);
    case VEX_SYS_OPEN_PATH: return sizeof(vex_open_path_req_t);
    case VEX_SYS_OPEN_CONTEXT: return sizeof(vex_open_context_req_t);
    case VEX_SYS_READFD: case VEX_SYS_WRITEFD: return sizeof(vex_fd_io_req_t);
    case VEX_SYS_PREAD: case VEX_SYS_PWRITE: return sizeof(vex_rw_offset_req_t);
    case VEX_SYS_STAT: case VEX_SYS_LSTAT: return sizeof(vex_path_stat_req_t);
    case VEX_SYS_FSTAT: return 0u;
    case VEX_SYS_RENAME: return sizeof(vex_rename_req_t);
    case VEX_SYS_MKDIR: return sizeof(vex_mkdir_req_t);
    case VEX_SYS_GETCWD: return sizeof(vex_getcwd_req_t);
    case VEX_SYS_IOCTL: return sizeof(vex_ioctl_req_t);
    case VEX_SYS_PIPE: return sizeof(vex_pipe_req_t);
    case VEX_SYS_MMAP: return sizeof(vex_mmap_req_t);
    case VEX_SYS_MUNMAP: case VEX_SYS_MPROTECT: case VEX_SYS_MADVISE: return sizeof(vex_memory_req_t);
    case VEX_SYS_HEAP_FREE: case VEX_SYS_HEAP_REALLOC: return sizeof(vex_heap_req_t);
    case VEX_SYS_MRAC_PUT: return sizeof(vex_mrac_put_req_t);
    case VEX_SYS_MRAC_GET: return sizeof(vex_mrac_get_req_t);
    case VEX_SYS_MRAC_REMOVE: return sizeof(vex_mrac_remove_req_t);
    case VEX_SYS_MRAC_VERSION_INFO: return sizeof(vex_mrac_version_info_req_t);
    case VEX_SYS_JIT_ARENA_CREATE: return sizeof(vex_jit_arena_create_req_t);
    case VEX_SYS_JIT_ARENA_DESTROY: return sizeof(vex_jit_arena_destroy_req_t);
    case VEX_SYS_BROWSER_COMMAND_READ: return sizeof(vex_browser_command_req_t);
    case VEX_SYS_BROWSER_VIEWPORT: return sizeof(vex_viewport_req_t);
    case VEX_SYS_BROWSER_RESULTS: return sizeof(vex_browser_result_req_t);
    case VEX_SYS_MOUSE_POLL: return sizeof(vex_mouse_packet_t);
    case VEX_SYS_PROCESS_SPAWN: return sizeof(vex_process_spawn_req_t);
    case VEX_SYS_CONSOLE: return sizeof(vex_console_req_t);
    case VEX_SYS_PROCESS_WAIT: return sizeof(vex_process_wait_req_t);
    case VEX_SYS_THREAD_CREATE: return sizeof(vex_thread_create_req_t);
    case VEX_SYS_PROCESS_EXEC: return sizeof(vex_process_exec_req_t);
    case VEX_SYS_POLL: return sizeof(vex_poll_req_t);
    case VEX_SYS_GETTIMEOFDAY: return sizeof(vex_gettimeofday_req_t);
    case VEX_SYS_CLOCK_GETTIME: return sizeof(vex_clock_gettime_req_t);
    case VEX_SYS_NANOSLEEP: return sizeof(vex_nanosleep_req_t);
    case VEX_SYS_READLINK: return sizeof(vex_readlink_req_t);
    case VEX_SYS_SYMLINK: return sizeof(vex_symlink_req_t);
    case VEX_SYS_REALPATH: return sizeof(vex_realpath_req_t);
    case VEX_SYS_SETENV: return sizeof(vex_setenv_req_t);
    case VEX_SYS_GETRANDOM: return sizeof(vex_getrandom_req_t);
    case VEX_SYS_CONNECT: case VEX_SYS_BIND: return sizeof(vex_sockaddr_req_t);
    case VEX_SYS_SEND: case VEX_SYS_RECV: return sizeof(vex_send_recv_req_t);
    case VEX_SYS_CONNECT_HOSTNAME: return sizeof(vex_connect_hostname_req_t);
    case VEX_SYS_ACCEPT: return sizeof(vex_accept_req_t);
    case VEX_SYS_SETSOCKOPT: case VEX_SYS_GETSOCKOPT: return sizeof(vex_sockopt_req_t);
    case VEX_SYS_SOCKETPAIR: return sizeof(vex_socketpair_req_t);
    case VEX_SYS_MESSAGE_OBSERVE: return sizeof(vex_message_observe_req_t);
    case VEX_SYS_MESSAGE_RECV_AUTH: return sizeof(vex_message_auth_req_t);
    case VEX_SYS_MESSAGE_PAIR: return sizeof(vex_message_pair_req_t);
    case VEX_SYS_MESSAGE_SEND: case VEX_SYS_MESSAGE_RECV: return sizeof(vex_message_io_req_t);
    case VEX_SYS_DISPLAY_PRESENT: return sizeof(vex_display_present_req_t);
    case VEX_SYS_APP_WINDOW_OPEN: return sizeof(vex_app_window_open_req_t);
    case VEX_SYS_APP_WINDOW_PRESENT: return sizeof(vex_app_window_present_req_t);
    case VEX_SYS_APP_WINDOW_POLL: return sizeof(vex_app_window_poll_req_t);
    case VEX_SYS_APP_WINDOW_RESIZE: return sizeof(vex_app_window_resize_req_t);
    case VEX_SYS_SHM_CREATE: return sizeof(vex_shm_create_req_t);
    case VEX_SYS_SHM_OPEN: return sizeof(vex_shm_open_req_t);
    case VEX_SYS_SHM_MAP: return sizeof(vex_shm_map_req_t);
    case VEX_SYS_EVENT_REGISTER: return sizeof(vex_event_register_req_t);
    case VEX_SYS_EVENT_MASK: return sizeof(vex_event_mask_req_t);
    case VEX_SYS_EVENT_POST: return sizeof(vex_event_post_req_t);
    case VEX_SYS_GPU_QUERY_INFO: return sizeof(vex_gpu_info_t);
    case VEX_SYS_GPU_QUERY_CAPS: return sizeof(vex_gpu_caps_t);
    case VEX_SYS_GPU_BUFFER_CREATE: return sizeof(vex_gpu_buffer_create_req_t);
    case VEX_SYS_GPU_QUEUE_SUBMIT: return sizeof(vex_gpu_submit_req_t);
    case VEX_SYS_GPU_PRESENT: return sizeof(vex_gpu_present_req_t);
    case VEX_SYS_GPU_SURFACE_CREATE: return sizeof(vex_gpu_surface_create_req_t);
    case VEX_SYS_GPU_SURFACE_PRESENT: return sizeof(vex_gpu_surface_present_req_t);
    case VEX_SYS_GPU_SURFACE_WAIT: return sizeof(vex_gpu_surface_wait_req_t);
    case VEX_SYS_SURFACE_MAP: return sizeof(vex_surface_map_req_t);
    case VEX_SYS_SURFACE_QUERY: return sizeof(vex_surface_query_req_t);
    case VEX_SYS_SURFACE_UNMAP: return sizeof(vex_surface_unmap_req_t);
    case VEX_SYS_ELEVATION_REQUEST: return sizeof(vex_elevation_request_req_t);
    case VEX_SYS_ELEVATION_STATUS: return sizeof(vex_elevation_status_req_t);
    default: return 0u;
    }
}

static int vex_syscall_request_is_writable(uint64_t number) {
    switch (number) {
    case VEX_SYS_EVENT_ALT_STACK:
    case VEX_SYS_SOCKET_OP:
    case VEX_SYS_DIRECTORY_LIST:
    case VEX_SYS_APP_WINDOW_OPEN:
    case VEX_SYS_MRAC_PUT:
    case VEX_SYS_MRAC_GET:
    case VEX_SYS_JIT_ARENA_CREATE:
    case VEX_SYS_MOUSE_POLL:
    case VEX_SYS_PROCESS_SPAWN:
    case VEX_SYS_CONSOLE:
    case VEX_SYS_THREAD_CONTEXT:
    case VEX_SYS_THREAD_CONTROL:
    case VEX_SYS_USER_IDENTITY:
    case VEX_SYS_GPU_QUERY_INFO:
    case VEX_SYS_GPU_QUERY_CAPS:
    case VEX_SYS_GPU_BUFFER_CREATE:
    case VEX_SYS_BROWSER_VIEWPORT:
    case VEX_SYS_GPU_SURFACE_CREATE:
    case VEX_SYS_GPU_SURFACE_PRESENT:
    case VEX_SYS_SURFACE_MAP:
    case VEX_SYS_SURFACE_QUERY:
    case VEX_SYS_ELEVATION_REQUEST:
    case VEX_SYS_ELEVATION_STATUS:
    case VEX_SYS_SHM_CREATE:
    case VEX_SYS_SHM_OPEN:
    case VEX_SYS_SHM_MAP:
        return 1;
    case VEX_SYS_MESSAGE_PAIR:
        return 1;
    case VEX_SYS_THREAD_CREATE:
        return 1;
    case VEX_SYS_MESSAGE_OBSERVE:
    case VEX_SYS_MESSAGE_RECV_AUTH:
    case VEX_SYS_MESSAGE_RECV:
        return 1;
    case VEX_SYS_EVENT_MASK:
        return 1;
    default:
        return 0;
    }
}

static void vex_copy_user_string(process_t *process, const char *src, char *dst, size_t capacity) {
    size_t i = 0u;

    if (capacity == 0u) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (i + 1u < capacity &&
           vex_dispatch_range_is_accessible(process,
               (uint64_t)(uintptr_t)(src + i), 1u)) {
        const char c = src[i];
        dst[i++] = c;
        if (c == '\0') {
            return;
        }
    }
    dst[i] = '\0';
}

typedef struct vex_exec_vector {
    char **items;
    size_t count;
} vex_exec_vector_t;

static void vex_exec_vector_free(vex_exec_vector_t *vector) {
    if (vector == NULL) {
        return;
    }
    if (vector->items != NULL) {
        for (size_t index = 0u; index < vector->count; ++index) {
            if (vector->items[index] != NULL) {
                kfree(vector->items[index]);
            }
        }
        kfree(vector->items);
    }
    vector->items = NULL;
    vector->count = 0u;
}

static int vex_exec_vector_copy(process_t *process, const char *const *user_vector,
                                vex_exec_vector_t *out) {
    size_t count = 0u;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (user_vector == NULL) {
        return 0;
    }

    while (count < VEX_EXEC_MAX_ARGS) {
        uint64_t slot_address;
        const char *user_string;

        if ((uint64_t)(uintptr_t)user_vector > UINT64_MAX - count * sizeof(*user_vector)) {
            return -1;
        }
        slot_address = (uint64_t)(uintptr_t)user_vector + count * sizeof(*user_vector);
        if (process != NULL && process->user_mode != 0u
            ? !vex_user_range_is_accessible(process, slot_address, sizeof(*user_vector))
            : !vex_dispatch_range_is_accessible(process, slot_address, sizeof(*user_vector))) {
            return -1;
        }
        user_string = user_vector[count];
        if (user_string == NULL) {
            break;
        }
        if (process != NULL && process->user_mode != 0u
            ? !vex_user_string_is_valid(process, (uint64_t)(uintptr_t)user_string, VEX_EXEC_MAX_STRING)
            : !vex_dispatch_user_string_is_valid(process, (uint64_t)(uintptr_t)user_string, VEX_EXEC_MAX_STRING)) {
            return -1;
        }
        ++count;
    }
    if (count == VEX_EXEC_MAX_ARGS) {
        return -1;
    }

    out->items = kmalloc((count + 1u) * sizeof(*out->items));
    if (out->items == NULL) {
        return -1;
    }
    memset(out->items, 0, (count + 1u) * sizeof(*out->items));
    out->count = count;

    for (size_t index = 0u; index < count; ++index) {
        out->items[index] = kmalloc(VEX_EXEC_MAX_STRING);
        if (out->items[index] == NULL) {
            vex_exec_vector_free(out);
            return -1;
        }
        vex_copy_user_string(process, user_vector[index], out->items[index], VEX_EXEC_MAX_STRING);
    }
    return 0;
}

static int vex_is_jit_requested(int prot) {
    return (prot & PROT_WRITE) != 0 && (prot & PROT_EXEC) != 0;
}

static uint64_t vex_map_user_memory(process_t *process, size_t length, uint64_t flags) {
    uint64_t virt;
    uint64_t page_count;
    const uint32_t owner_pid = process != NULL && process->group != NULL
        ? process->group->process_id : (process == NULL ? 0u : process->pid);

    (void)flags;

    if (process == NULL || process->user_mode == 0 || length == 0u) {
        return 0;
    }

    const uint64_t aligned_length = align_up_u64(length, VEX_USER_PAGE_SIZE);
    if (aligned_length == 0u || aligned_length > VMM_USER_ADDRESS_LIMIT) {
        return 0;
    }

    virt = process_memory_next_alloc_hint(owner_pid,
        align_up_u64(scheduler_current_user_alloc_next(), VEX_USER_PAGE_SIZE));
    page_count = aligned_length / VEX_USER_PAGE_SIZE;
    if (page_count == 0u) {
        return 0;
    }
    for (uint32_t attempt = 0u; attempt < 1024u; ++attempt) {
        uint64_t next;
        if (virt != 0u && virt < VMM_USER_ADDRESS_LIMIT &&
            page_count <= (VMM_USER_ADDRESS_LIMIT - virt) / VEX_USER_PAGE_SIZE &&
            vex_user_mapping_is_free(process, virt, (size_t)aligned_length)) {
            scheduler_set_process_user_alloc_next(process->pid, virt + aligned_length);
            return virt;
        }
        if (virt == 0u || virt > UINT64_MAX - (uint64_t)aligned_length) {
            return 0;
        }
        next = process_memory_next_alloc_hint(owner_pid, virt + aligned_length);
        if (next <= virt) {
            next = virt + aligned_length;
        }
        virt = align_up_u64(next, VEX_USER_PAGE_SIZE);
    }
    return 0;
}

static uint64_t vex_sys_thread_create(uint64_t arg1, uint64_t tls_base) {
    vex_thread_create_req_t *request = (vex_thread_create_req_t *)(uintptr_t)arg1;
    process_t *process = scheduler_current_process();
    const uint64_t reserve_size = VEX_NATIVE_THREAD_STACK_SIZE + VEX_USER_PAGE_SIZE;
    uint64_t reservation;
    uint64_t stack_base;
    uint32_t tid;
    void *pml4;
    uint64_t entry_flags;

    if (request == NULL || process == NULL || process->user_mode == 0u ||
        process->group == NULL || request->entry == NULL || request->flags != 0u) {
        return (uint64_t)-1;
    }
    if (!vex_user_range_is_accessible(process,
            (uint64_t)(uintptr_t)request->entry, 1u)) {
        return (uint64_t)-1;
    }
    pml4 = vmm_cr3_to_address_space(process->cr3);
    if (pml4 == NULL) return (uint64_t)-1;
    entry_flags = vmm_get_page_flags(pml4,
        (uint64_t)(uintptr_t)request->entry);
    if ((entry_flags & (VMM_PAGE_PRESENT | VMM_PAGE_USER | VMM_PAGE_NX)) !=
        (VMM_PAGE_PRESENT | VMM_PAGE_USER)) return (uint64_t)-1;
    if (tls_base != 0u && !vex_user_range_is_accessible(process, tls_base, 1u))
        return (uint64_t)-1;

    reservation = vex_map_user_memory(process, (size_t)reserve_size, 0u);
    if (reservation == 0u || reservation > UINT64_MAX - reserve_size)
        return (uint64_t)-1;
    stack_base = reservation + VEX_USER_PAGE_SIZE; /* bottom guard page */
    if (process_memory_register_mapping(scheduler_current_process_id(),
            stack_base, (size_t)VEX_NATIVE_THREAD_STACK_SIZE,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
            PROCESS_MEMORY_FLAG_VMM_DIRECT | PROCESS_MEMORY_FLAG_LAZY |
                PROCESS_MEMORY_FLAG_STACK) != 0 ||
        process_memory_materialize_page(scheduler_current_process_id(),
            process->cr3, stack_base + VEX_NATIVE_THREAD_STACK_SIZE - 1u,
            0x6u) != 0) {
        (void)process_memory_remove_region(scheduler_current_process_id(),
            process->cr3, stack_base, (size_t)VEX_NATIVE_THREAD_STACK_SIZE);
        return (uint64_t)-1;
    }

    tid = process_create_native_thread(stack_base,
        VEX_NATIVE_THREAD_STACK_SIZE,
        (uint64_t)(uintptr_t)request->entry,
        (uint64_t)(uintptr_t)request->arg, tls_base, 0u, 0u, 0u);
    if (tid == 0u) {
        (void)process_memory_remove_region(scheduler_current_process_id(),
            process->cr3, stack_base, (size_t)VEX_NATIVE_THREAD_STACK_SIZE);
        return (uint64_t)-1;
    }
    request->tid = tid;
    return 0u;
}

static uint64_t vex_negative(int error) {
    return (uint64_t)(int64_t)-error;
}

/* VEX_SYS_THREAD_START: a thread on a stack owned by the caller (see
   vex_thread_abi.h). The kernel reads the request snapshot only. */
static uint64_t vex_sys_thread_start(uint64_t arg1) {
    const vex_thread_start_req_t *req = (const vex_thread_start_req_t *)(uintptr_t)arg1;
    process_t *process = scheduler_current_process();
    void *pml4;
    uint64_t entry_flags;
    uint32_t tid;

    if (process == NULL || process->user_mode == 0u || process->group == NULL)
        return vex_negative(EINVAL);
    if (req->size != sizeof(*req) || (req->flags & ~VEX_THREAD_START_PAUSED) || req->entry == 0u ||
        req->stack_top == 0u || (req->stack_top & 15u) != 0u ||
        req->stack_top < 2u * VEX_USER_PAGE_SIZE)
        return vex_negative(EINVAL);
    pml4 = vmm_cr3_to_address_space(process->cr3);
    if (pml4 == NULL) return vex_negative(EINVAL);
    entry_flags = vmm_get_page_flags(pml4, req->entry);
    if ((entry_flags & (VMM_PAGE_PRESENT | VMM_PAGE_USER | VMM_PAGE_NX)) !=
        (VMM_PAGE_PRESENT | VMM_PAGE_USER))
        return vex_negative(EFAULT);
    if (req->tls_base != 0u && req->tls_base >= VMM_USER_ADDRESS_LIMIT)
        return vex_negative(EINVAL);
    if (req->clear_on_exit != 0u &&
        ((req->clear_on_exit & 3u) != 0u ||
         !vex_user_range_is_writable(process, req->clear_on_exit, sizeof(int32_t))))
        return vex_negative(EFAULT);
    if (req->tid_out != 0u &&
        !vex_user_range_is_writable(process, req->tid_out, sizeof(int32_t)))
        return vex_negative(EFAULT);

    /* Only the top of the stack is used: the kernel writes the start frame
       just below stack_top, inside the caller's mapping. */
    tid = process_create_native_thread(req->stack_top - 2u * VEX_USER_PAGE_SIZE,
        2u * VEX_USER_PAGE_SIZE, req->entry, req->arg, req->tls_base,
        req->clear_on_exit, req->tid_out, PROCESS_NATIVE_THREAD_CALLER_STACK |
        ((req->flags&VEX_THREAD_START_PAUSED)?PROCESS_NATIVE_THREAD_START_PAUSED:0u));
    return tid != 0u ? (uint64_t)tid : vex_negative(EAGAIN);
}

static uint64_t vex_sys_thread_exit_unmap(uint64_t arg1) {
    const vex_thread_exit_unmap_req_t *req =
        (const vex_thread_exit_unmap_req_t *)(uintptr_t)arg1;
    process_t *process = scheduler_current_process();

    if (process == NULL || process->user_mode == 0u || req->reserved != 0u ||
        (req->base & (VEX_USER_PAGE_SIZE - 1u)) != 0u)
        return vex_negative(EINVAL);
    if (req->length != 0u &&
        process_memory_remove_region(scheduler_current_process_id(), process->cr3,
                                     req->base, (size_t)req->length) != 0)
        return vex_negative(EINVAL);
    process_exit_status(req->status);
    return 0u;
}

static uint64_t vex_sys_futex(uint64_t arg1) {
    const vex_futex_req_t *req = (const vex_futex_req_t *)(uintptr_t)arg1;

    if (req->flags != 0u || req->reserved != 0u) return vex_negative(EINVAL);
    switch (req->op) {
    case VEX_FUTEX_WAIT:
        return (uint64_t)(int64_t)futex_wait_native(req->addr, req->value, req->timeout_ns);
    case VEX_FUTEX_WAKE:
        return (uint64_t)(int64_t)futex_wake_native(req->addr, req->value);
    default:
        return vex_negative(EINVAL);
    }
}

/* Path buffers of the native file calls live on the 32 KiB kernel stack;
   VexFS paths are at most 512 bytes, so this leaves room for resolution. */
#define VEX_NATIVE_PATH_MAX 1024u

static uint64_t vex_sys_set_times(uint64_t arg1) {
    const vex_set_times_req_t *req = (const vex_set_times_req_t *)(uintptr_t)arg1;
    vex_set_times_req_t copy = *req;
    char path[VEX_NATIVE_PATH_MAX];

    if (req->path != NULL) {
        if (vex_user_string_copy(path, sizeof(path), (uint64_t)(uintptr_t)req->path) != 0)
            return vex_negative(EFAULT);
        if (path[0] != '/') return vex_negative(EINVAL); /* libc resolves dirfd */
        copy.path = path;
    }
    *ladybird_errno_location() = 0;
    const int result = vex_set_times(&copy);
    return result == 0 ? 0u : vex_negative(-result);
}

static uint64_t vex_sys_fchmod(uint64_t arg1) {
    const vex_fchmod_req_t *req = (const vex_fchmod_req_t *)(uintptr_t)arg1;
    *ladybird_errno_location() = 0;
    const int result = vex_fchmod(req->fd, req->mode);
    return result == 0 ? 0u : vex_negative(-result);
}

static uint64_t vex_sys_stat_ex(uint64_t arg1) {
    const vex_stat_ex_req_t *req = (const vex_stat_ex_req_t *)(uintptr_t)arg1;
    process_t *process = scheduler_current_process();
    vex_stat_ex_t record;
    char path[VEX_NATIVE_PATH_MAX];
    int result;

    if (process == NULL || req->out == NULL ||
        !vex_user_range_is_writable(process, (uint64_t)(uintptr_t)req->out, sizeof(record)))
        return vex_negative(EFAULT);
    switch (req->kind) {
    case VEX_STAT_EX_PATH:
    case VEX_STAT_EX_LPATH:
        if (req->path == NULL ||
            vex_user_string_copy(path, sizeof(path), (uint64_t)(uintptr_t)req->path) != 0)
            return vex_negative(EFAULT);
        result = vex_stat_ex_path(path, req->kind == VEX_STAT_EX_PATH, &record);
        break;
    case VEX_STAT_EX_FD:
        result = vex_stat_ex_fd(req->fd, &record);
        break;
      case VEX_STAT_EX_OBJECT:
          if(req->path)return vex_negative(EINVAL);
          result=vex_stat_ex_object(req->fd,&record);break;
    default:
        return vex_negative(EINVAL);
    }
    if (result != 0) return vex_negative(-result);
    return vex_copy_to_user((uint64_t)(uintptr_t)req->out, &record, sizeof(record)) == 0
        ? 0u : vex_negative(EFAULT);
}

static uint64_t vex_sys_open_ex(uint64_t arg1) {
    const vex_open_ex_req_t *req = (const vex_open_ex_req_t *)(uintptr_t)arg1;
    char path[VEX_NATIVE_PATH_MAX];
    char full_path[VEX_NATIVE_PATH_MAX];
    int fd;

    if (req->reserved != 0u || req->path == NULL) return vex_negative(EINVAL);
    if (vex_user_string_copy(path, sizeof(path), (uint64_t)(uintptr_t)req->path) != 0)
        return vex_negative(EFAULT);
    if (path[0] == '\0') return vex_negative(ENOENT);
    if (path[0] != '/' && req->dirfd != VEX_AT_FDCWD) {
        if (vex_web_resolve_dirfd_path(req->dirfd, path, full_path, sizeof(full_path)) != 0) {
            const int error = *ladybird_errno_location();
            return vex_negative(error != 0 ? error : EBADF);
        }
    } else {
        memcpy(full_path, path, sizeof(full_path));
    }
    *ladybird_errno_location() = 0;
    fd = vex_open_mode(full_path, (int)req->flags, req->mode);
    if (fd < 0) {
        const int error = *ladybird_errno_location();
        return vex_negative(error != 0 ? error : ENOENT);
    }
    return (uint64_t)fd;
}

static uint64_t vex_sys_resolve_at(uint64_t arg1) {
    const vex_resolve_at_req_t *req = (const vex_resolve_at_req_t *)(uintptr_t)arg1;
    process_t *process = scheduler_current_process();
    char path[VEX_NATIVE_PATH_MAX];
    char resolved[VEX_NATIVE_PATH_MAX];
    size_t length;

    if (process == NULL || req->reserved != 0u || req->path == NULL ||
        req->buffer == NULL || req->size == 0u)
        return vex_negative(EINVAL);
    if (vex_user_string_copy(path, sizeof(path), (uint64_t)(uintptr_t)req->path) != 0)
        return vex_negative(EFAULT);
    *ladybird_errno_location() = 0;
    if (req->dirfd == VEX_AT_FDCWD || path[0] == '/') {
        if (path[0] == '/') {
            memcpy(resolved, path, sizeof(resolved));
        } else {
            char cwd[VEX_NATIVE_PATH_MAX];
            size_t used;
            if (getcwd(cwd, sizeof(cwd)) == NULL) return vex_negative(ERANGE);
            used = strlen(cwd);
            if (used + 1u + strlen(path) + 1u > sizeof(resolved))
                return vex_negative(ENAMETOOLONG);
            memcpy(resolved, cwd, used);
            if (used == 0u || resolved[used - 1u] != '/') resolved[used++] = '/';
            memcpy(resolved + used, path, strlen(path) + 1u);
        }
    } else if (vex_web_resolve_dirfd_path(req->dirfd, path, resolved, sizeof(resolved)) != 0) {
        const int error = *ladybird_errno_location();
        return vex_negative(error != 0 ? error : EBADF);
    }
    length = strlen(resolved);
    if (length + 1u > req->size) return vex_negative(ERANGE);
    if (!vex_user_range_is_writable(process, (uint64_t)(uintptr_t)req->buffer, length + 1u) ||
        vex_copy_to_user((uint64_t)(uintptr_t)req->buffer, resolved, length + 1u) != 0)
        return vex_negative(EFAULT);
    return (uint64_t)length;
}

static uint64_t vex_sys_getdents(uint64_t arg1) {
    const vex_getdents_req_t *req = (const vex_getdents_req_t *)(uintptr_t)arg1;
    process_t *process = scheduler_current_process();
    VEX_SCRATCH(temporary);
    void *records;
    ssize_t used;

    if (process == NULL || req->reserved != 0u || req->buffer == NULL ||
        req->size == 0u || req->size > (1u << 20))
        return vex_negative(EINVAL);
    if (!vex_user_range_is_writable(process, (uint64_t)(uintptr_t)req->buffer, req->size))
        return vex_negative(EFAULT);
    records = vex_scratch_allocate(&temporary, (size_t)req->size);
    if (records == NULL) return vex_negative(ENOMEM);
    /* vex_dirent_t has the record layout produced by the directory backend. */
    *ladybird_errno_location() = 0;
    used = vex_web_getdents64(req->fd, records, (size_t)req->size);
    if (used < 0) {
        const int error = *ladybird_errno_location();
        return vex_negative(error != 0 ? error : EBADF);
    }
    if (used > 0 &&
        vex_copy_to_user((uint64_t)(uintptr_t)req->buffer, records, (size_t)used) != 0)
        return vex_negative(EFAULT);
    return (uint64_t)used;
}

/* VEX_SYS_SOCKET_OP: the request is a kernel snapshot; buffers and
   addresses are checked here, the results copied back with the snapshot. */
static uint64_t vex_sys_socket_op(uint64_t arg1) {
    vex_socket_op_req_t *req = (vex_socket_op_req_t *)(uintptr_t)arg1;
    process_t *process = scheduler_current_process();
    struct sockaddr_in address;
    uint32_t actual = 0u;
    ssize_t result;

    if (process == NULL || req->length > (uint64_t)INT64_MAX) return vex_negative(EINVAL);
    *ladybird_errno_location() = 0;
    switch (req->op) {
    case VEX_SOCKET_SENDTO:
        if (!vex_user_range_is_accessible(process, (uint64_t)(uintptr_t)req->buffer, (size_t)req->length))
            return vex_negative(EFAULT);
        if (req->addr != NULL) {
            if (req->addrlen < sizeof(address)) return vex_negative(EINVAL);
            if (vex_copy_from_user(&address, (uint64_t)(uintptr_t)req->addr, sizeof(address)) != 0)
                return vex_negative(EFAULT);
        }
        result = vex_socket_sendto(req->fd, req->buffer, (size_t)req->length, req->flags,
            req->addr != NULL ? (const struct sockaddr *)&address : NULL, sizeof(address));
        break;
    case VEX_SOCKET_RECVFROM:
        if (!vex_user_range_is_writable(process, (uint64_t)(uintptr_t)req->buffer, (size_t)req->length))
            return vex_negative(EFAULT);
        memset(&address, 0, sizeof(address));
        result = vex_socket_recvfrom(req->fd, req->buffer, (size_t)req->length, req->flags,
            (struct sockaddr *)&address, sizeof(address), &actual);
        break;
    case VEX_SOCKET_GETSOCKNAME:
    case VEX_SOCKET_GETPEERNAME:
        memset(&address, 0, sizeof(address));
        result = req->op == VEX_SOCKET_GETSOCKNAME
            ? vex_socketpair_getsockname(req->fd, (struct sockaddr *)&address, sizeof(address), &actual)
            : vex_socketpair_getpeername(req->fd, (struct sockaddr *)&address, sizeof(address), &actual);
        break;
    default:
        return vex_negative(EINVAL);
    }
    if (result < 0) {
        const int error = *ladybird_errno_location();
        return vex_negative(error != 0 ? error : EIO);
    }
    if (req->op != VEX_SOCKET_SENDTO) {
        const uint32_t copied = req->addrlen < actual ? req->addrlen : actual;
        if (req->addr != NULL && copied != 0u &&
            (!vex_user_range_is_writable(process, (uint64_t)(uintptr_t)req->addr, copied) ||
             vex_copy_to_user((uint64_t)(uintptr_t)req->addr, &address, copied) != 0))
            return vex_negative(EFAULT);
        req->addrlen = actual;
    }
    return (uint64_t)result;
}

static uint64_t vex_sys_resolve(uint64_t arg1) {
    const vex_resolve_req_t *req = (const vex_resolve_req_t *)(uintptr_t)arg1;
    process_t *process = scheduler_current_process();
    char name[256];
    uint32_t ip = 0u, wire;

    if (process == NULL || req->reserved != 0u || req->ipv4 == NULL || req->capacity == 0u)
        return vex_negative(EINVAL);
    if (!vex_user_range_is_writable(process, (uint64_t)(uintptr_t)req->ipv4, sizeof(uint32_t)))
        return vex_negative(EFAULT);
    if (req->name == NULL) {
        ip = dhcp_dns_server();
        if (ip == 0u) return 0u;
    } else {
        if (vex_user_string_copy(name, sizeof(name), (uint64_t)(uintptr_t)req->name) != 0)
            return vex_negative(EFAULT);
        if (dns_resolve(name, &ip) != 0 || ip == 0u) return vex_negative(ENOENT);
    }
    /* dns_resolve reports the address in host order. */
    wire = ((ip & 255u) << 24) | ((ip & 0xff00u) << 8) | ((ip >> 8) & 0xff00u) | (ip >> 24);
    return vex_copy_to_user((uint64_t)(uintptr_t)req->ipv4, &wire, sizeof(wire)) == 0
        ? 1u : vex_negative(EFAULT);
}

static uint64_t vex_sys_thread_name(uint64_t arg1) {
    const vex_thread_name_req_t *req = (const vex_thread_name_req_t *)(uintptr_t)arg1;
    process_t *process = scheduler_current_process();
    char name[VEX_THREAD_NAME_MAX];
    uint32_t tid, owner = 0u;
    size_t length = 0u;

    if (process == NULL || req->name == NULL || req->size == 0u) return vex_negative(EINVAL);
    tid = req->tid != 0u ? req->tid : scheduler_current_tid();
    /* Only threads of the caller's own Process. */
    if (scheduler_thread_name(tid, name, sizeof(name), &owner) != 0 ||
        owner != scheduler_current_process_id())
        return vex_negative(ESRCH);
    if (req->op == VEX_THREAD_NAME_SET) {
        if (vex_user_string_copy(name, sizeof(name), (uint64_t)(uintptr_t)req->name) != 0) {
            /* Longer than the limit: keep the first VEX_THREAD_NAME_MAX-1 bytes. */
            if (vex_copy_from_user(name, (uint64_t)(uintptr_t)req->name, sizeof(name) - 1u) != 0)
                return vex_negative(EFAULT);
            name[sizeof(name) - 1u] = '\0';
        }
        return scheduler_set_process_name(tid, name) == 0 ? 0u : vex_negative(ESRCH);
    }
    if (req->op != VEX_THREAD_NAME_GET) return vex_negative(EINVAL);
    while (length + 1u < sizeof(name) && name[length] != '\0') ++length;
    if (length + 1u > req->size) return vex_negative(ERANGE);
    if (!vex_user_range_is_writable(process, (uint64_t)(uintptr_t)req->name, length + 1u)) return vex_negative(EFAULT);
    return vex_copy_to_user((uint64_t)(uintptr_t)req->name, name, length + 1u) == 0 ? 0u : vex_negative(EFAULT);
}

static uint64_t vex_sys_thread_join(uint64_t tid_value, uint64_t status_address) {
    process_t *process = scheduler_current_process();
    int status = 0;
    if (process == NULL || process->user_mode == 0u || tid_value == 0u ||
        tid_value > UINT32_MAX ||
        (status_address != 0u &&
         !vex_user_range_is_writable(process, status_address, sizeof(status)))) {
        return (uint64_t)-1;
    }
    if (process_wait_thread((uint32_t)tid_value, &status) != 0)
        return (uint64_t)-1;
    if (status_address != 0u) *(int *)(uintptr_t)status_address = status;
    return 0u;
}

static vex_jit_arena_t *vex_find_jit_arena(uint32_t pid, uint32_t arena_id, uint64_t addr) {
    for (vex_jit_arena_t *arena = vex_jit_arenas; arena != NULL; arena = arena->next) {
        const uint64_t end = arena->rw_base <= UINT64_MAX - arena->size
            ? arena->rw_base + arena->size : 0u;
        const uint64_t rx_end = arena->rx_base <= UINT64_MAX - arena->size
            ? arena->rx_base + arena->size : 0u;

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

void vex_jit_report_execution_samples(void) {
    irq_user_sample_t samples[64];
    for(uint32_t cpu=0;cpu<smp_cpu_count();++cpu) {
        uint32_t count=irq_copy_user_samples(cpu,samples);
        for(uint32_t i=0;i<count;++i) {
            uint32_t pid=0;uint64_t rip=samples[i].rip;
            {kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
             SCHEDULER_TASK_REF(task_ref);
             process_t *task=scheduler_task_acquire(&task_ref,samples[i].tid,0);
             if(!task || !task->group)continue;
             vex_jit_arena_t *arena=vex_find_jit_arena(task->group->process_id,0u,rip);
             if(!arena || rip<arena->rx_base || rip-arena->rx_base>=arena->size)continue;
             uint64_t flags=vmm_get_page_flags(vmm_cr3_to_address_space(task->cr3),rip);
             if((flags&(VMM_PAGE_PRESENT|VMM_PAGE_USER|VMM_PAGE_WRITABLE|VMM_PAGE_NX))!=
                (VMM_PAGE_PRESENT|VMM_PAGE_USER))continue;
             pid=task->group->process_id;
            }
            char line[160];snprintf(line,sizeof(line),"smp-jit: pid=%u tid=%u cpu=%u rip=%08x%08x RX/RO instruction sampled\n",pid,samples[i].tid,cpu,(unsigned)(rip>>32u),(unsigned)rip);debug_log_puts(line);
            break;
        }
    }
}

static int vex_jit_materialize_for_process(process_t *process, vex_jit_arena_t *arena,
                                           uint64_t cr3, uint64_t address,
                                           uint64_t page_fault_error) {
    void *pml4;
    uint64_t page;
    uint64_t selected_base;
    int had_backing;

    if (process == NULL || arena == NULL || cr3 == 0u ||
        (page_fault_error & 0x1u) != 0u || (page_fault_error & 0x4u) == 0u) {
        return -1;
    }

    /* Check the selected alias with subtraction so a hostile address cannot
       wrap `base + size` into the user range. */
    if (address >= arena->rw_base &&
        arena->size <= UINT64_MAX - arena->rw_base &&
        address - arena->rw_base < arena->size) {
        selected_base = arena->rw_base;
    } else if (address >= arena->rx_base &&
               arena->size <= UINT64_MAX - arena->rx_base &&
               address - arena->rx_base < arena->size) {
        selected_base = arena->rx_base;
    } else {
        return -1;
    }
    page = address & ~(VEX_USER_PAGE_SIZE - 1ull);

    pml4 = vmm_cr3_to_address_space(cr3);
    if (pml4 == NULL) {
        return -1;
    }

    /* A page can already exist through the opposite alias (or be retained as
       a non-present PROT_NONE leaf).  Only a genuinely new backing page is
       charged against the arena's committed quota. */
    had_backing = process_memory_shared_page_present(arena->shared_id,
        (uint32_t)((page - selected_base) / VEX_USER_PAGE_SIZE));
    arena->committed_bytes = process_memory_shared_committed_bytes(arena->shared_id);
    if (!had_backing &&
        (arena->flags & (VEX_JIT_ARENA_FLAG_DISABLE_QUOTA |
                         VEX_JIT_ARENA_FLAG_FULL_OPEN)) == 0u &&
        (arena->committed_bytes > arena->quota_bytes ||
         arena->quota_bytes - arena->committed_bytes < VEX_USER_PAGE_SIZE)) {
        vex_mmap_debug_hex("jit: materialize quota reject addr=", page);
        vex_mmap_debug_hex(" committed=", arena->committed_bytes);
        vex_mmap_debug_hex(" quota=", arena->quota_bytes);
        debug_log_puts("\n");
        return -1;
    }

    if (process_memory_materialize_page(arena->pid, cr3, address,
                                        page_fault_error) != 0) {
        return -1;
    }
    if ((page_fault_error & 0x10u) && arena->execute_faults++ < 4u) {
        vex_mmap_debug_hex("jit: RX instruction fetch pid=", arena->pid);
        vex_mmap_debug_hex(" address=", address);
        vex_mmap_debug_hex(" phys=", vmm_get_physical(pml4, page));
        debug_log_puts("\n");
    }
    if (!had_backing) {
        arena->committed_bytes += VEX_USER_PAGE_SIZE;
        /* Bound serial diagnostics: a JSC pool may materialize thousands of
           pages; serial output must not become its compilation hot path. */
        if (arena->committed_bytes <= 4u * VEX_USER_PAGE_SIZE) {
            vex_mmap_debug_hex("jit: materialize page=", page);
            vex_mmap_debug_hex(" committed=", arena->committed_bytes);
            debug_log_puts("\n");
        }
    }
    return 1;
}

int vex_jit_materialize_page(uint32_t pid, uint64_t cr3, uint64_t address,
                             uint64_t page_fault_error) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    process_t *process = scheduler_current_process();
    vex_jit_arena_t *arena = vex_find_jit_arena(pid, 0u, address);

    if (arena == NULL) {
        return 0;
    }
    return vex_jit_materialize_for_process(process, arena, cr3, address,
                                           page_fault_error);
}

int vex_jit_clone_pid(uint32_t source_pid, uint32_t destination_pid) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_jit_arena_t *source;
    vex_jit_arena_t *added = NULL;

    if (source_pid == 0u || destination_pid == 0u) {
        return -1;
    }
    for (source = vex_jit_arenas; source != NULL; source = source->next) {
        vex_jit_arena_t *copy;
        if (source->pid != source_pid) {
            continue;
        }
        copy = kmalloc(sizeof(*copy));
        if (copy == NULL) {
            while (added != NULL) {
                vex_jit_arena_t *next = added->next;
                kfree(added);
                added = next;
            }
            return -1;
        }
        *copy = *source;
        copy->pid = destination_pid;
        copy->source_namespace_id = 0u;
        /* Child aliases start non-present even when the shared backing already
           contains pages.  Charge only pages first materialized by this child. */
        copy->committed_bytes = 0u;
        copy->execute_faults = 0u;
        copy->next = added;
        added = copy;
    }
    while (added != NULL) {
        vex_jit_arena_t *next = added->next;
        added->next = vex_jit_arenas;
        vex_jit_arenas = added;
        added = next;
    }
    return 0;
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

    /* Keep the active namespace authoritative for JIT aliases as well.  The
     * sparse shared backing owns materialized pages; removing these records
     * only drops the two mapping references and never frees a page twice. */
    (void)process_memory_remove_region(arena->pid, process->cr3, arena->rw_base, arena->size);
    (void)process_memory_remove_region(arena->pid, process->cr3, arena->rx_base, arena->size);

    if (arena->source_namespace_id != 0u) {
        (void)process_memory_namespace_destroy(arena->pid, arena->source_namespace_id, 0u);
    }

    vex_unlink_jit_arena(arena);
    kfree(arena);
    return 0;
}

static uint64_t vex_sys_jit_arena_create(uint64_t arg1) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
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

    if (req == NULL || process == NULL || process->user_mode == 0 || req->size == 0u) {
        return (uint64_t)-1;
    }

    if ((req->flags & (VEX_JIT_ARENA_FLAG_DISABLE_LOCKS | VEX_JIT_ARENA_FLAG_FULL_OPEN)) != 0u) {
        /* These flags explicitly defeat the W^X contract.  There is no
           capability in this ABI that authorizes an executable writable alias. */
        return (uint64_t)-1;
    }

    {
        const uint64_t aligned = align_up_u64(req->size, VEX_USER_PAGE_SIZE);
        if (aligned == 0u || aligned > VMM_USER_ADDRESS_LIMIT ||
            aligned / VEX_USER_PAGE_SIZE > UINT32_MAX) {
            return (uint64_t)-1;
        }
        aligned_size = (size_t)aligned;
    }
    quota_limit = req->quota_bytes == 0u ? VEX_JIT_ARENA_DEFAULT_QUOTA_BYTES : req->quota_bytes;
    pml4 = vmm_cr3_to_address_space(process->cr3);
    if (pml4 == NULL || hhdm_request.response == NULL) {
        return (uint64_t)-1;
    }

    current_namespace_id = process_memory_current_namespace(scheduler_current_process_id());
    if (current_namespace_id != 0u &&
        process_memory_snapshot_namespace(scheduler_current_process_id(), current_namespace_id, &snapshot_namespace_id) != 0) {
        return (uint64_t)-1;
    }

    if (req->preferred_rw_addr != NULL &&
        (((uint64_t)(uintptr_t)req->preferred_rw_addr & (VEX_USER_PAGE_SIZE - 1ull)) != 0u ||
         !vex_user_mapping_is_free(process, (uint64_t)(uintptr_t)req->preferred_rw_addr, aligned_size))) {
        if (snapshot_namespace_id != 0u) {
            (void)process_memory_namespace_destroy(scheduler_current_process_id(), snapshot_namespace_id, 0u);
        }
        return (uint64_t)-1;
    }
    if (req->preferred_rx_addr != NULL &&
        (((uint64_t)(uintptr_t)req->preferred_rx_addr & (VEX_USER_PAGE_SIZE - 1ull)) != 0u ||
         !vex_user_mapping_is_free(process, (uint64_t)(uintptr_t)req->preferred_rx_addr, aligned_size))) {
        if (snapshot_namespace_id != 0u) {
            (void)process_memory_namespace_destroy(scheduler_current_process_id(), snapshot_namespace_id, 0u);
        }
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
            (void)process_memory_namespace_destroy(scheduler_current_process_id(), snapshot_namespace_id, 0u);
        }
        return (uint64_t)-1;
    }
    if (!vex_user_virtual_range_valid(rw_base, aligned_size) ||
        !vex_user_virtual_range_valid(rx_base, aligned_size) ||
        (aligned_size > UINT64_MAX - rx_base) ||
        (aligned_size > UINT64_MAX - rw_base) ||
        (rw_base < rx_base + aligned_size && rx_base < rw_base + aligned_size)) {
        if (snapshot_namespace_id != 0u) {
            (void)process_memory_namespace_destroy(scheduler_current_process_id(), snapshot_namespace_id, 0u);
        }
        return (uint64_t)-1;
    }

    arena = kmalloc(sizeof(*arena));
    if (arena == NULL) {
        if (snapshot_namespace_id != 0u) {
            (void)process_memory_namespace_destroy(scheduler_current_process_id(), snapshot_namespace_id, 0u);
        }
        return (uint64_t)-1;
    }

    arena->pid = scheduler_current_process_id();
    arena->arena_id = vex_next_jit_arena_id++;
    arena->flags = req->flags;
    arena->page_count = (uint32_t)(aligned_size / VEX_USER_PAGE_SIZE);
    arena->rw_base = rw_base;
    arena->rx_base = rx_base;
    arena->size = aligned_size;
    arena->quota_bytes = quota_limit;
    arena->committed_bytes = 0u;
    arena->execute_faults = 0u;
    arena->source_namespace_id = snapshot_namespace_id;
    arena->shared_id = 0u;
    arena->next = NULL;

    /* Reserve two aliases in the authoritative process-memory inventory.  A
     * sparse shared backing gives both aliases the same page only when either
     * alias is touched, so a 1 GiB arena consumes metadata plus demand pages,
     * never 1 GiB of physical memory. */
    if (process_memory_shared_create_lazy(arena->size, &arena->shared_id) != 0 ||
        process_memory_shared_map_lazy(arena->pid, process->cr3, arena->shared_id,
            arena->rw_base, arena->size, 0u, PROT_READ | PROT_WRITE) != 0 ||
        process_memory_mark_jit_region(arena->pid, arena->rw_base) != 0 ||
        process_memory_shared_map_lazy(arena->pid, process->cr3, arena->shared_id,
            arena->rx_base, arena->size, 0u, PROT_READ | PROT_EXEC) != 0 ||
        process_memory_mark_jit_region(arena->pid, arena->rx_base) != 0) {
        (void)process_memory_remove_region(arena->pid, process->cr3,
                                            arena->rw_base, arena->size);
        (void)process_memory_remove_region(arena->pid, process->cr3,
                                            arena->rx_base, arena->size);
        if (arena->shared_id != 0u) {
            /* Drop the creator reference; successful map calls own the other
             * references and remove_region releases those during rollback. */
            process_memory_shared_release(arena->shared_id);
        }
        if (snapshot_namespace_id != 0u) {
            (void)process_memory_namespace_destroy(arena->pid, snapshot_namespace_id, 0u);
        }
        kfree(arena);
        return (uint64_t)-1;
    }
    /* The two mappings retained their own references. */
    process_memory_shared_release(arena->shared_id);

    vex_mmap_debug_hex("jit: lazy reserve size=", arena->size);
    vex_mmap_debug_hex(" rw=", arena->rw_base);
    vex_mmap_debug_hex(" rx=", arena->rx_base);
    debug_log_puts("\n");

    arena->next = vex_jit_arenas;
    vex_jit_arenas = arena;

    req->arena_id = arena->arena_id;
    req->rw_base = (void *)(uintptr_t)arena->rw_base;
    req->rx_base = (void *)(uintptr_t)arena->rx_base;
    req->source_namespace_id = arena->source_namespace_id;
    return 0;
}

static uint64_t vex_sys_jit_arena_destroy(uint64_t arg1) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_jit_arena_destroy_req_t *req = (vex_jit_arena_destroy_req_t *)arg1;
    process_t *process = scheduler_current_process();
    vex_jit_arena_t *arena;

    if (req == NULL || process == NULL) {
        return (uint64_t)-1;
    }

    arena = vex_find_jit_arena(scheduler_current_process_id(), req->arena_id, (uint64_t)(uintptr_t)req->addr);
    if (arena == NULL) {
        return (uint64_t)-1;
    }

    return (uint64_t)vex_destroy_jit_arena_internal(process, arena);
}

void vex_jit_reap_pid(uint32_t pid, uint64_t cr3) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
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
    process_t *process = scheduler_current_process();
    if (!vex_dispatch_user_string_is_valid(process, arg1, 4096u)) {
        return (uint64_t)-1;
    }
    return (uint64_t)open((char const *)arg1, (int)arg2);
}

static uint64_t vex_sys_openat(uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    process_t *process = scheduler_current_process();
    if (!vex_dispatch_user_string_is_valid(process, arg2, 4096u)) {
        return (uint64_t)-1;
    }
    return (uint64_t)openat((int)arg1, (char const *)arg2, (int)arg3);
}

static uint64_t vex_buffer_io(int fd,void *buffer,size_t count,off_t offset,int writing,int positional) {
    process_t *p=scheduler_current_process();
    if(p && p->user_mode && (uint64_t)(uintptr_t)buffer<VMM_USER_ADDRESS_LIMIT) {
        VEX_SCRATCH(temporary);
        uint8_t local[1024];
        if(count>(size_t)INT64_MAX){*ladybird_errno_location()=EINVAL;return (uint64_t)-1;}
        {kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
         if(!vex_syscall_user_range_valid((uint64_t)(uintptr_t)buffer,count,!writing)){
             *ladybird_errno_location()=EFAULT;return (uint64_t)-1;}}
        void *bytes=count<=sizeof(local)?local:vex_scratch_allocate(&temporary,count);
        if(!bytes){*ladybird_errno_location()=ENOMEM;return (uint64_t)-1;}
        if(writing && vex_copy_from_user(bytes,(uint64_t)(uintptr_t)buffer,count)){
            *ladybird_errno_location()=EFAULT;return (uint64_t)-1;}
        ssize_t result=writing?(positional?pwrite(fd,bytes,count,offset):write(fd,bytes,count)):
            (positional?pread(fd,bytes,count,offset):read(fd,bytes,count));
        if(!writing && result>0 && vex_copy_to_user((uint64_t)(uintptr_t)buffer,bytes,(size_t)result)){
            *ladybird_errno_location()=EFAULT;return (uint64_t)-1;}
        return (uint64_t)result;
    }
    if(p && p->in_user_syscall){*ladybird_errno_location()=EFAULT;return (uint64_t)-1;}
    return (uint64_t)(writing?(positional?pwrite(fd,buffer,count,offset):write(fd,buffer,count)):
                             (positional?pread(fd,buffer,count,offset):read(fd,buffer,count)));
}
static uint64_t vex_sys_fd_io(uint64_t arg1, uint64_t which) {
    vex_fd_io_req_t *req = (vex_fd_io_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL) {
        *ladybird_errno_location()=EFAULT;
        return (uint64_t)-1;
    }

    if (which == VEX_SYS_READFD) {
        if (!vex_dispatch_range_is_writable(process,
                (uint64_t)(uintptr_t)req->buffer, req->count)) {
            *ladybird_errno_location()=EFAULT;
            return (uint64_t)-1;
        }
        return vex_buffer_io(req->fd,req->buffer,req->count,0,0,0);
    }

    if (!vex_dispatch_range_is_accessible(process,
            (uint64_t)(uintptr_t)req->buffer, req->count)) {
        *ladybird_errno_location()=EFAULT;
        return (uint64_t)-1;
    }
    return vex_buffer_io(req->fd,req->buffer,req->count,0,1,0);
}

static uint64_t vex_sys_close(uint64_t arg1) {
    return (uint64_t)close((int)arg1);
}

static uint64_t vex_sys_pread(uint64_t arg1) {
    vex_rw_offset_req_t *req = (vex_rw_offset_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL) {
        *ladybird_errno_location()=EFAULT;
        return (uint64_t)-1;
    }
    if (!vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->buffer, req->count)) {
        *ladybird_errno_location()=EFAULT;
        return (uint64_t)-1;
    }
    return vex_buffer_io(req->fd,req->buffer,req->count,req->offset,0,1);
}

static uint64_t vex_sys_pwrite(uint64_t arg1) {
    vex_rw_offset_req_t *req = (vex_rw_offset_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL) {
        *ladybird_errno_location()=EFAULT;
        return (uint64_t)-1;
    }
    if (!vex_dispatch_range_is_accessible(process,
            (uint64_t)(uintptr_t)req->buffer, req->count)) {
        *ladybird_errno_location()=EFAULT;
        return (uint64_t)-1;
    }
    return vex_buffer_io(req->fd,req->buffer,req->count,req->offset,1,1);
}

static uint64_t vex_sys_lseek(uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    return (uint64_t)lseek((int)arg1, (off_t)arg2, (int)arg3);
}

static uint64_t vex_sys_fsync(uint64_t arg1) {
    return (uint64_t)fsync((int)arg1);
}

static uint64_t vex_sys_stat_like(uint64_t arg1, uint64_t which) {
    kernel_guard_t copy_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_path_stat_req_t *req = (vex_path_stat_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL) {
        return (uint64_t)-1;
    }

    if (!vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->st, sizeof(*req->st))) {
        return (uint64_t)-1;
    }
    if (which != VEX_SYS_FSTAT &&
        (req->path == NULL ||
         (process != NULL && process->user_mode != 0u &&
          !vex_dispatch_user_string_is_valid(process,
              (uint64_t)(uintptr_t)req->path, 4096u)))) {
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
    kernel_guard_t copy_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    process_t *process = scheduler_current_process();
    if (!vex_dispatch_user_string_is_valid(process, arg1, 4096u)) {
        return (uint64_t)-1;
    }
    return (uint64_t)access((char const *)arg1, (int)arg2);
}

static uint64_t vex_sys_unlink(uint64_t arg1) {
    kernel_guard_t copy_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    process_t *process = scheduler_current_process();
    if (!vex_dispatch_user_string_is_valid(process, arg1, 4096u)) {
        return (uint64_t)-1;
    }
    return (uint64_t)unlink((char const *)arg1);
}

static uint64_t vex_sys_rename(uint64_t arg1) {
    kernel_guard_t copy_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_rename_req_t *req = (vex_rename_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL) {
        return (uint64_t)-1;
    }
    if ((req->old_path == NULL || req->new_path == NULL) ||
        (process != NULL && process->user_mode != 0u &&
         (!vex_dispatch_user_string_is_valid(process, (uint64_t)(uintptr_t)req->old_path, 4096u) ||
          !vex_dispatch_user_string_is_valid(process, (uint64_t)(uintptr_t)req->new_path, 4096u)))) {
        return (uint64_t)-1;
    }
    return (uint64_t)rename(req->old_path, req->new_path);
}

static uint64_t vex_sys_mkdir(uint64_t arg1) {
    kernel_guard_t copy_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_mkdir_req_t *req = (vex_mkdir_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL) {
        return (uint64_t)-1;
    }
    if (req->path == NULL ||
        (process != NULL && process->user_mode != 0u &&
         !vex_dispatch_user_string_is_valid(process, (uint64_t)(uintptr_t)req->path, 4096u))) {
        return (uint64_t)-1;
    }
    return (uint64_t)mkdir(req->path, req->mode);
}

static uint64_t vex_sys_getcwd(uint64_t arg1) {
    kernel_guard_t copy_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_getcwd_req_t *req = (vex_getcwd_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL) {
        return 0;
    }
    if (!vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->buffer, req->size)) {
        return 0;
    }
    return (uint64_t)(uintptr_t)getcwd(req->buffer, req->size);
}

static uint64_t vex_sys_ioctl(uint64_t arg1) {
    kernel_guard_t copy_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_ioctl_req_t *req = (vex_ioctl_req_t *)arg1;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    return (uint64_t)ioctl(req->fd, req->request, req->arg);
}

static uint64_t vex_sys_pipe(uint64_t arg1) {
    kernel_guard_t copy_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_pipe_req_t *req = (vex_pipe_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL) {
        return (uint64_t)-1;
    }
    if (!vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->pipefd, 2u * sizeof(*req->pipefd))) {
        return (uint64_t)-1;
    }
    return (uint64_t)pipe(req->pipefd);
}

static void vex_mmap_debug_hex(const char *label, uint64_t value) {
    static const char hex_digits[] = "0123456789abcdef";
    char out[19];
    size_t pos = 0u;

    debug_log_puts(label);
    out[pos++] = '0';
    out[pos++] = 'x';
    for (int shift = 60; shift >= 0; shift -= 4) {
        out[pos++] = hex_digits[(value >> shift) & 0xFu];
    }
    out[pos] = '\0';
    debug_log_puts(out);
}

static uint64_t vex_sys_mmap(uint64_t arg1) {
    profiler_span_t span __attribute__((cleanup(profiler_span_end)))=profiler_span_begin(100);
    kernel_guard_t memory_update __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
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
        span.id = 101;
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
    if (aligned_length == 0u || !vex_user_virtual_range_valid(1u, aligned_length)) {
        return (uint64_t)MAP_FAILED;
    }
    if (req->addr != NULL) {
        addr = (uint64_t)(uintptr_t)req->addr;
        if ((addr & (VEX_USER_PAGE_SIZE - 1ull)) != 0u ||
            !vex_user_virtual_range_valid(addr, aligned_length)) {
            return (uint64_t)MAP_FAILED;
        }
        if (!vex_user_mapping_is_free(process, addr, aligned_length)) {
            if ((req->flags & VEX_MAP_FIXED) != 0) {
                if (process_memory_remove_region(scheduler_current_process_id(), process->cr3,
                                                 addr, aligned_length) != 0) {
                    vex_mmap_debug_hex("mmap: MAP_FIXED range busy addr=", addr);
                    debug_log_puts("\n");
                    return (uint64_t)MAP_FAILED;
                }
            } else {
                /* addr is only a hint without MAP_FIXED.  An occupied hint
                 * must fall back to another free user range; glibc/WPE use
                 * this while reserving loader and thread arenas. */
                if (VEX_COMPAT_TRACE) {
                vex_mmap_debug_hex("mmap: preferred range busy, using fallback hint=", addr);
                vex_mmap_debug_hex(" prot=", (uint64_t)(uint32_t)req->prot);
                vex_mmap_debug_hex(" flags=", (uint64_t)(uint32_t)req->flags);
                vex_mmap_debug_hex(" fd=", (uint64_t)(uint32_t)req->fd);
                vex_mmap_debug_hex(" len=", (uint64_t)aligned_length);
                debug_log_puts("\n");
                }
                addr = vex_map_user_memory(process, aligned_length, 0u);
                if (addr == 0u) {
                    return (uint64_t)MAP_FAILED;
                }
                if (VEX_COMPAT_TRACE) {
                    vex_mmap_debug_hex("mmap: preferred hint fallback addr=", addr);
                    debug_log_puts("\n");
                }
            }
        }
    } else {
        addr = vex_map_user_memory(process, aligned_length, 0u);
    }
    if (addr == 0u || !vex_user_virtual_range_valid(addr, aligned_length)) {
        return (uint64_t)MAP_FAILED;
    }

    {
        const int file_backed =
            (req->flags & MAP_ANONYMOUS) == 0 && req->fd >= 0;
        size_t file_length = 0u;
        if (file_backed) {
            struct stat mapping_stat;
            uint64_t remaining;
            if (req->offset < 0 || fstat(req->fd, &mapping_stat) != 0 ||
                mapping_stat.st_size < 0) {
                return (uint64_t)MAP_FAILED;
            }
            remaining = (uint64_t)req->offset >= (uint64_t)mapping_stat.st_size
                ? 0u : (uint64_t)mapping_stat.st_size - (uint64_t)req->offset;
            if (remaining > req->length) {
                remaining = req->length;
            }
            file_length = (size_t)remaining;
        }
        /* File-backed mappings are reservations first.  Keep the file object
         * alive in process_memory and fault pages in on demand; eagerly
         * materialising an entire shared-library segment makes fork/exec
         * consume a kernel-sized snapshot for bytes the process has never
         * touched. */
        if (file_backed != 0) {
            span.id = 102;
            if (req->offset < 0 ||
                ((uint64_t)req->offset & (VEX_USER_PAGE_SIZE - 1ull)) != 0u ||
                process_memory_file_map(scheduler_current_process_id(), process->cr3,
                    addr, aligned_length, req->prot, req->flags, req->fd,
                    (uint64_t)req->offset, file_length) != 0) {
                vex_mmap_debug_hex("mmap-file: lazy reserve FAILED addr=", addr);
                vex_mmap_debug_hex(" len=", (uint64_t)aligned_length);
                debug_log_puts("\n");
                return (uint64_t)MAP_FAILED;
            }
            return addr;
        }
        if (file_length == 0u &&
            (req->flags & MAP_ANONYMOUS) != 0 &&
            (req->prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) != 0 &&
            (req->flags & MAP_SHARED) != 0 && aligned_length <= VEX_TRACKED_MMAP_MAX_BYTES) {
            span.id = 103;
            buffer = kmalloc(aligned_length);
            if (buffer == NULL) {
                return (uint64_t)MAP_FAILED;
            }
            memset(buffer, 0, aligned_length);
            /* `buffer` is a temporary kernel allocation owned by this syscall.
             * Never let a read-only mapping retain it as ORA_DIRECT after the
             * materialisation path frees the staging buffer. */
            if (process_memory_register_region(scheduler_current_process_id(), addr, aligned_length, req->prot, req->flags,
                    PROCESS_MEMORY_FLAG_NO_DIRECT, buffer) != 0) {
                kfree(buffer);
                return (uint64_t)MAP_FAILED;
            }
            if (process_memory_materialize_region(scheduler_current_process_id(), process->cr3, addr) != 0) {
                (void)process_memory_remove_region(scheduler_current_process_id(), process->cr3, addr, aligned_length);
                kfree(buffer);
                return (uint64_t)MAP_FAILED;
            }
            kfree(buffer);
            if ((req->flags & MAP_SHARED) != 0 &&
                process_memory_adopt_shared_mapping(scheduler_current_process_id(), process->cr3, addr) != 0) {
                (void)process_memory_remove_region(scheduler_current_process_id(), process->cr3, addr, aligned_length);
                return (uint64_t)MAP_FAILED;
            }
            return addr;
        }
        /* Reserve anonymous ranges without consuming physical memory: private
         * ones of any size (eagerly zeroing and copying every small mapping cost
         * ~30 ms each, all under the memory gate), shared ones above the tracked
         * limit. Private pages are process-owned; MAP_SHARED pages use a sparse
         * refcounted backing so fork/process mappings see the same page when
         * it is first materialized. */
        if (file_length == 0u && (req->flags & MAP_ANONYMOUS) != 0 &&
            (req->prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) != 0 &&
            ((req->flags & MAP_SHARED) == 0 || aligned_length > VEX_TRACKED_MMAP_MAX_BYTES)) {
            span.id = 104;
            if ((req->flags & MAP_SHARED) != 0) {
                uint64_t shared_id = 0u;
                if (process_memory_shared_create_lazy(aligned_length, &shared_id) != 0 ||
                    process_memory_shared_map_lazy(scheduler_current_process_id(),
                        process->cr3, shared_id, addr, aligned_length, 0u, req->prot) != 0) {
                    if (shared_id != 0u) {
                        process_memory_shared_release(shared_id);
                    }
                    return (uint64_t)MAP_FAILED;
                }
                /* The mapping retained one reference; drop the temporary
                 * creator reference now that the region owns the backing. */
                process_memory_shared_release(shared_id);
                return addr;
            }
            if (process_memory_register_mapping(scheduler_current_process_id(), addr,
                    aligned_length, req->prot, req->flags,
                    PROCESS_MEMORY_FLAG_VMM_DIRECT | PROCESS_MEMORY_FLAG_LAZY) != 0) {
                return (uint64_t)MAP_FAILED;
            }
            return addr;
        }
        /* PROT_NONE mappings are address-space reservations (ld.so reserves a
         * library's full span, then overwrites parts with MAP_FIXED). Don't
         * back them with physical pages — just hand back the reserved range. */
        {
            const uint32_t mapping_kind = (req->prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) == 0
                ? PROCESS_MEMORY_FLAG_RESERVATION
                : PROCESS_MEMORY_FLAG_VMM_DIRECT;
            if (process_memory_register_mapping(scheduler_current_process_id(), addr, aligned_length,
                    req->prot, req->flags, mapping_kind) != 0) {
                vmm_unmap_range(vmm_cr3_to_address_space(process->cr3), addr, aligned_length, 1u);
                return (uint64_t)MAP_FAILED;
            }
            if ((req->flags & MAP_SHARED) != 0 &&
                process_memory_adopt_shared_mapping(scheduler_current_process_id(), process->cr3, addr) != 0) {
                (void)process_memory_remove_region(scheduler_current_process_id(), process->cr3, addr, aligned_length);
                return (uint64_t)MAP_FAILED;
            }
        }
    }

    return addr;
}

static uint64_t vex_sys_memory_op(uint64_t arg1, uint64_t which) {
    profiler_span_t span __attribute__((cleanup(profiler_span_end)))=profiler_span_begin(110u+(uint32_t)(which&15u));
    kernel_guard_t memory_update __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
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

    {
        const uint64_t request_addr = (uint64_t)(uintptr_t)req->addr;
        if (req->addr == NULL || req->length == 0u ||
            !vex_user_virtual_range_valid(request_addr, req->length)) {
            return (uint64_t)-1;
        }
        if (which == VEX_SYS_MPROTECT &&
            (req->prot_or_advice & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC)) {
            return (uint64_t)-1;
        }
    }

    jit_arena = vex_find_jit_arena(scheduler_current_process_id(), 0u, (uint64_t)(uintptr_t)req->addr);
    if (jit_arena != NULL) {
        if (which == VEX_SYS_MADVISE) {
            const uint64_t addr = (uint64_t)(uintptr_t)req->addr;
            const uint64_t base = addr >= jit_arena->rx_base &&
                addr - jit_arena->rx_base < jit_arena->size
                ? jit_arena->rx_base : jit_arena->rw_base;
            if (addr < base || addr - base >= jit_arena->size ||
                req->length > jit_arena->size - (addr - base) ||
                addr % VEX_USER_PAGE_SIZE || req->length % VEX_USER_PAGE_SIZE)
                return (uint64_t)-1;
            if (req->prot_or_advice == 3) return 0; /* WILLNEED: remains demand-paged. */
            if (req->prot_or_advice != 4) return (uint64_t)-1;
            const int result = process_memory_jit_discard(jit_arena->pid,
                process->cr3, jit_arena->shared_id, (size_t)(addr - base), req->length);
            jit_arena->committed_bytes = process_memory_shared_committed_bytes(jit_arena->shared_id);
            return (uint64_t)result;
        }
        if (which == VEX_SYS_MUNMAP) {
            if ((uint64_t)(uintptr_t)req->addr != jit_arena->rw_base &&
                (uint64_t)(uintptr_t)req->addr != jit_arena->rx_base) {
                return (uint64_t)-1;
            }
            if (req->length != jit_arena->size) {
                return (uint64_t)-1;
            }
            return (uint64_t)vex_destroy_jit_arena_internal(process, jit_arena);
        }
        if (which == VEX_SYS_MPROTECT) {
            const uint64_t request_addr = (uint64_t)(uintptr_t)req->addr;
            const uint64_t arena_end_rw = jit_arena->size <= UINT64_MAX - jit_arena->rw_base
                ? jit_arena->rw_base + jit_arena->size : 0u;
            const uint64_t arena_end_rx = jit_arena->size <= UINT64_MAX - jit_arena->rx_base
                ? jit_arena->rx_base + jit_arena->size : 0u;
            const uint64_t request_end = req->length <= UINT64_MAX - request_addr
                ? request_addr + req->length : 0u;
            const int is_rw_alias = request_addr >= jit_arena->rw_base &&
                request_end != 0u && arena_end_rw != 0u && request_end <= arena_end_rw;
            const int is_rx_alias = request_addr >= jit_arena->rx_base &&
                request_end != 0u && arena_end_rx != 0u && request_end <= arena_end_rx;
            uint64_t alias_flags = VMM_PAGE_USER;

            if ((request_addr & (VEX_USER_PAGE_SIZE - 1ull)) != 0u ||
                is_rw_alias == is_rx_alias) {
                return (uint64_t)-1;
            }
            if (is_rw_alias && (req->prot_or_advice & PROT_EXEC) != 0) {
                return (uint64_t)-1;
            }
            if (is_rx_alias && (req->prot_or_advice & PROT_WRITE) != 0) {
                return (uint64_t)-1;
            }
            if ((req->prot_or_advice & (PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) {
                alias_flags |= VMM_PAGE_PRESENT;
            }
            if ((req->prot_or_advice & PROT_WRITE) != 0) {
                alias_flags |= VMM_PAGE_WRITABLE;
            }
            if ((req->prot_or_advice & PROT_EXEC) == 0) {
                alias_flags |= VMM_PAGE_NX;
            }
            if(process_memory_protect_region(scheduler_current_process_id(),request_addr,req->length,req->prot_or_advice)!=0)return (uint64_t)-1;
            for (uint64_t page = request_addr;
                 page < request_end;
                 page += VEX_USER_PAGE_SIZE) {
                /* Untouched lazy pages have no PTE yet.  Their protection is
                   carried by process_memory and will be applied at first
                   materialization; only existing leaves need a flag update. */
                if (vmm_get_physical_any(pml4, page) != 0u &&
                    vmm_set_page_flags(pml4, page, alias_flags) != 0) {
                    return (uint64_t)-1;
                }
            }
            return 0;
        }
    }

    if (which == VEX_SYS_MUNMAP) {
        const uint64_t addr = (uint64_t)(uintptr_t)req->addr;
        const size_t aligned_length = (size_t)align_up_u64(req->length, VEX_USER_PAGE_SIZE);

        if (addr == 0u || req->length == 0u || (addr & (VEX_USER_PAGE_SIZE - 1ull)) != 0u ||
            aligned_length == 0u || !vex_user_virtual_range_valid(addr, aligned_length)) {
            return (uint64_t)-1;
        }
        if (process_memory_remove_region(scheduler_current_process_id(), process->cr3, addr, aligned_length) == 0) {
            return 0;
        }

        /* Every mmap/JIT/reservation created by the native path is now in the
         * namespace inventory.  Refuse a cross-region partial unmap instead
         * of unmapping PTEs behind the inventory's back. */
        if (!process_memory_range_is_free(process, addr, aligned_length)) {
            return (uint64_t)-1;
        }

        /* Legacy PE mappings not registered in process_memory are owned by
           their address space. Inventory-owned mappings never use this path. */
        vmm_unmap_range(pml4, addr, aligned_length, 1u);
        return 0;
    }
    if (which == VEX_SYS_MPROTECT) {
        const uint64_t start = (uint64_t)(uintptr_t)req->addr;
        const uint64_t end = align_up_u64(start + req->length, VEX_USER_PAGE_SIZE);
        int lazy_range=process_memory_region_is_lazy(process,start,end>start?end-start:0);
        int inventoried=process_memory_region_contains(process,start,end>start?end-start:0);


        if ((start & (VEX_USER_PAGE_SIZE - 1ull)) != 0u || end == 0u || end < start ||
            !vex_user_virtual_range_valid(start, end - start)) {
            return (uint64_t)-1;
        }
        if (!process_memory_protection_allowed(scheduler_current_process_id(),
                start, (size_t)(end - start), req->prot_or_advice)) {
            return (uint64_t)-1;
        }

        /* A protection change must never replace a retained PROT_NONE page
           with a newly zeroed page. Validate existing non-lazy leaves first. */
        if(!inventoried)
            for(uint64_t page=start;page<end;page+=4096)
                if(!vmm_get_physical_any(pml4,page) || !(vmm_get_page_flags(pml4,page) & VMM_PAGE_USER))return (uint64_t)-1;
        /* Detach every immutable view touched by a writable range, not just
           its first region. No immutable physical page can acquire WRITE. */
        if((req->prot_or_advice & PROT_WRITE) &&
           process_memory_prepare_write(scheduler_current_process_id(),process->cr3,start,end-start)!=0 &&
           inventoried)return (uint64_t)-1;
        /* Prepare/publish inventory while OOM can still be reported without
           changing any PTE permissions. Existing PTE flag writes allocate no
           tables; untouched lazy pages remain absent. */
        if(process_memory_protect_region(scheduler_current_process_id(),start,end-start,req->prot_or_advice)!=0 &&
           !process_memory_range_is_free(process,start,end-start))return (uint64_t)-1;

        lazy_range=process_memory_region_is_lazy(process,start,end-start);
        flags = VMM_PAGE_USER;
        if ((req->prot_or_advice & (PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) {
            flags |= VMM_PAGE_PRESENT;
        }
        if ((req->prot_or_advice & PROT_WRITE) != 0) {
            flags |= VMM_PAGE_WRITABLE;
        }
        if ((req->prot_or_advice & PROT_EXEC) == 0) {
            flags |= VMM_PAGE_NX;
        }

        if (inventoried) {
            if (vmm_set_page_flags_range(pml4, start, end - start, flags) != 0) {
                return (uint64_t)-1;
            }
        } else for (uint64_t page=start;page<end;page+=4096) {
            if(!vmm_get_physical_any(pml4,page)) {
                if(lazy_range)continue;
                return (uint64_t)-1;
            }
            if(vmm_set_page_flags(pml4,page,flags))return (uint64_t)-1;
        }
        return 0;
    }
    return 0;
}

static uint64_t vex_sys_heap_alloc(uint64_t arg1) {
    kernel_guard_t memory_update __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    process_t *process=scheduler_current_process();size_t size=(size_t)arg1;
    size_t length=(size_t)align_up_u64(size,VEX_USER_PAGE_SIZE);
    if(!process || !size || !length)return 0;
    uint64_t address=vex_map_user_memory(process,length,0);
    if(!address)return 0;
    /* Heap identity and logical size belong to the memory inventory. The
       lazy mutable pages are not canonical content and commit on access. */
    if(process_memory_register_mapping(scheduler_current_process_id(),address,length,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS,PROCESS_MEMORY_FLAG_HEAP|PROCESS_MEMORY_FLAG_VMM_DIRECT|PROCESS_MEMORY_FLAG_LAZY)!=0)return 0;
    if(process_memory_set_allocation_size(scheduler_current_process_id(),address,size)!=0) {
        process_memory_remove_region(scheduler_current_process_id(),process->cr3,address,length);return 0;
    }
    return address;
}

static uint64_t vex_sys_heap_free(uint64_t arg1) {
    kernel_guard_t memory_update __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_heap_req_t *req=(vex_heap_req_t *)arg1;process_t *process=scheduler_current_process();process_memory_region_t region;
    if(!process || !req || !req->ptr || process_memory_get_region(scheduler_current_process_id(),(uint64_t)req->ptr,&region)!=0 ||
       !(region.kind_flags & PROCESS_MEMORY_FLAG_HEAP) || region.allocation_base!=(uint64_t)req->ptr)return (uint64_t)-1;
    return process_memory_remove_region(scheduler_current_process_id(),process->cr3,region.allocation_base,align_up_u64(region.allocation_size,4096));
}

static uint64_t vex_sys_heap_realloc(uint64_t arg1) {
    kernel_guard_t memory_update __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_heap_req_t *req=(vex_heap_req_t *)arg1;process_t *process=scheduler_current_process();process_memory_region_t old;
    if(!process || !req)return 0;
    if(!req->size){if(req->ptr)vex_sys_heap_free(arg1);return 0;}
    if(!req->ptr)return vex_sys_heap_alloc(req->size);
    uint32_t owner=scheduler_current_process_id();
    if(process_memory_get_region(owner,(uint64_t)req->ptr,&old)!=0 || !(old.kind_flags & PROCESS_MEMORY_FLAG_HEAP) || old.allocation_base!=(uint64_t)req->ptr)return 0;
    uint64_t address=vex_sys_heap_alloc(req->size);if(!address)return 0;
    size_t count=req->size<old.allocation_size?req->size:old.allocation_size;
    for(size_t offset=0;offset<count;) {
        size_t bytes=count-offset;if(bytes>4096)bytes=4096;
        if(!vex_dispatch_range_is_accessible(process,old.addr+offset,bytes) ||
           !vex_dispatch_range_is_writable(process,address+offset,bytes))goto fail;
        memcpy((void *)(address+offset),(void *)(old.addr+offset),bytes);offset+=bytes;
    }
    if(process_memory_remove_region(owner,process->cr3,old.allocation_base,align_up_u64(old.allocation_size,4096))!=0)goto fail;
    return address;
fail:
    {process_memory_region_t replacement;if(process_memory_get_region(owner,address,&replacement)==0)
        process_memory_remove_region(owner,process->cr3,address,replacement.length);}
    return 0;
}

static uint64_t vex_sys_mrac_put(uint64_t arg1) {
    kernel_guard_t transaction __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_mrac_put_req_t copy;
    vex_mrac_put_req_t *req=&copy;
    process_t *process = scheduler_current_process();
    if(!process || !vex_request_writable(process,arg1,sizeof(copy)))return (uint64_t)-1;
    memcpy(&copy,(const void *)(uintptr_t)arg1,sizeof(copy));
    if (process == NULL || req == NULL || req->data == NULL || req->size == 0u) {
        return (uint64_t)-1;
    }
    if (!vex_dispatch_range_is_accessible(process,
            (uint64_t)(uintptr_t)req->data, req->size)) {
        return (uint64_t)-1;
    }

    if(versioned_put(scheduler_current_process_id(), req->key_hash,
        req->data, req->size, &req->version_out))return (uint64_t)-1;
    ((vex_mrac_put_req_t *)(uintptr_t)arg1)->version_out=req->version_out;
    return 0;
}

static uint64_t vex_sys_mrac_get(uint64_t arg1) {
    kernel_guard_t transaction __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_mrac_get_req_t copy;
    vex_mrac_get_req_t *req=&copy;
    process_t *process=scheduler_current_process();
    if(!process || !vex_request_writable(process,arg1,sizeof(copy)))return (uint64_t)-1;
    memcpy(&copy,(const void *)(uintptr_t)arg1,sizeof(copy));
    if(!process || !req)return (uint64_t)-1;
    versioned_info_t info;
    ora_ref_t content=versioned_acquire(scheduler_current_process_id(),req->key_hash,req->version,&info);
    if(!content)return (uint64_t)-1;
    ((vex_mrac_get_req_t *)(uintptr_t)arg1)->resolved_version=info.version;
    ((vex_mrac_get_req_t *)(uintptr_t)arg1)->size_out=info.size;
    if(!req->buffer || req->buffer_size<info.size ||
       !vex_dispatch_range_is_writable(process,(uint64_t)(uintptr_t)req->buffer,info.size)) {
        ora_release(content);return (uint64_t)-1;
    }
    memcpy(req->buffer,ora_data(content),info.size);
    ora_release(content);return 0;
}

static uint64_t vex_sys_mrac_remove(uint64_t arg1) {
    kernel_guard_t transaction __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_mrac_remove_req_t copy;
    vex_mrac_remove_req_t *req=&copy;
    process_t *process = scheduler_current_process();
    if(!process || !vex_request_readable(process,arg1,sizeof(copy)))return (uint64_t)-1;
    memcpy(&copy,(const void *)(uintptr_t)arg1,sizeof(copy));

    if (process == NULL || req == NULL) {
        return (uint64_t)-1;
    }

    if ((req->flags & VEX_MRAC_REMOVE_ALL) != 0u) {
        versioned_remove_all(scheduler_current_process_id(), req->key_hash);
        return 0;
    }

    if ((req->flags & VEX_MRAC_REMOVE_CURRENT) != 0u || req->version == VEX_MRAC_VERSION_CURRENT) {
        return versioned_remove(scheduler_current_process_id(), req->key_hash, 0) == 0 ? 0 : (uint64_t)-1;
    }

    return versioned_remove(scheduler_current_process_id(), req->key_hash, req->version) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t vex_sys_mrac_version_info(uint64_t arg1) {
    kernel_guard_t transaction __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_mrac_version_info_req_t copy;
    vex_mrac_version_info_req_t *req=&copy;
    process_t *process = scheduler_current_process();
    versioned_info_t info;
    if(!process || !vex_request_readable(process,arg1,sizeof(copy)))return (uint64_t)-1;
    memcpy(&copy,(const void *)(uintptr_t)arg1,sizeof(copy));

    if (process == NULL || req == NULL || req->info == NULL ||
        !vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->info, sizeof(*req->info))) {
        return (uint64_t)-1;
    }

    if (versioned_info_at(scheduler_current_process_id(), req->key_hash, req->index, &info) != 0) {
        return (uint64_t)-1;
    }

    req->info->version = info.version;
    req->info->size = info.size;
    req->info->is_current = info.is_current;
    return 0;
}

static uint64_t vex_sys_poll(uint64_t arg1) {
    vex_poll_req_t *req = (vex_poll_req_t *)arg1;
    process_t *process = scheduler_current_process();
    size_t bytes;
    if (req == NULL) {
        return (uint64_t)-1;
    }
    if ((uint64_t)req->count > SIZE_MAX / sizeof(*req->fds)) {
        return (uint64_t)-1;
    }
    bytes = (size_t)req->count * sizeof(*req->fds);
    if (!vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->fds, bytes)) {
        return (uint64_t)-1;
    }
    if(process && process->user_mode && (uint64_t)(uintptr_t)req->fds<VMM_USER_ADDRESS_LIMIT) {
        VEX_SCRATCH(temporary);
        struct pollfd local[64];
        struct pollfd *fds=bytes<=sizeof(local)?local:vex_scratch_allocate(&temporary,bytes);
        if(!fds){*ladybird_errno_location()=ENOMEM;return (uint64_t)-1;}
        if(vex_copy_from_user(fds,(uint64_t)(uintptr_t)req->fds,bytes)){
            *ladybird_errno_location()=EFAULT;return (uint64_t)-1;}
        int result=vex_poll_kernel(fds,req->count,req->timeout);
        if(result>=0 && vex_copy_to_user((uint64_t)(uintptr_t)req->fds,fds,bytes)){
            *ladybird_errno_location()=EFAULT;return (uint64_t)-1;}
        return (uint64_t)result;
    }
    return (uint64_t)poll(req->fds,req->count,req->timeout);
}

static uint64_t vex_sys_time(uint64_t arg1, uint64_t which) {
    process_t *process = scheduler_current_process();
    if (which == VEX_SYS_GETTIMEOFDAY) {
        kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
        vex_gettimeofday_req_t *req = (vex_gettimeofday_req_t *)arg1;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        if (!vex_dispatch_range_is_writable(process,
                (uint64_t)(uintptr_t)req->tv, sizeof(*req->tv))) {
            return (uint64_t)-1;
        }
        if (req->tz != NULL && !vex_dispatch_range_is_writable(process,
                (uint64_t)(uintptr_t)req->tz, sizeof(int32_t) * 2u)) {
            return (uint64_t)-1;
        }
        return (uint64_t)gettimeofday(req->tv, req->tz);
    }

    if (which == VEX_SYS_CLOCK_GETTIME) {
        kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
        vex_clock_gettime_req_t *req = (vex_clock_gettime_req_t *)arg1;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        if (!vex_dispatch_range_is_writable(process,
                (uint64_t)(uintptr_t)req->ts, sizeof(*req->ts))) {
            return (uint64_t)-1;
        }
        return (uint64_t)clock_gettime(req->clock_id, req->ts);
    }

    {
        vex_nanosleep_req_t *req = (vex_nanosleep_req_t *)arg1;
        struct timespec requested,remaining={0};
        if (req == NULL) {
            return (uint64_t)-1;
        }
        {kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
        if (!vex_dispatch_range_is_accessible(process,
                (uint64_t)(uintptr_t)req->requested, sizeof(*req->requested)) ||
            (req->remaining != NULL && !vex_dispatch_range_is_writable(process,
                (uint64_t)(uintptr_t)req->remaining, sizeof(*req->remaining)))) {
            *ladybird_errno_location()=EFAULT;return (uint64_t)-1;
        }
        memcpy(&requested,req->requested,sizeof(requested));}
        int result=nanosleep(&requested,req->remaining?&remaining:NULL);
        if(req->remaining) {
            kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
            if(!vex_dispatch_range_is_writable(process,(uint64_t)(uintptr_t)req->remaining,sizeof(remaining))) {
                *ladybird_errno_location()=EFAULT;return (uint64_t)-1;
            }
            memcpy(req->remaining,&remaining,sizeof(remaining));
        }
        return (uint64_t)result;
    }
}

static uint64_t vex_sys_path_buffer(uint64_t arg1, uint64_t which) {
    process_t *process = scheduler_current_process();
    if (which == VEX_SYS_READLINK) {
        vex_readlink_req_t *req = (vex_readlink_req_t *)arg1;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        if ((process != NULL && process->user_mode != 0u &&
             !vex_dispatch_user_string_is_valid(process,
                 (uint64_t)(uintptr_t)req->path, 4096u)) ||
            !vex_dispatch_range_is_writable(process,
                (uint64_t)(uintptr_t)req->buffer, req->size)) {
            return (uint64_t)-1;
        }
        return (uint64_t)readlink(req->path, req->buffer, req->size);
    }

    if (which == VEX_SYS_SYMLINK) {
        vex_symlink_req_t *req = (vex_symlink_req_t *)arg1;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        if ((process != NULL && process->user_mode != 0u &&
             (!vex_dispatch_user_string_is_valid(process,
                 (uint64_t)(uintptr_t)req->target, 4096u) ||
              !vex_dispatch_user_string_is_valid(process,
                 (uint64_t)(uintptr_t)req->link_path, 4096u)))) {
            return (uint64_t)-1;
        }
        return (uint64_t)symlink(req->target, req->link_path);
    }

    {
        vex_realpath_req_t *req = (vex_realpath_req_t *)arg1;
        if (req == NULL) {
            return 0;
        }
        if ((process != NULL && process->user_mode != 0u &&
             !vex_dispatch_user_string_is_valid(process,
                 (uint64_t)(uintptr_t)req->path, 4096u)) ||
            !vex_dispatch_range_is_writable(process,
                (uint64_t)(uintptr_t)req->resolved_path, 4096u)) {
            return 0;
        }
        return (uint64_t)(uintptr_t)realpath(req->path, req->resolved_path);
    }
}

static uint64_t vex_sys_env(uint64_t arg1, uint64_t arg2, uint64_t which) {
    process_t *process = scheduler_current_process();
    (void)arg2;

    if (which == VEX_SYS_GETENV) {
        if (process != NULL && process->user_mode != 0u &&
            !vex_dispatch_user_string_is_valid(process, arg1, 4096u)) {
            return 0;
        }
        return (uint64_t)(uintptr_t)getenv((char const *)arg1);
    }
    if (which == VEX_SYS_SETENV) {
        vex_setenv_req_t *req = (vex_setenv_req_t *)arg1;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        if ((process != NULL && process->user_mode != 0u &&
             (!vex_dispatch_user_string_is_valid(process,
                 (uint64_t)(uintptr_t)req->name, 4096u) ||
              !vex_dispatch_user_string_is_valid(process,
                 (uint64_t)(uintptr_t)req->value, 4096u)))) {
            return (uint64_t)-1;
        }
        return (uint64_t)setenv(req->name, req->value, req->overwrite);
    }
    if (which == VEX_SYS_UNSETENV) {
        if (process != NULL && process->user_mode != 0u &&
            !vex_dispatch_user_string_is_valid(process, arg1, 4096u)) {
            return (uint64_t)-1;
        }
        return (uint64_t)unsetenv((char const *)arg1);
    }
    if (which == VEX_SYS_SYSCONF) {
        return (uint64_t)sysconf((int)arg1);
    }
    return vex_request_writable(process,arg1,sizeof(struct utsname))
        ? (uint64_t)uname((void *)arg1) : (uint64_t)-1;
}

static uint64_t vex_sys_getrandom(uint64_t arg1) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_getrandom_req_t *req = (vex_getrandom_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL) {
        return (uint64_t)-1;
    }
    if (!vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->buffer, req->length)) {
        return (uint64_t)-1;
    }
    return (uint64_t)getrandom(req->buffer, req->length, req->flags);
}

static uint64_t vex_sys_sockaddr(uint64_t arg1, uint64_t which) {
    vex_sockaddr_req_t *req = (vex_sockaddr_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL) {
        return (uint64_t)-1;
    }
    if (req->addrlen > 4096u) {
        return (uint64_t)-1;
    }
    uint8_t address[4096];
    {kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
     if (!vex_dispatch_range_is_accessible(process,
             (uint64_t)(uintptr_t)req->addr, req->addrlen)) {
         *ladybird_errno_location()=EFAULT;return (uint64_t)-1;
     }
     memcpy(address,req->addr,req->addrlen);
    }

    if (which == VEX_SYS_CONNECT) {
        return (uint64_t)connect(req->sockfd, (const struct sockaddr *)address, req->addrlen);
    }
    return (uint64_t)bind(req->sockfd, (const struct sockaddr *)address, req->addrlen);
}

static uint64_t vex_sys_send_recv(uint64_t arg1, uint64_t which) {
    vex_send_recv_req_t *req = (vex_send_recv_req_t *)arg1;
    process_t *process = scheduler_current_process();
    VEX_SCRATCH(temporary);
    uint8_t local[1024];
    if (req == NULL) {
        return (uint64_t)-1;
    }

    int user_buffer=process && process->user_mode && (uint64_t)(uintptr_t)req->buffer<VMM_USER_ADDRESS_LIMIT;
    if(user_buffer) {
        void *bytes=req->length<=sizeof(local)?local:vex_scratch_allocate(&temporary,req->length);
        if(!bytes){*ladybird_errno_location()=ENOMEM;return (uint64_t)-1;}
        {kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
         if(!vex_syscall_user_range_valid((uint64_t)(uintptr_t)req->buffer,req->length,which!=VEX_SYS_SEND)){
            *ladybird_errno_location()=EFAULT;return (uint64_t)-1;}}
        if(which==VEX_SYS_SEND && vex_copy_from_user(bytes,(uint64_t)(uintptr_t)req->buffer,req->length)){
            *ladybird_errno_location()=EFAULT;return (uint64_t)-1;}
        ssize_t n=which==VEX_SYS_SEND?send(req->sockfd,bytes,req->length,req->flags):recv(req->sockfd,bytes,req->length,req->flags);
        if(which!=VEX_SYS_SEND && n>0 && vex_copy_to_user((uint64_t)(uintptr_t)req->buffer,bytes,(size_t)n)){
            *ladybird_errno_location()=EFAULT;return (uint64_t)-1;}
        return (uint64_t)n;
    }
    if (which == VEX_SYS_SEND) {
        if (!vex_dispatch_range_is_accessible(process,
                (uint64_t)(uintptr_t)req->buffer, req->length)) {
            return (uint64_t)-1;
        }
        return (uint64_t)send(req->sockfd, req->buffer, req->length, req->flags);
    }
    if (!vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->buffer, req->length)) {
        return (uint64_t)-1;
    }
    return (uint64_t)recv(req->sockfd, req->buffer, req->length, req->flags);
}

static uint64_t vex_sys_connect_hostname(uint64_t arg1) {
    vex_connect_hostname_req_t *req = (vex_connect_hostname_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL) {
        return (uint64_t)-1;
    }
    if (process != NULL && process->user_mode != 0u &&
        !vex_dispatch_user_string_is_valid(process,
            (uint64_t)(uintptr_t)req->hostname, 256u)) {
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
    vex_accept_req_t *req=(vex_accept_req_t *)(uintptr_t)arg1;
    process_t *process=scheduler_current_process();
    uint32_t capacity=0,actual=0;
    uint8_t address[4096];
    if(!req)return (uint64_t)-1;
    {kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
     if(req->addrlen) {
        if(!vex_dispatch_range_is_writable(process,(uint64_t)(uintptr_t)req->addrlen,sizeof(capacity))){*ladybird_errno_location()=EFAULT;return (uint64_t)-1;}
        memcpy(&capacity,req->addrlen,sizeof(capacity));
        if(capacity>sizeof(address)){*ladybird_errno_location()=EINVAL;return (uint64_t)-1;}
        if(!vex_dispatch_range_is_writable(process,(uint64_t)(uintptr_t)req->addr,capacity)){*ladybird_errno_location()=EFAULT;return (uint64_t)-1;}
     }else if(req->addr){*ladybird_errno_location()=EINVAL;return (uint64_t)-1;}}
    actual=capacity;
    int fd=accept(req->sockfd,req->addrlen?(struct sockaddr *)address:NULL,req->addrlen?&actual:NULL);
    if(fd>=0 && req->addrlen) {
        kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
        size_t copied=actual<capacity?actual:capacity;
        if(!vex_dispatch_range_is_writable(process,(uint64_t)(uintptr_t)req->addrlen,sizeof(actual)) ||
           !vex_dispatch_range_is_writable(process,(uint64_t)(uintptr_t)req->addr,copied)) {
            close(fd);*ladybird_errno_location()=EFAULT;return (uint64_t)-1;
        }
        if(copied)memcpy(req->addr,address,copied);
        memcpy(req->addrlen,&actual,sizeof(actual));
    }
    return (uint64_t)fd;
}
static uint64_t vex_sys_sockopt(uint64_t arg1, uint64_t which) {
    /* Socket options never wait: keep nested user fields stable throughout
       the short metadata operation, in memory -> network lock order. */
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_sockopt_req_t *req = (vex_sockopt_req_t *)arg1;
    process_t *process = scheduler_current_process();
    uint32_t option_len = 0u;
    if (req == NULL) {
        return (uint64_t)-1;
    }

    if (req->option_len != NULL) {
        if (!vex_dispatch_range_is_writable(process,
                (uint64_t)(uintptr_t)req->option_len, sizeof(*req->option_len))) {
            return (uint64_t)-1;
        }
        option_len = *req->option_len;
    }
    if (option_len > 4096u) {
        return (uint64_t)-1;
    }
    if (which == VEX_SYS_GETSOCKOPT && req->option_len == NULL) {
        return (uint64_t)-1;
    }
    if (!vex_dispatch_range_is_accessible(process,
            (uint64_t)(uintptr_t)req->option_value, option_len)) {
        return (uint64_t)-1;
    }
    if (which == VEX_SYS_GETSOCKOPT &&
        !vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->option_value, option_len)) {
        return (uint64_t)-1;
    }

    if (which == VEX_SYS_SETSOCKOPT) {
        return (uint64_t)setsockopt(req->sockfd, req->level, req->option_name, req->option_value, option_len);
    }

    return (uint64_t)getsockopt(req->sockfd, req->level, req->option_name, req->option_value, req->option_len);
}

static uint64_t vex_sys_socketpair(uint64_t arg1) {
    kernel_guard_t copy_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_socketpair_req_t *req = (vex_socketpair_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL) {
        return (uint64_t)-1;
    }
    if (!vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->sv, 2u * sizeof(*req->sv))) {
        return (uint64_t)-1;
    }
    return (uint64_t)socketpair(req->domain, req->type, req->protocol, req->sv);
}

static uint64_t vex_sys_message_pair(uint64_t arg1) {
    kernel_guard_t copy_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_message_pair_req_t *req = (vex_message_pair_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if (req == NULL || req->fds == NULL ||
        !vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->fds, 2u * sizeof(*req->fds))) {
        return (uint64_t)-1;
    }
    return vex_native_message_pair(req->flags, req->fds) == 0 ? 0u : (uint64_t)-1;
}

static uint64_t vex_sys_message_io(uint64_t arg1, uint64_t which) {
    vex_message_io_req_t *req = (vex_message_io_req_t *)arg1;
    process_t *process = scheduler_current_process();
    vex_message_attachment_t attachments[VEX_MESSAGE_MAX_ATTACHMENTS];
    vex_message_attachment_req_t requested[VEX_MESSAGE_MAX_ATTACHMENTS];
    vex_message_attachment_req_t received[VEX_MESSAGE_MAX_ATTACHMENTS];
    size_t attachment_count;
    ssize_t result;
    const int user=process && process->user_mode && process->in_user_syscall;
    const uint64_t output_request=user?process->native_request_user_address:arg1;
    VEX_SCRATCH(payload_lease);
    uint8_t local_payload[512];void *payload=req?req->buffer:NULL;

    if (req == NULL || process == NULL ||
        (req->buffer == NULL && req->length != 0u) ||
        req->attachment_count > VEX_MESSAGE_MAX_ATTACHMENTS) {
        return (uint64_t)-EINVAL;
    }
    const size_t copied_length=which==VEX_SYS_MESSAGE_SEND?req->length:(req->length>65536?65536:req->length);
    if(which==VEX_SYS_MESSAGE_SEND && req->length>65536)return (uint64_t)-EMSGSIZE;
    if(user) {
        payload=copied_length<=sizeof(local_payload)?local_payload:vex_scratch_allocate(&payload_lease,copied_length);
        if(!payload)return (uint64_t)-ENOMEM;
        if(which==VEX_SYS_MESSAGE_SEND && copied_length && vex_copy_from_user(payload,(uintptr_t)req->buffer,copied_length))return (uint64_t)-EFAULT;
    }
    if (which == VEX_SYS_MESSAGE_SEND) {
        if (!vex_dispatch_range_is_accessible(process,
                (uint64_t)(uintptr_t)req->buffer, req->length) ||
            (req->attachment_count != 0u &&
             !vex_dispatch_range_is_accessible(process,
                (uint64_t)(uintptr_t)req->attachments,
                req->attachment_count * sizeof(requested[0])))) {
            return (uint64_t)-EFAULT;
        }
        if (req->attachment_count == 0u) {
            result=send(req->fd, payload, req->length,(int)req->flags);
            return result<0?(uint64_t)-(int64_t)*ladybird_errno_location():(uint64_t)result;
        }
        attachment_count = req->attachment_count;
        if(user) {if(vex_copy_from_user(requested,(uintptr_t)req->attachments,attachment_count*sizeof(requested[0])))return (uint64_t)-EFAULT;}
        else memcpy(requested,req->attachments,attachment_count*sizeof(requested[0]));
        memset(attachments, 0, sizeof(attachments));
        for (size_t i = 0u; i < attachment_count; ++i) {
            if (requested[i].type == VEX_MESSAGE_OBJECT_FD) {
                if (requested[i].handle > INT32_MAX ||
                    requested[i].rights != VEX_MESSAGE_ATTACHMENT_DUP) {
                    result = -EINVAL;
                    goto send_attachment_fail;
                }
                attachments[i].object = ladybird_fd_acquire((int)requested[i].handle);
                if (attachments[i].object == NULL) {
                    result = -EBADF;
                    goto send_attachment_fail;
                }
                attachments[i].rights = VEX_MESSAGE_ATTACHMENT_DUP;
                attachments[i].type = VEX_MESSAGE_ATTACHMENT_FD;
                int leased=vex_ultra_export_endpoint((vex_web_fd_object_t **)&attachments[i].object);
                if(leased){result=leased;goto send_attachment_fail;}
            } else if (requested[i].type == VEX_MESSAGE_OBJECT_SURFACE) {
                const int exported = compositor_surface_export(
                    scheduler_current_process_id(),
                    requested[i].handle, requested[i].rights, &attachments[i]);
                if (exported != COMPOSITOR_SURFACE_OK) {
                    result = exported == COMPOSITOR_SURFACE_ERROR_ACCESS
                        ? -EACCES : -EINVAL;
                    goto send_attachment_fail;
                }
            } else {
                result = -EINVAL;
                goto send_attachment_fail;
            }
        }
        result = vex_socketpair_send_attached(req->fd, payload, req->length,
            (int)req->flags, attachments, attachment_count);
        if (result >= 0) {
            return (uint64_t)result;
        }
send_attachment_fail:
        if(result==-1)result=-*ladybird_errno_location();
        for (size_t i = 0u; i < attachment_count; ++i) {
            vex_message_attachment_release(&attachments[i]);
        }
        return (uint64_t)result;
    }
    if (!vex_request_writable(process,arg1,sizeof(*req)) ||
        !vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->buffer, req->length) ||
        (req->attachment_count != 0u &&
         !vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->attachments,
            req->attachment_count * sizeof(requested[0])))) {
        return (uint64_t)-EFAULT;
    }
    if (req->attachment_count == 0u && which!=VEX_SYS_MESSAGE_RECV_AUTH) {
        result=recv(req->fd,payload,copied_length,(int)req->flags);
        if(result<0)return (uint64_t)-(int64_t)*ladybird_errno_location();
        kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
        if(user && ((result>0 && vex_copy_to_user((uintptr_t)req->buffer,payload,(size_t)result)) ||
           vex_copy_to_user(output_request,req,sizeof(*req))))return (uint64_t)-EFAULT;
        return (uint64_t)result;
    }
    attachment_count = req->attachment_count;
    if(user) {if(vex_copy_from_user(requested,(uintptr_t)req->attachments,attachment_count*sizeof(requested[0])))return (uint64_t)-EFAULT;}
    else memcpy(requested,req->attachments,attachment_count*sizeof(requested[0]));
    memset(attachments, 0, sizeof(attachments));
    vex_message_sender_t sender={0};
    result = vex_socketpair_receive_authenticated(req->fd, payload, copied_length,
        (int)req->flags, attachments, &attachment_count,&sender);
    if (result < 0 || attachment_count > req->attachment_count) {
        return result<0?(uint64_t)-(int64_t)*ladybird_errno_location():(uint64_t)-EIO;
    }
    memset(received, 0, sizeof(received));
    for (size_t i = 0u; i < attachment_count; ++i) {
        if(requested[i].type && requested[i].type!=attachments[i].type){result=-EINVAL;goto receive_attachment_fail;}
        if (attachments[i].type == VEX_MESSAGE_ATTACHMENT_FD) {
            const int fd = ladybird_fd_install(
                (ladybird_fd_object_t *)attachments[i].object, -1);
            attachments[i].object = NULL; /* install consumes on every path */
            if (fd < 0) {
                result = -EMFILE;
                goto receive_attachment_fail;
            }
            received[i].handle = (uint64_t)(uint32_t)fd;
            received[i].type = VEX_MESSAGE_OBJECT_FD;
            received[i].rights = VEX_MESSAGE_ATTACHMENT_DUP;
        } else if (attachments[i].type == VEX_MESSAGE_ATTACHMENT_SURFACE) {
            const uint32_t rights = requested[i].rights == 0u
                ? attachments[i].rights : requested[i].rights;
            compositor_surface_handle_t handle;
            const int imported = compositor_surface_import_attachment(
                scheduler_current_process_id(),
                &attachments[i], rights, &handle);
            if (imported != COMPOSITOR_SURFACE_OK) {
                result = imported == COMPOSITOR_SURFACE_ERROR_ACCESS
                    ? -EACCES : -EMFILE;
                goto receive_attachment_fail;
            }
            received[i].handle = handle;
            received[i].type = VEX_MESSAGE_OBJECT_SURFACE;
            received[i].rights = rights;
            vex_message_attachment_release(&attachments[i]);
        } else {
            result = -EINVAL;
            goto receive_attachment_fail;
        }
    }
    {
        kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
        req->attachment_count=attachment_count;
        size_t output_size=which==VEX_SYS_MESSAGE_RECV_AUTH?sizeof(vex_message_auth_req_t):sizeof(*req);
        if(which==VEX_SYS_MESSAGE_RECV_AUTH)((vex_message_auth_req_t *)req)->sender=sender;
        if(user) {
            if(!vex_user_range_is_writable(process,output_request,output_size) ||
               (result>0 && vex_copy_to_user((uintptr_t)req->buffer,payload,(size_t)result)) ||
               (attachment_count && vex_copy_to_user((uintptr_t)req->attachments,received,attachment_count*sizeof(received[0]))) ||
               vex_copy_to_user(output_request,req,output_size)) {result=-EFAULT;goto receive_attachment_fail;}
        }else {
            memcpy(req->attachments,received,attachment_count*sizeof(received[0]));
        }
    }
    return (uint64_t)result;

receive_attachment_fail:
    for (size_t i = 0u; i < attachment_count; ++i) {
        if (attachments[i].object != NULL) {
            vex_message_attachment_release(&attachments[i]);
        }
        if (received[i].type == VEX_MESSAGE_OBJECT_FD) {
            (void)close((int)received[i].handle);
        } else if (received[i].type == VEX_MESSAGE_OBJECT_SURFACE) {
            (void)compositor_surface_release(scheduler_current_process_id(),
                                             received[i].handle);
        }
    }
    return (uint64_t)result;
}

static uint64_t vex_app_surface_present_copy(process_t *process, app_surface_t *surface,
                                             const uint32_t *pixels, uint32_t width,
                                             uint32_t height, uint32_t stride,
                                             const vex_rect_t *dirty_user) {
    vex_rect_t dirty;
    uint32_t copy_x = 0u;
    uint32_t copy_y = 0u;
    uint32_t copy_w;
    uint32_t copy_h;
    size_t source_bytes;
    uint8_t use_partial = 0u;

    if (process == NULL || surface == NULL || surface->pixels == NULL ||
        pixels == NULL || width == 0u || height == 0u || stride < width) {
        return (uint64_t)-1;
    }
    if (surface->width != width || surface->height != height) {
        return (uint64_t)-1;
    }
    if ((uint64_t)stride > SIZE_MAX / sizeof(uint32_t) ||
        (uint64_t)height > SIZE_MAX / ((size_t)stride * sizeof(uint32_t))) {
        return (uint64_t)-1;
    }

    source_bytes = (size_t)stride * (size_t)height * sizeof(uint32_t);
    if (!vex_user_range_is_accessible(process, (uint64_t)(uintptr_t)pixels, source_bytes)) {
        return (uint64_t)-1;
    }

    copy_w = width;
    copy_h = height;
    if (dirty_user != NULL &&
        vex_user_range_is_accessible(process, (uint64_t)(uintptr_t)dirty_user, sizeof(dirty))) {
        memcpy(&dirty, dirty_user, sizeof(dirty));
        if (dirty.x >= 0 &&
            dirty.y >= 0 &&
            dirty.w > 0 &&
            dirty.h > 0 &&
            (uint32_t)dirty.x < width &&
            (uint32_t)dirty.y < height &&
            (uint32_t)dirty.w <= width - (uint32_t)dirty.x &&
            (uint32_t)dirty.h <= height - (uint32_t)dirty.y) {
            copy_x = (uint32_t)dirty.x;
            copy_y = (uint32_t)dirty.y;
            copy_w = (uint32_t)dirty.w;
            copy_h = (uint32_t)dirty.h;
            use_partial = 1u;
        }
    }

    if (use_partial) {
        for (uint32_t row = copy_y; row < copy_y + copy_h; ++row) {
            memcpy(
                surface->pixels + (size_t)row * surface->stride + copy_x,
                pixels + (size_t)row * stride + copy_x,
                (size_t)copy_w * sizeof(uint32_t)
            );
        }
    } else {
        for (uint32_t y = 0; y < height; ++y) {
            memcpy(
                surface->pixels + (size_t)y * surface->stride,
                pixels + (size_t)y * stride,
                (size_t)width * sizeof(uint32_t)
            );
        }
    }

    return app_surface_present(surface, use_partial ? &dirty : NULL) == 0
        ? 0u : (uint64_t)-1;
}

static uint64_t vex_sys_display_present(uint64_t arg1) {
    GUI_GUARD;
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

#if VEX_TRACE_BROWSER_PRESENT
    debug_log_puts("KRN: display_present enter\n");
#endif

    if (process == NULL) {
#if VEX_TRACE_BROWSER_PRESENT
        debug_log_puts("KRN: dp err=no_process\n");
#endif
        return (uint64_t)-1;
    }
    if (req_user == NULL) {
#if VEX_TRACE_BROWSER_PRESENT
        debug_log_puts("KRN: dp err=req_null\n");
#endif
        return (uint64_t)-1;
    }
    if (!vex_request_readable(process,(uint64_t)(uintptr_t)req_user,sizeof(req))) {
#if VEX_TRACE_BROWSER_PRESENT
        debug_log_puts("KRN: dp err=req_inaccessible\n");
#endif
        return (uint64_t)-1;
    }

    memcpy(&req, req_user, sizeof(req));

    if (req.pixels == NULL) {
#if VEX_TRACE_BROWSER_PRESENT
        debug_log_puts("KRN: dp err=pixels_null\n");
#endif
        return (uint64_t)-1;
    }
    if (req.width == 0u || req.height == 0u || req.stride < req.width) {
#if VEX_TRACE_BROWSER_PRESENT
        debug_log_puts("KRN: dp err=dimensions_invalid\n");
#endif
        return (uint64_t)-1;
    }
    if ((uint64_t)req.stride > SIZE_MAX / sizeof(uint32_t) || (uint64_t)req.height > SIZE_MAX / ((size_t)req.stride * sizeof(uint32_t))) {
#if VEX_TRACE_BROWSER_PRESENT
        debug_log_puts("KRN: dp err=dimensions_overflow\n");
#endif
        return (uint64_t)-1;
    }

    source_bytes = (size_t)req.stride * (size_t)req.height * sizeof(uint32_t);
    if (!vex_user_range_is_accessible(process, (uint64_t)(uintptr_t)req.pixels, source_bytes)) {
#if VEX_TRACE_BROWSER_PRESENT
        debug_log_puts("KRN: dp err=pixels_inaccessible\n");
#endif
        return (uint64_t)-1;
    }

    surface = ladybird_display_surface();
    if (surface == NULL ||
        surface->pixels == NULL ||
        surface->width != req.width ||
        surface->height != req.height) {
        if (ladybird_display_open(req.width, req.height) != 0) {
#if VEX_TRACE_BROWSER_PRESENT
            debug_log_puts("KRN: dp err=display_open_failed\n");
#endif
            return (uint64_t)-1;
        }
        surface = ladybird_display_surface();
    }
    if (surface == NULL || surface->pixels == NULL) {
#if VEX_TRACE_BROWSER_PRESENT
        debug_log_puts("KRN: dp err=surface_unavailable\n");
#endif
        return (uint64_t)-1;
    }

    copy_w = req.width;
    copy_h = req.height;

    if (req.dirty != NULL) {
        if (!vex_user_range_is_accessible(process, (uint64_t)(uintptr_t)req.dirty, sizeof(dirty))) {
#if VEX_TRACE_BROWSER_PRESENT
            debug_log_puts("KRN: dp err=dirty_inaccessible\n");
#endif
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
#if VEX_TRACE_BROWSER_PRESENT
            debug_log_puts("KRN: dp err=dirty_invalid\n");
#endif
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
#if VEX_TRACE_BROWSER_PRESENT
    debug_log_puts("KRN: display_present exit ok\n");
#endif
    return 0;
}

static uint64_t vex_sys_app_window_open(uint64_t arg1) {
    GUI_GUARD;
    process_t *process = scheduler_current_process();
    vex_app_window_open_req_t *req_user = (vex_app_window_open_req_t *)arg1;
    vex_app_window_open_req_t req;
    char title[64];
    uint32_t window_id = 0u;

    debug_log_puts("app_window_open: enter\n");
    if (process == NULL || req_user == NULL ||
        !vex_request_writable(process,(uint64_t)(uintptr_t)req_user,sizeof(req))) {
        debug_log_puts("app_window_open: bad request\n");
        return (uint64_t)-1;
    }

    memcpy(&req, req_user, sizeof(req));
    if (req.width == 0u || req.height == 0u) {
        debug_log_puts("app_window_open: bad dimensions\n");
        return (uint64_t)-1;
    }
    if (process->user_mode != 0u &&
        !vex_dispatch_user_string_is_valid(process,
            (uint64_t)(uintptr_t)req.title, 256u)) {
        return (uint64_t)-1;
    }

    vex_copy_user_string(process, req.title, title, sizeof(title));
    debug_log_puts("app_window_open: title=");
    debug_log_puts(title);
    debug_log_puts("\n");
    if (app_surface_open(title, req.width, req.height, req.flags, &window_id) != 0) {
        debug_log_puts("app_window_open: surface failed\n");
        return (uint64_t)-1;
    }

    req.window_id = window_id;
    memcpy(req_user, &req, sizeof(req));
    debug_log_puts("app_window_open: exit ok\n");
    return 0;
}

static uint64_t vex_sys_app_window_present(uint64_t arg1) {
    GUI_GUARD;
    process_t *process = scheduler_current_process();
    vex_app_window_present_req_t *req_user = (vex_app_window_present_req_t *)arg1;
    vex_app_window_present_req_t req;
    app_surface_t *surface;

    if (process == NULL || req_user == NULL ||
        !vex_request_readable(process,(uint64_t)(uintptr_t)req_user,sizeof(req))) {
        debug_log_puts("app_window_present: bad request\n");
        return (uint64_t)-1;
    }

    memcpy(&req, req_user, sizeof(req));
    surface = app_surface_find(req.window_id);
    if (surface == NULL || surface->owner_pid != scheduler_current_process_id()) {
        debug_log_puts("app_window_present: bad window\n");
        return (uint64_t)-1;
    }

    return vex_app_surface_present_copy(process, surface, req.pixels, req.width,
                                        req.height, req.stride, req.dirty);
}

static uint64_t vex_sys_app_window_poll(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_app_window_poll_req_t *req_user = (vex_app_window_poll_req_t *)arg1;
    vex_app_window_poll_req_t req;
    vex_app_window_event_t event;

    {kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
     if (process == NULL || req_user == NULL ||
         !vex_request_readable(process,(uint64_t)(uintptr_t)req_user,sizeof(req)))return (uint64_t)-1;
     memcpy(&req,req_user,sizeof(req));
    }
    if((req.flags&0xfffeu) || ((req.flags>>16) && !(req.flags&VEX_APP_WINDOW_POLL_WAIT)))return (uint64_t)-1;
    const uint64_t wait_start=scheduler_ticks_now(),wait_ticks=req.flags>>16;
    for(;;) {
        uint64_t notification=0;
        {GUI_GUARD;
         if(!vex_user_range_is_writable(process,(uint64_t)(uintptr_t)req.event,sizeof(event)))return (uint64_t)-1;
         app_surface_t *surface=app_surface_find(req.window_id);
         if(surface && surface->owner_pid!=scheduler_current_process_id())return (uint64_t)-1;
         app_surface_poll_event(req.window_id,&event);
         if(surface)notification=surface->event_counter;
         if(event.type!=VEX_APP_EVENT_NONE || !(req.flags&VEX_APP_WINDOW_POLL_WAIT)) {
             memcpy(req.event,&event,sizeof(event));return 0;
         }
        }
        /* No GUI/PM lock crosses a wait. The counter's operation reference
           covers close/kill; a released token is looked up safely and the
           next inventory lookup reports CLOSE if the window disappeared. */
        uint64_t observed=0;
          uint64_t elapsed=scheduler_ticks_now()-wait_start;
          int result=wait_ticks?(elapsed>=wait_ticks?-4:
              vex_event_counter_read_timed(notification,&observed,0u,wait_ticks-elapsed)):
              vex_event_counter_read(notification,&observed,0u);
          if(result==-4){GUI_GUARD;
              if(!vex_user_range_is_writable(process,(uint64_t)(uintptr_t)req.event,sizeof(event)))return (uint64_t)-1;
              memset(&event,0,sizeof(event));memcpy(req.event,&event,sizeof(event));return 0;
          }
          if(result<0)return (uint64_t)-1;
    }
}

static uint64_t vex_sys_app_window_close(uint64_t arg1) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find((uint32_t)arg1);
    if (surface == NULL || surface->owner_pid != scheduler_current_pid()) {
        return (uint64_t)-1;
    }
    app_surface_close((uint32_t)arg1);
    return 0;
}

static uint64_t vex_sys_app_window_resize(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_app_window_resize_req_t *req_user = (vex_app_window_resize_req_t *)arg1;
    vex_app_window_resize_req_t req;

    /* Native requests are snapshots: validate and copy them like the other
     * window calls (see vex_sys_app_window_poll). */
    {
        kernel_guard_t memory __attribute__((cleanup(process_memory_end_update))) =
            process_memory_begin_update();
        if (process == NULL || req_user == NULL ||
            !vex_request_readable(process, (uint64_t)(uintptr_t)req_user, sizeof(req))) {
            return (uint64_t)-1;
        }
        memcpy(&req, req_user, sizeof(req));
    }
    {
        GUI_GUARD;
        return app_surface_accept_resize(req.window_id, req.width, req.height) == 0
            ? 0u : (uint64_t)-1;
    }
}

static uint64_t vex_sys_gpu_query_info(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_gpu_info_t info;

    if (!vex_request_writable(process,arg1,sizeof(info))) {
        return (uint64_t)-1;
    }
    if (gpu_query_info(&info) != 0) {
        return (uint64_t)-1;
    }

    memcpy((void *)(uintptr_t)arg1, &info, sizeof(info));
    return 0;
}

static uint64_t vex_sys_gpu_query_caps(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_gpu_caps_t caps;
    uint32_t requested_size;

    if (!vex_request_readable(process,arg1,sizeof(requested_size))) {
        return (uint64_t)-1;
    }
    memcpy(&requested_size, (const void *)(uintptr_t)arg1,
           sizeof(requested_size));
    if (requested_size < sizeof(caps) ||
        !vex_request_writable(process,arg1,sizeof(caps))) {
        return (uint64_t)-1;
    }
    if (gpu_query_caps(&caps) != 0) {
        return (uint64_t)-1;
    }
    memcpy((void *)(uintptr_t)arg1, &caps, sizeof(caps));
    return 0;
}

static uint64_t vex_sys_gpu_buffer_create(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_gpu_buffer_create_req_t *req_user = (vex_gpu_buffer_create_req_t *)arg1;
    vex_gpu_buffer_create_req_t req;

    if (process == NULL || !vex_request_writable(process,arg1,sizeof(req))) {
        return (uint64_t)-1;
    }

    memcpy(&req, req_user, sizeof(req));
    if (gpu_buffer_create(scheduler_current_process_id(), &req) != 0) {
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

    return gpu_buffer_destroy(scheduler_current_process_id(), (uint32_t)arg1) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t vex_sys_gpu_queue_submit(uint64_t arg1) {
    kernel_guard_t copy_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    process_t *process = scheduler_current_process();
    vex_gpu_submit_req_t *req_user = (vex_gpu_submit_req_t *)arg1;
    vex_gpu_submit_req_t req;
    size_t commands_size;

    if (process == NULL || !vex_request_readable(process,arg1,sizeof(req))) {
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
        if ((uint64_t)command->src_stride >
                SIZE_MAX / ((size_t)command->src_height * sizeof(uint32_t))) {
            return (uint64_t)-1;
        }
        if (!vex_user_range_is_accessible(process,
                (uint64_t)(uintptr_t)command->src_pixels,
                (size_t)command->src_stride * (size_t)command->src_height * sizeof(uint32_t))) {
            return (uint64_t)-1;
        }
    }

    return gpu_queue_submit(scheduler_current_process_id(), &req, req.commands) == 0 ? 0 : (uint64_t)-1;
}

static int vex_gpu_surface_present_internal(process_t *process,
                                            vex_gpu_surface_present_req_t *req,
                                            const vex_rect_t *damage,
                                            int restore_native_foreground) {
    GUI_GUARD;
    compositor_surface_present_info_t present;
    compositor_target_t target;
    compositor_layer_t layer;
    vex_rect_t destination_damage;
    compositor_surface_identity_t surface_identity = 0u;

    if (process == NULL || req == NULL) {
        return -1;
    }
    memset(&present, 0, sizeof(present));
    present.damage = damage;
    present.damage_count = req->damage_count;
    present.flags = req->flags;
    present.wait_serial = req->wait_serial;
    memset(&layer, 0, sizeof(layer));
    layer.source = req->source;
    layer.destination = req->destination;
    layer.opacity = req->opacity;
    layer.filter = req->filter;
    layer.transform = req->transform;
    if ((req->flags & VEX_GPU_SURFACE_PRESENT_ASYNC) != 0u) {
        return compositor_surface_queue_present(
            scheduler_current_process_id(), req->surface_handle, &present,
            &layer, req->image_available_event,
            req->swapchain_available_event, &req->signal_serial) ==
            COMPOSITOR_SURFACE_OK ? 0 : -1;
    }
    if (compositor_surface_pin(scheduler_current_process_id(), req->surface_handle,
                               &surface_identity) != COMPOSITOR_SURFACE_OK) {
        return -1;
    }
    if (compositor_surface_present(scheduler_current_process_id(), req->surface_handle,
                                   &present, &req->signal_serial) !=
        COMPOSITOR_SURFACE_OK) {
        compositor_surface_unpin(surface_identity);
        return -1;
    }
    if (gui_compositor_get_target(&target) != 0) {
        compositor_surface_unpin(surface_identity);
        return -1;
    }

    if (compositor_surface_compose(scheduler_current_process_id(),
                                   req->surface_handle, &target, &layer,
                                   &destination_damage) !=
        COMPOSITOR_SURFACE_OK) {
        compositor_surface_unpin(surface_identity);
        return -1;
    }
    {
        uint32_t consumed_count = 0u;

        (void)compositor_surface_consume_damage(
            scheduler_current_process_id(), req->surface_handle,
            NULL, 0u, &consumed_count);
    }
    gui_compositor_damage(&destination_damage);
    if(restore_native_foreground && gui_browser_store_presented(&req->destination)!=0) {
        compositor_surface_unpin(surface_identity);
        return -1; /* no successful ACK without a retained completed version */
    }
    if(restore_native_foreground)gui_compositor_restore_foreground(&destination_damage);
    gui_flush_scene_and_cursor();
    if (compositor_surface_complete(surface_identity, req->signal_serial) !=
        COMPOSITOR_SURFACE_OK) {
        compositor_surface_unpin(surface_identity);
        return -1;
    }
    compositor_surface_unpin(surface_identity);
    return 0;
}

static uint64_t vex_sys_gpu_present(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_gpu_present_req_t *req_user = (vex_gpu_present_req_t *)arg1;
    vex_gpu_present_req_t req;
    vex_gpu_surface_present_req_t surface_present;
    compositor_surface_info_t surface_info;
    compositor_surface_handle_t surface_handle;

    if (process == NULL || !vex_request_readable(process,arg1,sizeof(req))) {
        return (uint64_t)-1;
    }

    memcpy(&req, req_user, sizeof(req));
    if (compositor_surface_import_gpu_buffer(scheduler_current_process_id(), req.buffer_handle,
                                             COMPOSITOR_SURFACE_ALPHA_OPAQUE,
                                             COMPOSITOR_SURFACE_USAGE_SAMPLED,
                                             &surface_handle) !=
        COMPOSITOR_SURFACE_OK) {
        return (uint64_t)-1;
    }
    if (compositor_surface_query(scheduler_current_process_id(), surface_handle, &surface_info) !=
        COMPOSITOR_SURFACE_OK) {
        (void)compositor_surface_release(scheduler_current_process_id(), surface_handle);
        return (uint64_t)-1;
    }

    memset(&surface_present, 0, sizeof(surface_present));
    surface_present.surface_handle = surface_handle;
    surface_present.source = req.dirty;
    if (surface_present.source.x < 0 || surface_present.source.y < 0 ||
        surface_present.source.w <= 0 || surface_present.source.h <= 0 ||
        (uint64_t)(uint32_t)surface_present.source.x +
            (uint32_t)surface_present.source.w > surface_info.desc.width ||
        (uint64_t)(uint32_t)surface_present.source.y +
            (uint32_t)surface_present.source.h > surface_info.desc.height) {
        surface_present.source.x = 0;
        surface_present.source.y = 0;
        surface_present.source.w = (int32_t)surface_info.desc.width;
        surface_present.source.h = (int32_t)surface_info.desc.height;
    }
    surface_present.destination.x = req.dst_x;
    surface_present.destination.y = req.dst_y;
    surface_present.destination.w = surface_present.source.w;
    surface_present.destination.h = surface_present.source.h;
    surface_present.damage = &surface_present.source;
    surface_present.damage_count = 1u;
    surface_present.opacity = 255u;
    surface_present.filter = COMPOSITOR_SURFACE_FILTER_NEAREST;
    surface_present.transform = COMPOSITOR_SURFACE_TRANSFORM_IDENTITY;
    if (vex_gpu_surface_present_internal(process, &surface_present,
                                         &surface_present.source, 0) != 0) {
        (void)compositor_surface_release(scheduler_current_process_id(), surface_handle);
        return (uint64_t)-1;
    }
    (void)compositor_surface_release(scheduler_current_process_id(), surface_handle);
    return 0u;
}

static uint64_t vex_sys_gpu_surface_create(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_gpu_surface_create_req_t req;
    uint32_t usage = COMPOSITOR_SURFACE_USAGE_SAMPLED;

    if (process == NULL ||
        !vex_request_writable(process,arg1,sizeof(req))) {
        return (uint64_t)-1;
    }
    memcpy(&req, (const void *)(uintptr_t)arg1, sizeof(req));
    if ((req.usage & ~VEX_GPU_SURFACE_USAGE_SAMPLED) != 0u ||
        req.alpha_mode > VEX_GPU_SURFACE_ALPHA_STRAIGHT) {
        return (uint64_t)-1;
    }
    if (compositor_surface_import_gpu_buffer(scheduler_current_process_id(), req.buffer_handle,
                                             req.alpha_mode, usage,
                                             &req.surface_handle) !=
        COMPOSITOR_SURFACE_OK) {
        return (uint64_t)-1;
    }
    memcpy((void *)(uintptr_t)arg1, &req, sizeof(req));
    return 0u;
}

static uint64_t vex_sys_gpu_surface_destroy(uint64_t arg1) {
    process_t *process = scheduler_current_process();

    if (process == NULL) {
        return (uint64_t)-1;
    }
    return compositor_surface_release(scheduler_current_process_id(),
                                      (compositor_surface_handle_t)arg1) ==
        COMPOSITOR_SURFACE_OK ? 0u : (uint64_t)-1;
}

static uint64_t vex_sys_gpu_surface_present(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_gpu_surface_present_req_t req;
    vex_rect_t damage[COMPOSITOR_SURFACE_DAMAGE_MAX];

    if (process == NULL ||
        !vex_request_writable(process,arg1,sizeof(req))) {
        return (uint64_t)-1;
    }
    memcpy(&req, (const void *)(uintptr_t)arg1, sizeof(req));
    if (req.damage_count > COMPOSITOR_SURFACE_DAMAGE_MAX ||
        req.opacity > 255u ||
        req.filter > VEX_GPU_SURFACE_FILTER_LINEAR ||
        req.transform != VEX_GPU_SURFACE_TRANSFORM_IDENTITY) {
        return (uint64_t)-1;
    }
    if (req.damage_count != 0u) {
        const size_t damage_bytes = (size_t)req.damage_count * sizeof(vex_rect_t);

        if (req.damage == NULL ||
            !vex_user_range_is_accessible(process,
                (uint64_t)(uintptr_t)req.damage, damage_bytes)) {
            return (uint64_t)-1;
        }
        memcpy(damage, req.damage, damage_bytes);
    }
    if ((req.flags & ~(VEX_GPU_SURFACE_PRESENT_ASYNC)) != 0u ||
        ((req.flags & VEX_GPU_SURFACE_PRESENT_ASYNC) != 0u &&
         (req.image_available_event == 0u ||
          req.swapchain_available_event == 0u ||
          vex_event_counter_authorize_owner(scheduler_current_process_id(),
              req.image_available_event) != 0 ||
          vex_event_counter_authorize_owner(scheduler_current_process_id(),
              req.swapchain_available_event) != 0))) {
        return (uint64_t)-1;
    }
    if (vex_gpu_surface_present_internal(process, &req,
            req.damage_count != 0u ? damage : NULL, 0) != 0) {
        return (uint64_t)-1;
    }
    memcpy((void *)(uintptr_t)arg1, &req, sizeof(req));
    return 0u;
}

static uint64_t vex_sys_gpu_surface_wait(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_gpu_surface_wait_req_t req;
    if (process == NULL ||
        !vex_request_readable(process,arg1,sizeof(req))) {
        return (uint64_t)-1;
    }
    memcpy(&req, (const void *)(uintptr_t)arg1, sizeof(req));
    return compositor_surface_wait(scheduler_current_process_id(), req.surface_handle,
        req.serial, req.timeout_ticks) == COMPOSITOR_SURFACE_OK
        ? 0u : (uint64_t)-1;
}

static uint64_t vex_sys_surface_map(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_surface_map_req_t req;
    compositor_surface_identity_t pin = 0u;
    uint64_t shared_id = 0u;
    uint64_t address;
    uint64_t aligned_length;
    uint32_t surface_access = 0u;
    compositor_surface_mapping_t surface_mapping;
    int prot = 0;
    int result;

    if (process == NULL ||
        !vex_request_writable(process,arg1,sizeof(req))) {
        return (uint64_t)-1;
    }
    memcpy(&req, (const void *)(uintptr_t)arg1, sizeof(req));
    if (req.length == 0u || req.length > SIZE_MAX || req.flags != 0u ||
        (req.access & ~(VEX_SURFACE_MAP_READ | VEX_SURFACE_MAP_WRITE)) != 0u ||
        req.access == 0u ||
        (req.offset & (VEX_USER_PAGE_SIZE - 1ull)) != 0u) {
        return (uint64_t)-1;
    }
    aligned_length = align_up_u64(req.length, VEX_USER_PAGE_SIZE);
    if (aligned_length == 0u || aligned_length > SIZE_MAX) {
        return (uint64_t)-1;
    }
    if ((req.access & VEX_SURFACE_MAP_READ) != 0u) {
        surface_access |= COMPOSITOR_SURFACE_MAP_READ;
        prot |= PROT_READ;
    }
    if ((req.access & VEX_SURFACE_MAP_WRITE) != 0u) {
        surface_access |= COMPOSITOR_SURFACE_MAP_WRITE;
        prot |= PROT_WRITE;
    }
    result = compositor_surface_authorize_mapping(scheduler_current_process_id(),
        req.surface_handle, surface_access, req.offset, (size_t)req.length,
        &shared_id, &pin);
    if (result != COMPOSITOR_SURFACE_OK) {
        return (uint64_t)-1;
    }
    address = req.preferred_addr;
    if (address != 0u) {
        if ((address & (VEX_USER_PAGE_SIZE - 1ull)) != 0u ||
            !vex_user_virtual_range_valid(address, aligned_length) ||
            !vex_user_mapping_is_free(process, address, (size_t)aligned_length)) {
            compositor_surface_unpin(pin);
            return (uint64_t)-1;
        }
    } else {
        address = vex_map_user_memory(process, (size_t)aligned_length, 0u);
    }
    if (address == 0u || !vex_user_virtual_range_valid(address, aligned_length) ||
        process_memory_shared_map_surface(scheduler_current_process_id(), process->cr3,
            shared_id, address, (size_t)aligned_length, (size_t)req.offset,
            prot) != 0) {
        compositor_surface_unpin(pin);
        return (uint64_t)-1;
    }
    /* Keep the Surface backing pinned against resize while this process has a
       live mapping. The address-space region owns the physical backing ref;
       these counters protect the Surface-to-backing association itself. */
    if (compositor_surface_map(scheduler_current_process_id(), req.surface_handle,
            surface_access, &surface_mapping) != COMPOSITOR_SURFACE_OK) {
        (void)process_memory_remove_region(scheduler_current_process_id(),
            process->cr3, address, (size_t)aligned_length);
        compositor_surface_unpin(pin);
        return (uint64_t)-1;
    }
    compositor_surface_unpin(pin);
    req.mapped_addr = address;
    memcpy((void *)(uintptr_t)arg1, &req, sizeof(req));
    return 0u;
}

static uint64_t vex_sys_surface_retain(uint64_t handle) {
    return compositor_surface_retain(scheduler_current_process_id(), handle) ==
        COMPOSITOR_SURFACE_OK ? 0u : (uint64_t)-1;
}

static uint64_t vex_sys_surface_query(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_surface_query_req_t req;
    compositor_surface_info_t info;

    if (process == NULL ||
        !vex_request_writable(process,arg1,sizeof(req))) {
        return (uint64_t)-1;
    }
    memcpy(&req, (const void *)(uintptr_t)arg1, sizeof(req));
    if (req.surface_handle == 0u ||
        compositor_surface_query(scheduler_current_process_id(),
            req.surface_handle, &info) != COMPOSITOR_SURFACE_OK) {
        return (uint64_t)-1;
    }
    req.object_identity = info.object_identity;
    req.backing_identity = info.backing_identity;
    req.submitted_serial = info.submitted_serial;
    req.completed_serial = info.completed_serial;
    req.backing_size = info.backing_size;
    req.width = info.desc.width;
    req.height = info.desc.height;
    req.stride = info.stride;
    req.format = info.desc.format;
    req.alpha_mode = info.desc.alpha_mode;
    req.usage = info.desc.usage;
    req.backing_type = (uint32_t)info.backing_type;
    req.reserved0 = 0u;
    req.capability_rights = info.capability_rights;
    req.reserved1 = 0u;
    /* shared_backing_id is intentionally kernel-only. */
    memcpy((void *)(uintptr_t)arg1, &req, sizeof(req));
    return 0u;
}

static uint64_t vex_sys_surface_unmap(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_surface_unmap_req_t req;
    compositor_surface_identity_t pin = 0u;
    uint64_t shared_id = 0u;
    uint64_t aligned_length;
    uint32_t surface_access = 0u;
    int prot = 0;

    if (process == NULL ||
        !vex_request_readable(process,arg1,sizeof(req))) {
        return (uint64_t)-1;
    }
    memcpy(&req, (const void *)(uintptr_t)arg1, sizeof(req));
    if (req.surface_handle == 0u || req.mapped_addr == 0u ||
        req.length == 0u || req.length > SIZE_MAX || req.flags != 0u ||
        req.access == 0u ||
        (req.access & ~(VEX_SURFACE_MAP_READ | VEX_SURFACE_MAP_WRITE)) != 0u ||
        (req.mapped_addr & (VEX_USER_PAGE_SIZE - 1ull)) != 0u ||
        (req.offset & (VEX_USER_PAGE_SIZE - 1ull)) != 0u ||
        req.length > UINT64_MAX - (VEX_USER_PAGE_SIZE - 1ull)) {
        return (uint64_t)-1;
    }
    if ((req.access & VEX_SURFACE_MAP_READ) != 0u) {
        surface_access |= COMPOSITOR_SURFACE_MAP_READ;
        prot |= PROT_READ;
    }
    if ((req.access & VEX_SURFACE_MAP_WRITE) != 0u) {
        surface_access |= COMPOSITOR_SURFACE_MAP_WRITE;
        prot |= PROT_WRITE;
    }
    aligned_length = align_up_u64(req.length, VEX_USER_PAGE_SIZE);
    if (aligned_length == 0u || aligned_length > SIZE_MAX ||
        !vex_user_virtual_range_valid(req.mapped_addr, aligned_length) ||
        compositor_surface_authorize_mapping(scheduler_current_process_id(),
            req.surface_handle, surface_access, req.offset, (size_t)req.length,
            &shared_id, &pin) != COMPOSITOR_SURFACE_OK ||
        process_memory_validate_surface_mapping(scheduler_current_process_id(),
            req.mapped_addr, (size_t)aligned_length, shared_id, prot) != 0) {
        if (pin != 0u) compositor_surface_unpin(pin);
        return (uint64_t)-1;
    }
    if (process_memory_remove_region(scheduler_current_process_id(), process->cr3,
            req.mapped_addr, (size_t)aligned_length) != 0) {
        compositor_surface_unpin(pin);
        return (uint64_t)-1;
    }
    if (compositor_surface_unmap(scheduler_current_process_id(), req.surface_handle,
            surface_access) != COMPOSITOR_SURFACE_OK) {
        compositor_surface_unpin(pin);
        return (uint64_t)-1;
    }
    compositor_surface_unpin(pin);
    return 0u;
}

static uint64_t vex_sys_browser_command_read(uint64_t arg1) {
    vex_browser_command_req_t *req = (vex_browser_command_req_t *)arg1;
    process_t *process = scheduler_current_process();
    if(scheduler_current_process_id()!=ladybird_browser_process_pid())return (uint64_t)-1;
    if (req == NULL || req->buffer == NULL || req->size == 0u) {
        return (uint64_t)-1;
    }
    if (!vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->buffer, req->size)) {
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
    process_t *process = scheduler_current_process();
    if (process != NULL && process->user_mode != 0u &&
        !vex_dispatch_user_string_is_valid(process, arg1, 4096u)) {
        return (uint64_t)-1;
    }
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
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_bulk_req_t *req = (vex_bulk_req_t *)arg1;
    vex_bulk_result_t *out;
    process_t *process;

    if (req == NULL || req->result == NULL) {
        return (uint64_t)-1;
    }

    out = req->result;
    process = scheduler_current_process();
    if (!vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)out, sizeof(*out))) {
        return (uint64_t)-1;
    }
    memset(out, 0, sizeof(*out));

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
        out->capabilities = scheduler_current_capabilities();
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
        copy_fixed_string(out->kernel_name, sizeof(out->kernel_name), "KyIlOS");
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

typedef struct vex_linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[];
} vex_linux_dirent64_t;

static uint64_t vex_sys_directory_list(uint64_t arg1) {
    enum {
        VEX_DIRECTORY_RAW_BUFFER_SIZE = 32768,
        VEX_DIRECTORY_MAX_ENTRIES = 128,
        VEX_LINUX_O_DIRECTORY = 0x10000,
        VEX_LINUX_DT_DIR = 4,
        VEX_LINUX_DT_REG = 8,
        VEX_LINUX_DT_LNK = 10
    };
    process_t *process = scheduler_current_process();
    vex_directory_list_req_t *req = (vex_directory_list_req_t *)arg1;
    uint8_t *raw = NULL;
    char path[512];
    ssize_t byte_count;
    size_t offset = 0u;
    size_t count = 0u;
    int fd;

    if (process == NULL ||
        !vex_request_readable(process,arg1,sizeof(*req))) {
        return (uint64_t)-1;
    }
    if (req->path == NULL || req->capacity > VEX_DIRECTORY_MAX_ENTRIES ||
        (process->user_mode != 0u &&
         !vex_dispatch_user_string_is_valid(process,
             (uint64_t)(uintptr_t)req->path, 4096u)) ||
        (req->capacity != 0u &&
         !vex_user_range_is_writable(process,
             (uint64_t)(uintptr_t)req->entries,
             req->capacity * sizeof(*req->entries)))) {
        return (uint64_t)-1;
    }
    vex_copy_user_string(process, req->path, path, sizeof(path));
    if (path[0] == '\0') {
        return (uint64_t)-1;
    }
    req->count = 0u;
    fd = open(path, VEX_LINUX_O_DIRECTORY);
    if (fd < 0) {
        return (uint64_t)-1;
    }
    raw = kmalloc(VEX_DIRECTORY_RAW_BUFFER_SIZE);
    if (raw == NULL) {
        close(fd);
        return (uint64_t)-1;
    }
    byte_count = vex_web_getdents64(fd, raw, VEX_DIRECTORY_RAW_BUFFER_SIZE);
    close(fd);
    if (byte_count < 0) {
        kfree(raw);
        return (uint64_t)-1;
    }

    while (offset + offsetof(vex_linux_dirent64_t, d_name) <
           (size_t)byte_count && count < req->capacity) {
        const vex_linux_dirent64_t *source =
            (const vex_linux_dirent64_t *)(raw + offset);
        vex_directory_entry_t *destination;
        size_t name_capacity;
        size_t name_length = 0u;

        if (source->d_reclen < offsetof(vex_linux_dirent64_t, d_name) + 1u ||
            offset + source->d_reclen > (size_t)byte_count) {
            kfree(raw);
            return (uint64_t)-1;
        }
        name_capacity = source->d_reclen -
                        offsetof(vex_linux_dirent64_t, d_name);
        while (name_length < name_capacity &&
               source->d_name[name_length] != '\0') {
            ++name_length;
        }
        if (name_length == name_capacity) {
            kfree(raw);
            return (uint64_t)-1;
        }
        if (!((name_length == 1u && source->d_name[0] == '.') ||
              (name_length == 2u && source->d_name[0] == '.' &&
               source->d_name[1] == '.'))) {
            destination = &req->entries[count];
            memset(destination, 0, sizeof(*destination));
            copy_fixed_string(destination->name, sizeof(destination->name),
                              source->d_name);
            if (source->d_type == VEX_LINUX_DT_DIR) {
                destination->type = VEX_DIRECTORY_TYPE_DIRECTORY;
            } else if (source->d_type == VEX_LINUX_DT_LNK) {
                destination->type = VEX_DIRECTORY_TYPE_SYMLINK;
            } else if (source->d_type == VEX_LINUX_DT_REG) {
                destination->type = VEX_DIRECTORY_TYPE_FILE;
            } else {
                destination->type = VEX_DIRECTORY_TYPE_UNKNOWN;
            }
            ++count;
        }
        offset += source->d_reclen;
    }
    req->count = count;
    kfree(raw);
    return 0u;
}

static uint64_t vex_sys_open_path(uint64_t arg1) {
    enum { VEX_LINUX_O_DIRECTORY = 0x10000 };
    process_t *process = scheduler_current_process();
    vex_open_path_req_t *req = (vex_open_path_req_t *)arg1;
    char path[512];

    if (process == NULL ||
        !vex_request_readable(process,arg1,sizeof(*req)) ||
        req->path == NULL ||
        (process->user_mode != 0u &&
         !vex_dispatch_user_string_is_valid(process,
             (uint64_t)(uintptr_t)req->path, 4096u))) {
        return (uint64_t)-1;
    }
    vex_copy_user_string(process, req->path, path, sizeof(path));
    if (path[0] == '\0') {
        return (uint64_t)-1;
    }
    if (req->type == VEX_DIRECTORY_TYPE_DIRECTORY) {
        const int fd = open(path, VEX_LINUX_O_DIRECTORY);
        if (fd < 0) {
            return (uint64_t)-1;
        }
        close(fd);
        gui_open_file_manager_path(path);
        return 0u;
    }
    if (!fs_exists(path)) {
        return (uint64_t)-1;
    }
    gui_open_path(path);
    return 0u;
}

static uint64_t vex_sys_open_context(uint64_t arg1) {
    process_t *process = scheduler_current_process();
    vex_open_context_req_t *req = (vex_open_context_req_t *)arg1;

    if (process == NULL ||
        !vex_request_readable(process,arg1,sizeof(*req)) ||
        req->buffer == NULL || req->size == 0u ||
        req->size > 512u ||
        !vex_user_range_is_writable(process,
            (uint64_t)(uintptr_t)req->buffer, req->size)) {
        return (uint64_t)-1;
    }
    return gui_claim_open_context(req->application, scheduler_current_process_id(),
                                  req->buffer, req->size) == 0
        ? 0u : (uint64_t)-1;
}

static uint64_t vex_sys_process_spawn(uint64_t arg1) {
    vex_process_spawn_req_t *req = (vex_process_spawn_req_t *)arg1;
    process_t *process = scheduler_current_process();
    int spawned;

    if (req == NULL || req->path == NULL) {
        debug_log_puts("spawn: invalid request\n");
        return (uint64_t)-1;
    }
    /* A failed spawn must never leave a caller-provided/stale PID looking
       valid.  The loader writes the PID only after the complete image and
       process-memory namespace have committed. */
    req->pid = 0u;
    if (process != NULL && process->user_mode != 0u &&
        !vex_dispatch_user_string_is_valid(process,
            (uint64_t)(uintptr_t)req->path, 4096u)) {
        return (uint64_t)-1;
    }

    debug_log_puts("spawn: ");
    debug_log_puts(req->path);
    debug_log_puts("\n");
    uint32_t allowed=VEX_PROCESS_SPAWN_CAPABILITIES|(VEX_CAP_ALL<<VEX_PROCESS_SPAWN_CAP_SHIFT);
    uint32_t caps=req->flags>>VEX_PROCESS_SPAWN_CAP_SHIFT;
    if((req->flags&~allowed) || (caps && !(req->flags&VEX_PROCESS_SPAWN_CAPABILITIES)))return (uint64_t)-22;
    if(!scheduler_current_has_capability(caps))return (uint64_t)-13;
    spawned = caps?elf_spawn_pid_caps(req->path,&req->pid,caps):elf_spawn_pid(req->path, &req->pid);
    if (spawned != 0 || req->pid == 0u) {
        req->pid = 0u;
        debug_log_puts("spawn: failed ");
        debug_log_puts(req->path);
        debug_log_puts("\n");
        return (uint64_t)-1;
    }

    return 0;
}

static uint64_t vex_sys_process_wait(uint64_t arg1) {
    vex_process_wait_req_t *req = (vex_process_wait_req_t *)arg1;
    process_t *process = scheduler_current_process();
    uint32_t reaped_pid = 0u;

    if (req == NULL) {
        return (uint64_t)-1;
    }
    if (req->status != NULL && !vex_dispatch_range_is_writable(process,
            (uint64_t)(uintptr_t)req->status, sizeof(*req->status))) {
        return (uint64_t)-1;
    }

    return (uint64_t)process_wait_pid((int32_t)req->pid, req->status, req->flags, &reaped_pid);
}

static uint64_t vex_sys_process_exec(uint64_t arg1) {
    vex_process_exec_req_t *req = (vex_process_exec_req_t *)arg1;
    process_t *process = scheduler_current_process();
    vex_exec_vector_t argv;
    vex_exec_vector_t envp;
    char path[512];
    int result;

    *ladybird_errno_location() = 0;
    memset(&argv, 0, sizeof(argv));
    memset(&envp, 0, sizeof(envp));

    if (req == NULL || req->path == NULL) {
        *ladybird_errno_location() = EINVAL;
        return (uint64_t)-1;
    }
    if (process != NULL && process->user_mode != 0u &&
        !vex_user_string_is_valid(process,
            (uint64_t)(uintptr_t)req->path, 4096u)) {
        *ladybird_errno_location() = EFAULT;
        return (uint64_t)-1;
    }

    if (!vex_dispatch_user_string_is_valid(process,
            (uint64_t)(uintptr_t)req->path, sizeof(path)) ||
        process == NULL) {
        *ladybird_errno_location() = EFAULT;
        return (uint64_t)-1;
    }
    vex_copy_user_string(process, req->path, path, sizeof(path));
    if (path[0] == '\0') {
        *ladybird_errno_location() = ENOENT;
        return (uint64_t)-1;
    }
    if (!fs_exists(path)) {
        *ladybird_errno_location() = ENOENT;
        return (uint64_t)-1;
    }
    if (
        vex_exec_vector_copy(process, (const char *const *)req->argv, &argv) != 0 ||
        vex_exec_vector_copy(process, (const char *const *)req->envp, &envp) != 0) {
        *ladybird_errno_location() = EFAULT;
        vex_exec_vector_free(&argv);
        vex_exec_vector_free(&envp);
        return (uint64_t)-1;
    }

    result = elf_exec_current_args(path,
        (const char *const *)argv.items, argv.count,
        (const char *const *)envp.items, envp.count);
    vex_exec_vector_free(&argv);
    vex_exec_vector_free(&envp);
    if (result != 0 && *ladybird_errno_location() == 0) {
        *ladybird_errno_location() = EINVAL;
    }
    return (uint64_t)result;
}

static uint64_t vex_sys_elevation_request(uint64_t arg1) {
    vex_elevation_request_req_t *req = (vex_elevation_request_req_t *)arg1;
    process_t *process = scheduler_current_process();

    if (req == NULL) {
        return (uint64_t)-1;
    }
    if (process != NULL && process->user_mode != 0u &&
        !vex_dispatch_user_string_is_valid(process,
            (uint64_t)(uintptr_t)req->reason, 4096u)) {
        return (uint64_t)-1;
    }

    return (uint64_t)elevation_request_create(req->reason, req->flags, &req->request_id);
}

static uint64_t vex_sys_elevation_status(uint64_t arg1) {
    vex_elevation_status_req_t *req = (vex_elevation_status_req_t *)arg1;

    if (req == NULL) {
        return (uint64_t)-1;
    }

    return (uint64_t)elevation_query(scheduler_current_pid(), req);
}

static uint64_t vex_sys_shm_create(uint64_t arg1) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_shm_create_req_t *req = (vex_shm_create_req_t *)(uintptr_t)arg1;
    process_t *process = scheduler_current_process();
    char name[64];
    vex_named_shm_t *object;
    uint64_t id;
    uint64_t token;

    if (req == NULL || process == NULL || req->size == 0u ||
        (uint64_t)req->size > UINT64_MAX - (VEX_USER_PAGE_SIZE - 1ull) ||
        (req->prot & ~(PROT_READ | PROT_WRITE)) != 0 ||
        vex_copy_shm_name(process, req->name, name) != 0) {
        return (uint64_t)-1;
    }
    object = vex_find_named_shm(name);
    if (object != NULL) {
        const uint32_t caller_pid = scheduler_current_process_id();
        const int authorized = vex_shm_open_authorized(object, caller_pid);
        if (!authorized || object->size < req->size ||
            (req->prot & ~object->prot) != 0 ||
            vex_shm_grant_capability(object, caller_pid, &token) != 0) {
            return (uint64_t)-1;
        }
        req->handle = token;
        return 0;
    }
    if (process_memory_shared_create(req->size, &id) != 0) {
        return (uint64_t)-1;
    }
    object = kmalloc(sizeof(*object));
    if (object == NULL) {
        process_memory_shared_release(id);
        return (uint64_t)-1;
    }
    memset(object, 0, sizeof(*object));
    object->id = id;
    object->size = (req->size + VEX_USER_PAGE_SIZE - 1u) &
        ~(VEX_USER_PAGE_SIZE - 1u);
    object->prot = req->prot;
    object->creator_pid = scheduler_current_process_id();
    object->handle_refs = 0u;
    memcpy(object->name, name, sizeof(object->name));
    object->next = vex_named_shm_objects;
    vex_named_shm_objects = object;
    if (vex_shm_grant_capability(object, scheduler_current_process_id(),
                                 &token) != 0) {
        vex_named_shm_objects = object->next;
        kfree(object);
        process_memory_shared_release(id);
        return (uint64_t)-1;
    }
    /* process_memory_shared_create starts with one temporary creator ref;
     * the capability now owns the durable ref granted above. */
    process_memory_shared_release(id);
    req->handle = token;
    return 0;
}

static uint64_t vex_sys_shm_open(uint64_t arg1) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_shm_open_req_t *req = (vex_shm_open_req_t *)(uintptr_t)arg1;
    process_t *process = scheduler_current_process();
    char name[64];
    vex_named_shm_t *object;
    uint64_t token;

    if (req == NULL || process == NULL || vex_copy_shm_name(process, req->name, name) != 0) {
        return (uint64_t)-1;
    }
    object = vex_find_named_shm(name);
    if (object == NULL || !vex_shm_open_authorized(object,
            scheduler_current_process_id()) || vex_shm_grant_capability(object,
            scheduler_current_process_id(), &token) != 0) {
        return (uint64_t)-1;
    }
    req->handle = token;
    return 0;
}

static uint64_t vex_sys_shm_close(uint64_t arg1) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_shm_capability_t *cap = vex_find_shm_capability(
        scheduler_current_process_id(), arg1);
    return cap != NULL && vex_shm_revoke_capability(cap) == 0
        ? 0u : (uint64_t)-1;
}

static uint64_t vex_sys_shm_map(uint64_t arg1) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_shm_map_req_t *req = (vex_shm_map_req_t *)(uintptr_t)arg1;
    process_t *process = scheduler_current_process();
    vex_shm_capability_t *cap;
    uint64_t addr;
    size_t length;

    if (req == NULL || process == NULL || req->handle == 0u) {
        return (uint64_t)-1;
    }
    cap = vex_find_shm_capability(scheduler_current_process_id(), req->handle);
    if (cap == NULL || (req->prot & ~(int)cap->rights) != 0 ||
        req->offset % VEX_USER_PAGE_SIZE != 0u) {
        return (uint64_t)-1;
    }
    {
        vex_named_shm_t *object = NULL;
        for (vex_named_shm_t *candidate = vex_named_shm_objects; candidate != NULL;
             candidate = candidate->next) {
            if (candidate->id == cap->backing_id) {
                object = candidate;
                break;
            }
        }
        if (object == NULL || req->offset > object->size) {
            return (uint64_t)-1;
        }
        length = req->length == 0u ? object->size - req->offset : req->length;
        if ((uint64_t)length > UINT64_MAX - (VEX_USER_PAGE_SIZE - 1ull)) {
            return (uint64_t)-1;
        }
        length = (size_t)align_up_u64(length, VEX_USER_PAGE_SIZE);
        if (length == 0u || length > object->size - req->offset) {
            return (uint64_t)-1;
        }
    }
    if (req->preferred_addr != NULL) {
        addr = (uint64_t)(uintptr_t)req->preferred_addr;
        if ((addr & (VEX_USER_PAGE_SIZE - 1ull)) != 0u ||
            !vex_user_virtual_range_valid(addr, length) ||
            !vex_user_mapping_is_free(process, addr, length)) {
            return (uint64_t)-1;
        }
    } else {
        addr = vex_map_user_memory(process, length, 0u);
    }
    if (addr == 0u || process_memory_shared_map(scheduler_current_process_id(),
            process->cr3, cap->backing_id, addr, length, req->offset,
            req->prot) != 0) {
        return (uint64_t)-1;
    }
    req->mapped_addr = (void *)(uintptr_t)addr;
    return 0;
}

void vex_shm_reap_pid(uint32_t owner_pid) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    vex_shm_capability_t *cap;

    if (owner_pid == 0u) {
        return;
    }
    cap = vex_shm_capabilities;
    while (cap != NULL) {
        vex_shm_capability_t *next = cap->next;
        if (cap->owner_pid == owner_pid) {
            (void)vex_shm_revoke_capability(cap);
        }
        cap = next;
    }
    /* A backing may have been held by a mapping until namespace teardown. */
    for (vex_named_shm_t *object = vex_named_shm_objects; object != NULL;) {
        vex_named_shm_t *next = object->next;
        vex_reap_named_shm_if_unused(object);
        object = next;
    }
}

static uint64_t vex_sys_console(uint64_t arg) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    process_t *p=scheduler_current_process();vex_console_req_t *r=(void *)(uintptr_t)arg;
    if(!p || !p->user_mode || !vex_request_writable(p,arg,sizeof(*r)) || r->reserved)return (uint64_t)-22;
    if(r->operation==VEX_CONSOLE_ATTACH)return console_attach(r->window_id,&r->token);
    if(r->operation==VEX_CONSOLE_READ) {
        if(!r->size || r->size>4096 || !vex_user_range_is_writable(p,(uintptr_t)r->buffer,r->size))return (uint64_t)-14;
        return console_read(r->window_id,r->token,r->buffer,r->size,&r->flags);
    }
    if(r->operation==VEX_CONSOLE_SUBMIT) {
        char text[255];
        if(!r->size || r->size>254 || !vex_user_range_is_accessible(p,(uintptr_t)r->buffer,r->size))return (uint64_t)-14;
        memcpy(text,r->buffer,r->size);return console_submit(r->window_id,r->token,text,r->size);
    }
    return (uint64_t)-22;
}
static uint64_t vex_syscall_dispatch_impl(uint64_t number, uint64_t arg1,
                                           uint64_t arg2, uint64_t arg3,
                                           int from_user) {
    process_t *process = scheduler_current_process();
    const size_t request_size = vex_syscall_request_size(number);
    if(number==VEX_SYS_BROWSER_VIEWPORT) {
        static unsigned reports;
        if(reports++<3) {
            debug_log_puts("viewport-entry: pid=");vex_mmap_debug_hex("pid",scheduler_current_process_id());
            debug_log_puts(" owner=");vex_mmap_debug_hex("owner",ladybird_browser_process_pid());
            debug_log_puts(" writable=");vex_mmap_debug_hex("writable",vex_user_range_is_writable(process,arg1,request_size));debug_log_puts("\n");
        }
    }

    /* Every structured native request is checked before a handler can
       dereference its first field.  Input-only records need read access;
       records whose fields are returned to the caller need write access.
       Nested buffers are checked by the individual handler. */
    if (from_user != 0 && request_size != 0u &&
        (process == NULL || process->user_mode == 0u ||
         (vex_syscall_request_is_writable(number)
             ? !vex_user_range_is_writable(process, arg1, request_size)
             : !vex_user_range_is_accessible(process, arg1, request_size)))) {
        return (uint64_t)-1;
    }
    if (process != NULL && process->user_mode != 0u) {
        switch (number) {
        case VEX_SYS_OPEN:
        case VEX_SYS_ACCESS:
        case VEX_SYS_UNLINK:
        case VEX_SYS_RMDIR:
        case VEX_SYS_CHDIR:
        case VEX_SYS_CHMOD:
        case VEX_SYS_GETENV:
        case VEX_SYS_UNSETENV:
        case VEX_SYS_DEBUG_LOG:
            if (!vex_dispatch_user_string_is_valid(process, arg1, 4096u)) {
                return (uint64_t)-1;
            }
            break;
        case VEX_SYS_SHM_CREATE:
        case VEX_SYS_SHM_OPEN: {
            const char *name = number == VEX_SYS_SHM_CREATE
                ? ((vex_shm_create_req_t *)(uintptr_t)arg1)->name
                : ((vex_shm_open_req_t *)(uintptr_t)arg1)->name;
            if (!vex_dispatch_user_string_is_valid(process,
                    (uint64_t)(uintptr_t)name, 64u)) {
                return (uint64_t)-1;
            }
            break;
        }
        case VEX_SYS_OPENAT:
            if (!vex_dispatch_user_string_is_valid(process, arg2, 4096u)) {
                return (uint64_t)-1;
            }
            break;
        case VEX_SYS_UNAME:
            if (!vex_request_writable(process,arg1,sizeof(struct utsname))) {
                return (uint64_t)-1;
            }
            break;
        default:
            break;
        }
    }

    switch (number) {
    case VEX_SYS_BULK:
        return vex_sys_bulk(arg1);
    case VEX_SYS_CONSOLE:
        return vex_sys_console(arg1);
    case VEX_SYS_OPEN:
        return vex_sys_open(arg1, arg2);
    case VEX_SYS_OPENAT:
        return vex_sys_openat(arg1, arg2, arg3);
    case VEX_SYS_DIRECTORY_LIST:
        return vex_sys_directory_list(arg1);
    case VEX_SYS_OPEN_PATH:
        return vex_sys_open_path(arg1);
    case VEX_SYS_OPEN_CONTEXT:
        return vex_sys_open_context(arg1);
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
        if (from_user != 0 && (process == NULL ||
            !vex_user_range_is_writable(process, arg2, sizeof(struct stat)))) {
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
        return (uint64_t)versioned_current(scheduler_current_process_id(), arg1);
    case VEX_SYS_MRAC_VERSION_COUNT:
        return (uint64_t)versioned_count(scheduler_current_process_id(), arg1);
    case VEX_SYS_MRAC_VERSION_INFO:
        return vex_sys_mrac_version_info(arg1);
    case VEX_SYS_JIT_ARENA_CREATE:
        return vex_sys_jit_arena_create(arg1);
    case VEX_SYS_JIT_ARENA_DESTROY:
        return vex_sys_jit_arena_destroy(arg1);
    case VEX_SYS_BROWSER_COMMAND_READ:
        return vex_sys_browser_command_read(arg1);
    case VEX_SYS_BROWSER_VIEWPORT: {
        GUI_GUARD;
        vex_viewport_req_t *r=(void *)(uintptr_t)arg1;
        if(scheduler_current_process_id()!=ladybird_browser_process_pid() || r->reserved)return (uint64_t)-1;
        if(r->padding)return (uint64_t)-22;
        if(r->operation==VEX_VIEWPORT_STATE) {
            if(!vex_user_range_is_accessible(process,(uintptr_t)r->state,sizeof(vex_browser_state_t)))return (uint64_t)-14;
            vex_browser_state_t state=*r->state;
            size_t u=0,t=0,e=0;
            while(u<sizeof(state.uri) && state.uri[u])u++;
            while(t<sizeof(state.title) && state.title[t])t++;
            while(e<sizeof(state.error) && state.error[e])e++;
            if(u==sizeof(state.uri) || t==sizeof(state.title) || e==sizeof(state.error))return (uint64_t)-22;
            return gui_browser_update_state(&state);
        }
        if(r->operation==VEX_VIEWPORT_DOWNLOAD) {
            if(!vex_user_range_is_accessible(process,(uintptr_t)r->download,sizeof(vex_download_state_t)))return (uint64_t)-14;
            vex_download_state_t download=*r->download;
            return (uint64_t)(int64_t)browser_downloads_update(&download);
        }
        if(r->operation==VEX_VIEWPORT_POLL){int result=browser_viewport_poll(&r->event);r->tab_id=browser_viewport_polled_tab();return result;}
        if(r->operation==VEX_VIEWPORT_QUERY) {
            vex_rect_t b={0};browser_viewport_bounds(&b);
            r->tab_id=gui_browser_active_tab();
            r->event=(vex_viewport_event_t){.type=VEX_VIEWPORT_CONFIGURE,.x=b.x,.y=b.y,.width=b.w,.height=b.h};return 0;
        }
        if(r->operation==VEX_VIEWPORT_PRESENT) {
            if(r->tab_id && r->tab_id!=gui_browser_active_tab())return (uint64_t)-11;
            compositor_surface_info_t info;vex_rect_t b;
            if(compositor_surface_query(scheduler_current_process_id(),r->surface,&info)!=COMPOSITOR_SURFACE_OK){debug_log_puts("viewport-present: query failed\n");return (uint64_t)-1;}
            gui_browser_web_content_ready();
            if(!browser_viewport_bounds(&b))return (uint64_t)-11; /* hidden: retain frame until visible */
            vex_gpu_surface_present_req_t p={0};p.surface_handle=r->surface;
            p.source=(vex_rect_t){0,0,info.desc.width,info.desc.height};p.destination=b;p.opacity=255;
            const uint64_t present_start_ns=profiler_active?sched_clock_ns():0u;
            int result=vex_gpu_surface_present_internal(scheduler_current_process(),&p,NULL,1);
            if(present_start_ns && !result) {
                static uint64_t previous_present_ns;
                const uint64_t finished_ns=sched_clock_ns();
                profiler_latency(PROF_LAT_PRESENT_COST,finished_ns-present_start_ns);
                if(previous_present_ns)profiler_latency(PROF_LAT_WEB_PRESENT_GAP,present_start_ns-previous_present_ns);
                previous_present_ns=present_start_ns;
            }
            if(result)debug_log_puts("viewport-present: composition failed\n");
            return result==0?0:(uint64_t)-1;
        }
        return (uint64_t)-1;
    }
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
    case VEX_SYS_PROCESS_SPAWN_SHARED:
        {
            const uint32_t child_pid = process_fork_current_with_stack(arg1);
            return child_pid == 0u ? (uint64_t)-1 : (uint64_t)child_pid;
        }
    case VEX_SYS_PROCESS_EXEC:
        return vex_sys_process_exec(arg1);
    case VEX_SYS_THREAD_CREATE:
        return vex_sys_thread_create(arg1, arg2);
    case VEX_SYS_THREAD_JOIN:
        return vex_sys_thread_join(arg1, arg2);
    case VEX_SYS_THREAD_EXIT:
        process_exit_status((int)arg1);
        return 0u;
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
    case VEX_SYS_MESSAGE_OBSERVE: {
        vex_message_observe_req_t *r=(void *)arg1;
        if(r->reserved || r->capacity>65536 || (!r->prefix && r->capacity))return vex_negative(EINVAL);
        VEX_SCRATCH(lease);uint8_t local[512];
        void *data=r->capacity<=sizeof(local)?local:vex_scratch_allocate(&lease,r->capacity);
        if(!data)return vex_negative(ENOMEM);
        vex_message_observe_req_t snapshot=*r;snapshot.prefix=data;snapshot.reserved_out=0;
        int e=vex_fd_message_observe(r->fd,&snapshot);
        if(e)return (uint64_t)(int64_t)e;
        if(snapshot.copied && vex_copy_to_user((uintptr_t)r->prefix,data,snapshot.copied))return vex_negative(EFAULT);
        snapshot.prefix=r->prefix;*r=snapshot;return 0;
    }
    case VEX_SYS_MESSAGE_PAIR:
        return vex_sys_message_pair(arg1);
    case VEX_SYS_MESSAGE_SEND:
    case VEX_SYS_MESSAGE_RECV_AUTH:
    case VEX_SYS_MESSAGE_RECV:
        return vex_sys_message_io(arg1, number);
    case VEX_SYS_DISPLAY_OPEN:
        if(ladybird_browser_process_pid() && scheduler_current_process_id()!=ladybird_browser_process_pid())return (uint64_t)-16;
        return (uint64_t)ladybird_display_open((uint32_t)arg1, (uint32_t)arg2);
    case VEX_SYS_DISPLAY_RESIZE:
        if(scheduler_current_process_id()!=ladybird_browser_process_pid())return (uint64_t)-1;
        ladybird_display_resize((uint32_t)arg1, (uint32_t)arg2);
        return 0;
    case VEX_SYS_DISPLAY_PRESENT:
        return vex_sys_display_present(arg1);
    case VEX_SYS_DISPLAY_CLOSE:
        if(scheduler_current_process_id()!=ladybird_browser_process_pid())return (uint64_t)-1;
        ladybird_display_close();
        return 0;
    case VEX_SYS_APP_WINDOW_OPEN:
        return vex_sys_app_window_open(arg1);
    case VEX_SYS_APP_WINDOW_PRESENT:
        return vex_sys_app_window_present(arg1);
    case VEX_SYS_APP_WINDOW_POLL:
        return vex_sys_app_window_poll(arg1);
    case VEX_SYS_APP_WINDOW_CLOSE:
        return vex_sys_app_window_close(arg1);
    case VEX_SYS_APP_WINDOW_RESIZE:
        return vex_sys_app_window_resize(arg1);
    case VEX_SYS_SHM_CREATE:
        return vex_sys_shm_create(arg1);
    case VEX_SYS_SHM_OPEN:
        return vex_sys_shm_open(arg1);
    case VEX_SYS_SHM_CLOSE:
        return vex_sys_shm_close(arg1);
    case VEX_SYS_SHM_MAP:
        return vex_sys_shm_map(arg1);
    case VEX_SYS_EVENT_REGISTER: {
        const vex_event_register_req_t *req = (const vex_event_register_req_t *)(uintptr_t)arg1;
        vex_event_registration_t registration;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        registration.handler = req->handler;
        registration.restorer = req->restorer;
        registration.flags = req->flags;
        registration.delivery_value = req->delivery_value;
        return (uint64_t)vex_event_register(req->event_id, &registration);
    }
    case VEX_SYS_EVENT_MASK: {
        vex_event_mask_req_t *req = (vex_event_mask_req_t *)(uintptr_t)arg1;
        uint64_t old_mask = 0u;
        if (req == NULL || vex_event_set_mask(req->how, req->set, &old_mask) != 0) {
            return (uint64_t)-1;
        }
        req->old_mask = old_mask;
        return 0u;
    }
    case VEX_SYS_EVENT_POST: {
        const vex_event_post_req_t *req = (const vex_event_post_req_t *)(uintptr_t)arg1;
        if (req == NULL) {
            return (uint64_t)-1;
        }
        return (uint64_t)vex_event_post(req->process_id, req->thread_id, req->event_id);
    }
    case VEX_SYS_EVENT_RETURN:
        return (uint64_t)vex_event_sigreturn();
    case VEX_SYS_GPU_QUERY_INFO:
        return vex_sys_gpu_query_info(arg1);
    case VEX_SYS_GPU_QUERY_CAPS:
        return vex_sys_gpu_query_caps(arg1);
    case VEX_SYS_GPU_BUFFER_CREATE:
        return vex_sys_gpu_buffer_create(arg1);
    case VEX_SYS_GPU_BUFFER_DESTROY:
        return vex_sys_gpu_buffer_destroy(arg1);
    case VEX_SYS_GPU_QUEUE_SUBMIT:
        return vex_sys_gpu_queue_submit(arg1);
    case VEX_SYS_GPU_QUEUE_CREATE: {
        uint64_t handle = 0u;
        return gpu_queue_create(scheduler_current_process_id(),
            (uint32_t)arg1, &handle) == 0 ? handle : (uint64_t)-1;
    }
    case VEX_SYS_GPU_QUEUE_DESTROY:
        return gpu_queue_destroy(scheduler_current_process_id(), arg1) == 0
            ? 0u : (uint64_t)-1;
    case VEX_SYS_GPU_SUBMIT_BEGIN: {
        uint64_t serial = 0u;
        return gpu_queue_submit_begin(scheduler_current_process_id(), arg1,
            &serial) == 0 ? serial : (uint64_t)-1;
    }
    case VEX_SYS_GPU_SUBMIT_COMPLETE:
        return gpu_queue_submit_complete(scheduler_current_process_id(),
            arg1, arg2, (int32_t)arg3) == 0 ? 0u : (uint64_t)-1;
    case VEX_SYS_GPU_FENCE_QUERY:
        return (uint64_t)gpu_queue_fence_query(
            scheduler_current_process_id(), arg1, arg2);
    case VEX_SYS_GPU_FENCE_WAIT:
        return (uint64_t)gpu_queue_fence_wait(
            scheduler_current_process_id(), arg1, arg2, arg3);
    case VEX_SYS_EVENT_COUNTER_CREATE: {
        uint64_t capability = 0u;
        return vex_event_counter_create_owned(
            scheduler_current_process_id(), arg1, (uint32_t)arg2,
            &capability) == 0 ? capability : (uint64_t)-1;
    }
    case VEX_SYS_EVENT_COUNTER_SIGNAL:
        return vex_event_counter_authorize_owner(
                   scheduler_current_process_id(), arg1) == 0 &&
               vex_event_counter_signal(arg1, arg2) == 0
            ? 0u : (uint64_t)-1;
    case VEX_SYS_EVENT_COUNTER_READ: {
        process_t *process = scheduler_current_process();
        vex_event_counter_read_req_t req;
        uint64_t value = 0u;
        int result;
        if (process == NULL ||
            !vex_request_readable(process,arg1,sizeof(req)))
            return (uint64_t)-1;
        memcpy(&req, (const void *)(uintptr_t)arg1, sizeof(req));
        if (req.reserved0 != 0u || req.value_out == NULL ||
            vex_event_counter_authorize_owner(
                scheduler_current_process_id(), req.capability) != 0 ||
            !vex_user_range_is_writable(process,
                (uint64_t)(uintptr_t)req.value_out, sizeof(value)))
            return (uint64_t)-1;
        result = vex_event_counter_read_timed(req.capability, &value,
            req.flags, req.timeout_ticks);
        if (result == 0) memcpy(req.value_out, &value, sizeof(value));
        return (uint64_t)result;
    }
    case VEX_SYS_EVENT_COUNTER_RELEASE:
        return vex_event_counter_release_owned(
            scheduler_current_process_id(), arg1) == 0
            ? 0u : (uint64_t)-1;
    case VEX_SYS_GPU_PRESENT:
        return vex_sys_gpu_present(arg1);
    case VEX_SYS_GPU_SURFACE_CREATE:
        return vex_sys_gpu_surface_create(arg1);
    case VEX_SYS_GPU_SURFACE_DESTROY:
        return vex_sys_gpu_surface_destroy(arg1);
    case VEX_SYS_GPU_SURFACE_PRESENT:
        return vex_sys_gpu_surface_present(arg1);
    case VEX_SYS_GPU_SURFACE_WAIT:
        return vex_sys_gpu_surface_wait(arg1);
    case VEX_SYS_SURFACE_MAP:
        return vex_sys_surface_map(arg1);
    case VEX_SYS_SURFACE_RETAIN:
        return vex_sys_surface_retain(arg1);
    case VEX_SYS_SURFACE_QUERY:
        return vex_sys_surface_query(arg1);
    case VEX_SYS_SURFACE_UNMAP:
        return vex_sys_surface_unmap(arg1);
    case VEX_SYS_ELEVATION_REQUEST:
        return vex_sys_elevation_request(arg1);
    case VEX_SYS_ELEVATION_STATUS:
        return vex_sys_elevation_status(arg1);
    case VEX_SYS_THREAD_START:
        return vex_sys_thread_start(arg1);
    case VEX_SYS_THREAD_EXIT_UNMAP:
        return vex_sys_thread_exit_unmap(arg1);
    case VEX_SYS_FUTEX:
        return vex_sys_futex(arg1);
    case VEX_SYS_THREAD_SET_TLS:
        if (arg1 >= VMM_USER_ADDRESS_LIMIT) return vex_negative(EINVAL);
        scheduler_set_current_fs_base(arg1);
        return 0u;
    case VEX_SYS_THREAD_CONTEXT: {
        vex_thread_context_req_t *r=(void *)(uintptr_t)arg1;
        if(r->size!=sizeof(*r) || r->reserved || r->operation>VEX_THREAD_CONTEXT_SET ||
            (r->mask&~3u) || (!r->operation && r->mask) || (r->operation && !r->mask))
            return vex_negative(EINVAL);
        if(r->operation) {
            if(((r->mask&1u) && r->fs_base>=VMM_USER_ADDRESS_LIMIT) ||
               ((r->mask&2u) && r->gs_base>=VMM_USER_ADDRESS_LIMIT))return vex_negative(EINVAL);
            if(r->mask&1u)scheduler_set_current_fs_base(r->fs_base);
            if(r->mask&2u)scheduler_set_current_user_gs_base(r->gs_base);
        }
        r->fs_base=scheduler_current_fs_base();r->gs_base=scheduler_current_user_gs_base();
        r->tid=scheduler_current_tid();r->cpu=smp_current_index();
        process_memory_region_t region;
        uint64_t base,file_offset;uint32_t kind;
        r->stack_low=r->stack_high=0u;
        /* get_region is an exact-base lookup. Reuse the existing containing
           region index rather than pretending the live RSP is a region key. */
        if(process_memory_describe_address(scheduler_current_process_id(),scheduler_current_user_rsp(),
              &base,&kind,&file_offset,NULL,0)==0 &&
           process_memory_get_region(scheduler_current_process_id(),base,&region)==0) {
            r->stack_low=region.addr;r->stack_high=region.addr+region.length;
        }
        return 0u;
    }
    case VEX_SYS_THREAD_STOP: {
        const vex_thread_stop_req_t *r=(const void *)(uintptr_t)arg1;
        return (uint64_t)(int64_t)scheduler_stop_own_thread(r->tid,r->status);
    }
    case VEX_SYS_THREAD_CONTROL:
        return (uint64_t)(int64_t)scheduler_thread_control((void *)(uintptr_t)arg1);
    case VEX_SYS_THREAD_EXIT_WORD: {
        kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
        if(arg1 && ((arg1&3u) || !vex_user_range_is_writable(process,arg1,4)))return vex_negative(EFAULT);
        process_set_clear_child_tid(arg1);return 0u;
    }
    case VEX_SYS_FS_ROOT: {
        vex_fs_root_req_t *r=(void *)(uintptr_t)arg1;
        if(r->size!=sizeof(*r) || r->reserved)return vex_negative(EINVAL);
        if(r->operation==VEX_FS_ROOT_SEAL || r->operation==VEX_FS_ROOT_POLICY) {
            if(r->fd || r->rights || r->path || r->flags || r->mode || r->out || !process || !process->group)return vex_negative(EINVAL);
            if(r->operation==VEX_FS_ROOT_POLICY)return __atomic_load_n(&process->group->fs_sealed,__ATOMIC_ACQUIRE);
            kernel_guard_t ns __attribute__((cleanup(fs_namespace_end)))=fs_namespace_begin();
            __atomic_store_n(&process->group->fs_sealed,1,__ATOMIC_RELEASE);return 0u;
        }
        if(r->operation==VEX_FS_ROOT_QUERY) {
            if(r->rights || r->path || r->flags || r->mode || !r->out)return vex_negative(EINVAL);
            vex_fs_root_info_t info;
            if(!vex_user_range_is_writable(process,(uintptr_t)r->out,sizeof(info)))return vex_negative(EFAULT);
            int result=vex_fs_root_query(r->fd,&info);if(result)return (uint64_t)(int64_t)result;
            return vex_copy_to_user((uintptr_t)r->out,&info,sizeof(info))?vex_negative(EFAULT):0u;
        }
        char path[512];
        if(!r->path || vex_user_string_copy(path,sizeof(path),(uintptr_t)r->path))return vex_negative(EFAULT);
        int result;
        switch(r->operation) {
        case VEX_FS_ROOT_CREATE:
            if(r->fd || r->flags || r->mode || r->out)return vex_negative(EINVAL);
            result=vex_fs_root_create_fd(path,r->rights);break;
        case VEX_FS_ROOT_DERIVE:
            if(r->flags || r->mode || r->out)return vex_negative(EINVAL);
            result=vex_fs_root_derive(r->fd,path,r->rights);break;
        case VEX_FS_ROOT_OPEN:
            if(r->rights || r->out)return vex_negative(EINVAL);
            result=vex_fs_root_open(r->fd,path,r->flags,r->mode);break;
        case VEX_FS_ROOT_MKDIR:
            if(r->rights || r->flags || r->out)return vex_negative(EINVAL);
            result=vex_fs_root_mkdir(r->fd,path,r->mode);break;
        case VEX_FS_ROOT_REMOVE:
            if(r->rights || r->mode || r->out)return vex_negative(EINVAL);
            result=vex_fs_root_remove(r->fd,path,r->flags);break;
        case VEX_FS_ROOT_MOVE:
        case VEX_FS_ROOT_REPLACE: {
            if(r->rights || r->mode || r->flags || !r->out)return vex_negative(EINVAL);
            vex_fs_root_names_t names;char destination[512],backup[512];
            if(vex_copy_from_user(&names,(uintptr_t)r->out,sizeof(names)) || !names.destination ||
               vex_user_string_copy(destination,sizeof(destination),(uintptr_t)names.destination) ||
               (names.backup && vex_user_string_copy(backup,sizeof(backup),(uintptr_t)names.backup)))return vex_negative(EFAULT);
            if(names.backup && r->operation!=VEX_FS_ROOT_REPLACE)return vex_negative(EINVAL);
            result=vex_fs_root_move(r->fd,path,destination,names.backup?backup:NULL,r->operation==VEX_FS_ROOT_REPLACE);break;
        }
        case VEX_FS_ROOT_MODE:
            if(r->rights || r->out)return vex_negative(EINVAL);
            result=vex_fs_root_mode(r->fd,path,r->flags,r->mode);break;
        case VEX_FS_ROOT_STAT: {
            if(r->rights || r->flags || r->mode || !r->out)return vex_negative(EINVAL);
            vex_stat_ex_t stat;
            if(!vex_user_range_is_writable(process,(uintptr_t)r->out,sizeof(stat)))return vex_negative(EFAULT);
            result=vex_fs_root_stat(r->fd,path,&stat);if(result)break;
            return vex_copy_to_user((uintptr_t)r->out,&stat,sizeof(stat))?vex_negative(EFAULT):0u;
        }
        default:return vex_negative(EINVAL);
        }
        return (uint64_t)(int64_t)result;
    }
    case VEX_SYS_MEMORY_VIEW: {
        vex_memory_view_req_t *r=(void *)(uintptr_t)arg1;
        if(r->size!=sizeof(*r) || r->reserved || !process || !process->user_mode)return vex_negative(EINVAL);
        if(r->operation==VEX_MEMORY_VIEW_USAGE){
            if(r->address || r->length || r->permissions || !r->out)return vex_negative(EINVAL);
            vex_memory_usage_t info={0};vmm_memory_usage_t vm;
            if(vmm_memory_usage(vmm_cr3_to_address_space(process->cr3),&vm))return vex_negative(EIO);
            info.resident_bytes=vm.resident;info.peak_resident_bytes=vm.peak_resident;
            info.private_bytes=vm.private_bytes;info.peak_private_bytes=vm.peak_private;info.materializations=vm.materializations;
            if(process_memory_commit_usage(scheduler_current_memory_owner_pid(),&info.private_charge_bytes,&info.peak_private_charge_bytes) ||
               pmm_commit_usage(&info.committed_bytes,&info.peak_committed_bytes))return vex_negative(75); /* EOVERFLOW */
            pmm_memory_usage(&info.physical_bytes,&info.available_bytes,&info.peak_used_bytes);
            kernel_allocator_usage(scheduler_current_memory_owner_pid(),&info.allocator_bytes,&info.owned_allocator_bytes,&info.peak_owned_allocator_bytes);
            info.allocator_bytes+=heap_bootstrap_bytes();info.cache_bytes=fs_cached_bytes();info.resource_handles=vex_fd_live_count();
            scheduler_inventory_counts(&info.processes,&info.threads);
            return vex_copy_to_user((uintptr_t)r->out,&info,sizeof(info))?vex_negative(EFAULT):0;
        }
        kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
        if(r->operation==VEX_MEMORY_VIEW_MACHINE) {
            if(r->address || r->length || r->permissions || !r->out)return vex_negative(EINVAL);
            vex_memory_machine_info_t info={VMM_USER_ADDRESS_LIMIT,VEX_USER_PAGE_SIZE,smp_cpu_count(),0};
            uint32_t low,high;uint64_t control;
            __asm__ volatile("rdmsr":"=a"(low),"=d"(high):"c"(0xc0000080u));
            __asm__ volatile("mov %%cr4,%0":"=r"(control));
            if(low&(1u<<11))info.flags|=VEX_MEMORY_MACHINE_NX;
            if(control&(1ull<<9))info.flags|=VEX_MEMORY_MACHINE_FXSAVE;
            return vex_copy_to_user((uintptr_t)r->out,&info,sizeof(info))?vex_negative(EFAULT):0;
        }
        if(r->operation==VEX_MEMORY_VIEW_CPU) {
            if(r->length || r->permissions || !r->out || r->address>=smp_cpu_count())return vex_negative(EINVAL);
            vex_cpu_t *cpu=smp_cpu_at((uint32_t)r->address);if(!cpu)return vex_negative(EINVAL);
            vex_cpu_topology_info_t info=cpu->topology;info.online=__atomic_load_n(&cpu->online,__ATOMIC_ACQUIRE);
            if(!info.valid)return vex_negative(ENOTSUP);
            return vex_copy_to_user((uintptr_t)r->out,&info,sizeof(info))?vex_negative(EFAULT):0;
        }
        if(r->operation==VEX_MEMORY_VIEW_CAPACITY) {
            if(r->address || r->length || r->permissions || !r->out)return vex_negative(EINVAL);
            vex_memory_capacity_t info={.virtual_begin=65536,.virtual_end=VMM_USER_ADDRESS_LIMIT};
            pmm_memory_snapshot(&info.physical_bytes,&info.available_physical_bytes);
            info.available_virtual_bytes=process_memory_available_va(scheduler_current_memory_owner_pid(),info.virtual_begin,info.virtual_end);
            return vex_copy_to_user((uintptr_t)r->out,&info,sizeof(info))?vex_negative(EFAULT):0;
        }
        if(r->operation==VEX_MEMORY_VIEW_DISCARD) {
            if(r->out || r->permissions)return vex_negative(EINVAL);
            return (uint64_t)(int64_t)process_memory_discard_private(scheduler_current_memory_owner_pid(),process->cr3,r->address,r->length);
        }
        if(r->operation==VEX_MEMORY_VIEW_COMMIT || r->operation==VEX_MEMORY_VIEW_DECOMMIT){
            if(r->out)return vex_negative(EINVAL);
            return (uint64_t)(int64_t)process_memory_private_state(scheduler_current_memory_owner_pid(),process->cr3,r->address,r->length,r->permissions,r->operation==VEX_MEMORY_VIEW_COMMIT);
        }
        if(r->operation==VEX_MEMORY_VIEW_PAGE) {
            if(r->length || r->permissions || !r->out)return vex_negative(EINVAL);
            vex_memory_page_info_t info;int rc=process_memory_page_info(scheduler_current_memory_owner_pid(),process->cr3,r->address,&info);
            if(rc)return (uint64_t)(int64_t)rc;
            return vex_copy_to_user((uintptr_t)r->out,&info,sizeof(info))?vex_negative(EFAULT):0;
        }
        if(r->operation==VEX_MEMORY_VIEW_PATH) {
            if(!r->out || !r->length || r->length>512 || r->permissions || r->address>=VMM_USER_ADDRESS_LIMIT)return vex_negative(EINVAL);
            char path[512];uint64_t base,offset;uint32_t kind;
            if(process_memory_describe_address(scheduler_current_memory_owner_pid(),r->address,&base,&kind,&offset,path,sizeof(path)) || !path[0])return vex_negative(ENOENT);
            size_t bytes=strlen(path)+1;if(bytes>r->length)return vex_negative(ERANGE);
            return vex_copy_to_user((uintptr_t)r->out,path,bytes)?vex_negative(EFAULT):0;
        }
        if(r->operation==VEX_MEMORY_VIEW_QUERY) {
            if(r->length || r->permissions || !r->out)return vex_negative(EINVAL);
            if(r->address>=VMM_USER_ADDRESS_LIMIT)return vex_negative(EFAULT);
            vex_memory_view_info_t info;
            if(process_memory_query_view(scheduler_current_memory_owner_pid(),r->address,&info))return vex_negative(EIO);
            if(info.kind==VEX_MEMORY_VIEW_MAPPED) {
                uint64_t flags=vmm_get_page_flags(vmm_cr3_to_address_space(process->cr3),info.base);
                info.present=(flags&(VMM_PAGE_USER|VMM_PAGE_PRESENT))==(VMM_PAGE_USER|VMM_PAGE_PRESENT);
            }
            return vex_copy_to_user((uintptr_t)r->out,&info,sizeof(info))?vex_negative(EFAULT):0;
        }
        if(r->operation==VEX_MEMORY_VIEW_ACCESS) {
            if(r->out || !r->permissions || (r->permissions&~3u))return vex_negative(EINVAL);
            if(r->length>VEX_MEMORY_COPY_MAX)return vex_negative(E2BIG);
            return vex_memory_view_access(process,r->address,r->length,r->permissions)?0:vex_negative(EFAULT);
        }
        if((r->operation!=VEX_MEMORY_VIEW_READ && r->operation!=VEX_MEMORY_VIEW_WRITE) || r->permissions)return vex_negative(EINVAL);
        if(r->length>VEX_MEMORY_COPY_MAX)return vex_negative(E2BIG);
        uint64_t source=r->operation==VEX_MEMORY_VIEW_READ?r->address:(uintptr_t)r->out;
        uint64_t destination=r->operation==VEX_MEMORY_VIEW_READ?(uintptr_t)r->out:r->address;
        if(!vex_memory_view_access(process,source,r->length,1) || !vex_memory_view_access(process,destination,r->length,2))return vex_negative(EFAULT);
        if(r->length)memmove((void *)(uintptr_t)destination,(const void *)(uintptr_t)source,(size_t)r->length);
        return 0;
    }
    case VEX_SYS_USER_IDENTITY: {
        vex_user_identity_t *r=(void *)(uintptr_t)arg1;
        if(r->size!=sizeof(*r) || r->reserved)return vex_negative(EINVAL);
        r->uid=scheduler_current_uid();r->capabilities=scheduler_current_capabilities();
        const char *name=user_name_for_uid(r->uid);
        if(!name || !strcmp(name,"unknown"))return vex_negative(ENOENT);
        snprintf(r->name,sizeof(r->name),"%s",name);
        snprintf(r->home,sizeof(r->home),"/home/%s",name);return 0u;
    }
    case VEX_SYS_RENDEZVOUS: {
        vex_rendezvous_req_t *r=(void *)(uintptr_t)arg1;uint8_t label[512];
        if(r->size!=sizeof(*r) || r->label_size>sizeof(label))return vex_negative(EINVAL);
        if(r->label_size && vex_copy_from_user(label,(uintptr_t)r->label,r->label_size))return vex_negative(EFAULT);
        if(r->operation==VEX_RENDEZVOUS_QUERY && !vex_request_writable(process,arg1,sizeof(*r)))return vex_negative(EFAULT);
        return (uint64_t)vex_rendezvous_call(r,label);
    }
    case VEX_SYS_PROCESS_RESOURCE:return (uint64_t)vex_process_resource_call((const void *)(uintptr_t)arg1);
    case VEX_SYS_EXECUTION_GATE: {
        const vex_execution_req_t *r=(void *)(uintptr_t)arg1;
        if(r->size!=sizeof(*r) || r->reserved)return vex_negative(EINVAL);
        return (uint64_t)scheduler_execution_gate(r->operation,r->capacity,r->capability,r->timeout_ns);
    }
    case VEX_SYS_THREAD_SELF:
        return (uint64_t)scheduler_current_tid();
    case VEX_SYS_LAST_ERROR:
        return (uint64_t)(uint32_t)*ladybird_errno_location();
    case VEX_SYS_PROCESS_EXIT_GROUP:
        process_exit_group((int)arg1);
        return 0u;
    case VEX_SYS_STAT_EX:
        return vex_sys_stat_ex(arg1);
    case VEX_SYS_OPEN_EX:
        return vex_sys_open_ex(arg1);
    case VEX_SYS_RESOLVE_AT:
        return vex_sys_resolve_at(arg1);
    case VEX_SYS_CPU_TIME:
        if (arg1 > VEX_CPU_TIME_PROCESS) return vex_negative(EINVAL);
        return scheduler_cpu_time_ns(arg1 == VEX_CPU_TIME_PROCESS);
    case VEX_SYS_GETDENTS:
        return vex_sys_getdents(arg1);
    case VEX_SYS_FILE_RANGE: {
        vex_file_range_req_t *r=(void *)(uintptr_t)arg1;
        if(r->size!=sizeof(*r) || r->mode>VEX_FILE_RANGE_WRITE || (r->flags&~(VEX_FILE_RANGE_WAIT|VEX_FILE_RANGE_DESCRIPTION)) ||
           (r->mode==VEX_FILE_RANGE_RELEASE && (r->flags&VEX_FILE_RANGE_WAIT)) || r->offset>INT64_MAX || r->length>INT64_MAX)return vex_negative(EINVAL);
        if(r->flags&VEX_FILE_RANGE_DESCRIPTION)return (uint64_t)(int64_t)vex_fd_description_range(r->fd,r->mode,r->offset,r->length,(r->flags&VEX_FILE_RANGE_WAIT)!=0);
        int result=vex_fd_group_range(r->fd,r->mode,r->offset,r->length,(r->flags&VEX_FILE_RANGE_WAIT)!=0);
        return result?vex_negative(*ladybird_errno_location()):0u;
    }
    case VEX_SYS_EVENT_ALT_STACK:
        return (uint64_t)(int64_t)vex_event_set_alt_stack((vex_event_alt_stack_req_t *)(uintptr_t)arg1);
    case VEX_SYS_EVENT_LEAVE:
        return (uint64_t)(int64_t)vex_event_leave();
    case VEX_SYS_SOCKET_OP:
        return vex_sys_socket_op(arg1);
    case VEX_SYS_RESOLVE:
        return vex_sys_resolve(arg1);
    case VEX_SYS_THREAD_NAME:
        return vex_sys_thread_name(arg1);
    case VEX_SYS_SET_TIMES:
        return vex_sys_set_times(arg1);
    case VEX_SYS_FCHMOD:
        return vex_sys_fchmod(arg1);
    default:
        return (uint64_t)-1;
    }
}

uint64_t vex_syscall_dispatch(uint64_t number, uint64_t arg1,
                               uint64_t arg2, uint64_t arg3) {
    process_t *process = scheduler_current_process();
    const uint8_t previous = process == NULL ? 0u : process->in_user_syscall;
    uint64_t result;

    if (process != NULL) {
        process->in_user_syscall = 0u;
    }
    result = vex_syscall_dispatch_impl(number, arg1, arg2, arg3, 0);
    if (process != NULL) {
        process->in_user_syscall = previous;
    }
    return result;
}

/* A confined service uses the same native primitives, not another kernel.
 * Keep ambient UI/device/global inspection entry points out of its projection. */
static int ultra_native_allowed(uint64_t n){
    if(n==VEX_SYS_PROCESS_EXIT_GROUP || n==VEX_SYS_LAST_ERROR || n==VEX_SYS_USER_IDENTITY || n==VEX_SYS_THREAD_START || n==VEX_SYS_THREAD_EXIT || n==VEX_SYS_THREAD_EXIT_UNMAP || n==VEX_SYS_FUTEX || n==VEX_SYS_THREAD_YIELD)return 1;
    if((n>=5 && n<=9) || n==12 || (n>=20 && n<=23) || (n>=26 && n<=28) ||
       (n>=30 && n<=39) || n==50 || (n>=84 && n<=88) || n==90 ||
       n==103 || n==104 || (n>=180 && n<=182) || (n>=225 && n<=229) || n==232 || n==233)return 1;
    return 0;
}
static void native_request_memory_end(kernel_guard_t *guard){if(guard->gate)process_memory_end_update(guard);}
uint64_t vex_syscall_dispatch_user(uint64_t number, uint64_t arg1,
                                    uint64_t arg2, uint64_t arg3) {
    process_t *process = scheduler_current_process();
    const uint8_t previous = process == NULL ? 0u : process->in_user_syscall;
    uint64_t result;

    if (process != NULL) {
        process->in_user_syscall = 1u;
    }
    if(process && process->group && process->group->ultra_space && !ultra_native_allowed(number)){process->in_user_syscall=previous;return (uint64_t)-EACCES;}
    /* The error slot describes the most recent native call only. */
    if (number != VEX_SYS_LAST_ERROR) *ladybird_errno_location() = 0;
    /* Console publication cannot precede permission checks for its returned
       lease. Keep the existing memory transaction through request copy-out. */
    kernel_guard_t request_memory __attribute__((cleanup(native_request_memory_end)))={0};
    if(number==VEX_SYS_CONSOLE || number==VEX_SYS_THREAD_CONTEXT)request_memory=process_memory_begin_update();
    const size_t size=vex_syscall_request_size(number);
    if(size) {
        uint64_t record[(size+sizeof(uint64_t)-1u)/sizeof(uint64_t)];
        if((number==VEX_SYS_CONSOLE || number==VEX_SYS_THREAD_CONTEXT) && !vex_user_range_is_writable(process,arg1,size)) {*ladybird_errno_location()=EFAULT;result=(uint64_t)-14;}
        else if(number==VEX_SYS_THREAD_CONTROL && !vex_syscall_user_range_valid(arg1,size,1)) {*ladybird_errno_location()=EFAULT;result=(uint64_t)-14;}
        else if(vex_copy_from_user(record,arg1,size)) {*ladybird_errno_location()=EFAULT;result=(uint64_t)-1;}
        else {
            void *saved=process->native_request_snapshot;
            size_t saved_size=process->native_request_snapshot_size;
            uint64_t saved_address=process->native_request_user_address;
            process->native_request_snapshot=record;process->native_request_snapshot_size=size;
            process->native_request_user_address=arg1;
            result=vex_syscall_dispatch_impl(number,(uint64_t)(uintptr_t)record,arg2,arg3,0);
            process->native_request_snapshot=saved;process->native_request_snapshot_size=saved_size;
            process->native_request_user_address=saved_address;
            const int rendezvous_query=number==VEX_SYS_RENDEZVOUS && ((vex_rendezvous_req_t *)record)->operation==VEX_RENDEZVOUS_QUERY;
            if(number!=VEX_SYS_MESSAGE_RECV && number!=VEX_SYS_MESSAGE_RECV_AUTH && (vex_syscall_request_is_writable(number) || rendezvous_query) && vex_copy_to_user(arg1,record,size)) {
                *ladybird_errno_location()=EFAULT;result=(uint64_t)-1;
            }
        }
    } else result=vex_syscall_dispatch_impl(number,arg1,arg2,arg3,1);
    if (process != NULL) {
        process->in_user_syscall = previous;
    }
    return result;
}
