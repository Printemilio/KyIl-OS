#include <stddef.h>
#include <stdint.h>

#include <unos/vex_abi.h>
#include <unos/vex_legacy_compat.h>

#include "vex_syscall.h"

#define PAGE_SIZE 4096u
#define USER_LIMIT 0x0000800000000000ull
#define KERNEL_POINTER 0xffff800000000000ull

static void log_text(const char *text) {
    (void)vex_syscall1(VEX_SYS_DEBUG_LOG, (uint64_t)(uintptr_t)text);
}

static void log_result(const char *name, int ok) {
    log_text("security_boundary: ");
    log_text(name);
    log_text(ok ? " ok\n" : " FAIL\n");
}

static int is_failed(uint64_t value) {
    return value == (uint64_t)-1 || value == (uint64_t)(uintptr_t)MAP_FAILED;
}

static void fill_return_code(uint8_t *code) {
    code[0] = 0xb8; /* mov eax, 42 */
    code[1] = 42;
    code[2] = 0;
    code[3] = 0;
    code[4] = 0;
    code[5] = 0xc3; /* ret */
}

static uint64_t syscall_with_invalid_rsp(uint64_t number) {
    uint64_t result;

    /* The entry path must copy this value before touching the user stack.  It
       is canonical user space but intentionally unmapped. */
    __asm__ volatile (
        "mov %%rsp, %%r12\n"
        "mov $0x0000700000000000, %%rsp\n"
        "syscall\n"
        "mov %%r12, %%rsp\n"
        : "=a"(result)
        : "a"(number)
        : "rcx", "r11", "r12", "memory"
    );
    return result;
}

int main(void) {
    int failures = 0;
    uint64_t result;
    vex_mmap_req_t mmap_request = { 0 };
    vex_memory_req_t memory_request = { 0 };
    uint32_t sequence = 0u;

    result = vex_syscall1(VEX_SYS_MMAP, 1u);
    log_result("invalid_request_pointer", is_failed(result));
    failures += !is_failed(result);

    result = vex_syscall1(VEX_SYS_MMAP, KERNEL_POINTER);
    log_result("kernel_request_pointer", is_failed(result));
    failures += !is_failed(result);

    mmap_request.addr = (void *)(uintptr_t)KERNEL_POINTER;
    mmap_request.length = PAGE_SIZE;
    mmap_request.prot = PROT_READ | PROT_WRITE;
    mmap_request.flags = MAP_PRIVATE | MAP_ANONYMOUS;
    mmap_request.fd = -1;
    result = vex_syscall1(VEX_SYS_MMAP, (uint64_t)(uintptr_t)&mmap_request);
    log_result("mmap_kernel_preferred", is_failed(result));
    failures += !is_failed(result);

    mmap_request.addr = (void *)(uintptr_t)(USER_LIMIT - PAGE_SIZE);
    mmap_request.length = PAGE_SIZE * 2u;
    result = vex_syscall1(VEX_SYS_MMAP, (uint64_t)(uintptr_t)&mmap_request);
    log_result("mmap_crosses_user_limit", is_failed(result));
    failures += !is_failed(result);

    memory_request.addr = (void *)(uintptr_t)KERNEL_POINTER;
    memory_request.length = PAGE_SIZE;
    memory_request.prot_or_advice = PROT_READ;
    result = vex_syscall1(VEX_SYS_MPROTECT, (uint64_t)(uintptr_t)&memory_request);
    log_result("mprotect_kernel_range", is_failed(result));
    failures += !is_failed(result);
    result = vex_syscall1(VEX_SYS_MUNMAP, (uint64_t)(uintptr_t)&memory_request);
    log_result("munmap_kernel_range", is_failed(result));
    failures += !is_failed(result);

    /* A 128-GiB anonymous reservation must consume VA metadata only.  Touch
     * two pages to exercise fault-time materialization, then toggle
     * protection and unmap the whole sparse range. */
    {
        volatile uint8_t *sparse;
        const size_t sparse_size = (size_t)128u * 1024u * 1024u * 1024u;
        mmap_request.addr = 0;
        mmap_request.length = sparse_size;
        mmap_request.prot = PROT_READ | PROT_WRITE;
        mmap_request.flags = MAP_PRIVATE | MAP_ANONYMOUS;
        mmap_request.fd = -1;
        mmap_request.offset = 0;
        result = vex_syscall1(VEX_SYS_MMAP, (uint64_t)(uintptr_t)&mmap_request);
        if (is_failed(result)) {
            log_result("lazy_128g_reservation", 0);
            ++failures;
        } else {
            sparse = (volatile uint8_t *)(uintptr_t)result;
            sparse[0] = 0x5a;
            sparse[sparse_size - PAGE_SIZE] = 0xa5;
            memory_request.addr = (void *)(uintptr_t)result;
            memory_request.length = sparse_size;
            memory_request.prot_or_advice = PROT_NONE;
            result = vex_syscall1(VEX_SYS_MPROTECT,
                (uint64_t)(uintptr_t)&memory_request);
            if (is_failed(result)) {
                log_result("lazy_mprotect", 0);
                ++failures;
            } else {
                memory_request.prot_or_advice = PROT_READ | PROT_WRITE;
                result = vex_syscall1(VEX_SYS_MPROTECT,
                    (uint64_t)(uintptr_t)&memory_request);
                log_result("lazy_mprotect", !is_failed(result));
                failures += is_failed(result);
            }
            memory_request.length = sparse_size;
            result = vex_syscall1(VEX_SYS_MUNMAP,
                (uint64_t)(uintptr_t)&memory_request);
            log_result("lazy_munmap", !is_failed(result));
            failures += is_failed(result);
            log_result("lazy_128g_reservation", failures == 0);
        }
    }

    /* MAP_SHARED uses the same lazy reservation contract, with a sparse
     * refcounted backing.  Touch both ends without allocating the gap. */
    {
        volatile uint8_t *sparse_shared;
        const size_t sparse_size = (size_t)128u * 1024u * 1024u * 1024u;
        mmap_request.addr = 0;
        mmap_request.length = sparse_size;
        mmap_request.prot = PROT_READ | PROT_WRITE;
        mmap_request.flags = MAP_SHARED | MAP_ANONYMOUS;
        mmap_request.fd = -1;
        mmap_request.offset = 0;
        result = vex_syscall1(VEX_SYS_MMAP, (uint64_t)(uintptr_t)&mmap_request);
        if (is_failed(result)) {
            log_result("lazy_shared_128g_reservation", 0);
            ++failures;
        } else {
            sparse_shared = (volatile uint8_t *)(uintptr_t)result;
            sparse_shared[0] = 0x11;
            sparse_shared[sparse_size - PAGE_SIZE] = 0xee;
            memory_request.addr = (void *)(uintptr_t)result;
            memory_request.length = sparse_size;
            memory_request.prot_or_advice = PROT_NONE;
            result = vex_syscall1(VEX_SYS_MPROTECT,
                (uint64_t)(uintptr_t)&memory_request);
            if (is_failed(result)) ++failures;
            memory_request.prot_or_advice = PROT_READ | PROT_WRITE;
            result = vex_syscall1(VEX_SYS_MPROTECT,
                (uint64_t)(uintptr_t)&memory_request);
            if (is_failed(result)) ++failures;
            result = vex_syscall1(VEX_SYS_MUNMAP,
                (uint64_t)(uintptr_t)&memory_request);
            log_result("lazy_shared_128g_reservation", !is_failed(result));
            failures += is_failed(result);
        }
    }

    /* Two mappings of one native SHM object must observe the same physical
     * pages and keep the backing alive until both mappings are gone. */
    {
        vex_shm_create_req_t create = { 0 };
        vex_shm_map_req_t map_one = { 0 };
        vex_shm_map_req_t map_two = { 0 };
        vex_memory_req_t unmap = { 0 };
        uint8_t *one;
        uint8_t *two;

        create.name = "security_boundary_shm";
        create.size = PAGE_SIZE;
        create.prot = PROT_READ | PROT_WRITE;
        result = vex_syscall1(VEX_SYS_SHM_CREATE,
            (uint64_t)(uintptr_t)&create);
        if (is_failed(result) || create.handle == 0u) {
            log_result("shm_create", 0);
            ++failures;
        } else {
            /* A SHM token is a process-owned capability, not a backing id or
             * a globally accepted integer.  An altered token must fail both
             * lookup and close without affecting the live object. */
            {
                vex_shm_map_req_t invalid_map = { 0 };
                uint64_t invalid_handle = create.handle ^ UINT64_C(0x9e3779b97f4a7c15);
                if (invalid_handle == 0u) invalid_handle = 1u;
                invalid_map.handle = invalid_handle;
                invalid_map.length = PAGE_SIZE;
                invalid_map.prot = PROT_READ;
                result = vex_syscall1(VEX_SYS_SHM_MAP,
                    (uint64_t)(uintptr_t)&invalid_map);
                log_result("shm_invalid_capability_map", is_failed(result));
                failures += !is_failed(result);
                result = vex_syscall1(VEX_SYS_SHM_CLOSE, invalid_handle);
                log_result("shm_invalid_capability_close", is_failed(result));
                failures += !is_failed(result);
            }
            map_one.handle = create.handle;
            map_one.length = PAGE_SIZE;
            map_one.prot = PROT_READ | PROT_WRITE;
            map_two = map_one;
            result = vex_syscall1(VEX_SYS_SHM_MAP,
                (uint64_t)(uintptr_t)&map_one);
            result = is_failed(result) ? (uint64_t)-1 :
                vex_syscall1(VEX_SYS_SHM_MAP,
                    (uint64_t)(uintptr_t)&map_two);
            if (is_failed(result) || map_one.mapped_addr == NULL ||
                map_two.mapped_addr == NULL) {
                log_result("shm_map", 0);
                ++failures;
            } else {
                one = (uint8_t *)map_one.mapped_addr;
                two = (uint8_t *)map_two.mapped_addr;
                one[0] = 0x7c;
                log_result("shm_coherent", two[0] == 0x7c);
                failures += two[0] != 0x7c;
                unmap.addr = map_one.mapped_addr;
                unmap.length = PAGE_SIZE;
                (void)vex_syscall1(VEX_SYS_MUNMAP,
                    (uint64_t)(uintptr_t)&unmap);
                unmap.addr = map_two.mapped_addr;
                (void)vex_syscall1(VEX_SYS_MUNMAP,
                    (uint64_t)(uintptr_t)&unmap);
            }
            (void)vex_syscall1(VEX_SYS_SHM_CLOSE, create.handle);
        }
    }

    {
        vex_jit_arena_create_req_t request = { 0 };
        vex_jit_arena_create_req_t large_request = { 0 };
        vex_jit_arena_destroy_req_t destroy = { 0 };
        vex_memory_req_t protect = { 0 };
        uint8_t *rw;
        int (*rx_function)(void);

        /* A large JIT reservation is address-space metadata only.  The
         * kernel must not eagerly consume one page per reserved byte. */
        large_request.size = 1ull << 30;
        result = vex_syscall1(VEX_SYS_JIT_ARENA_CREATE,
            (uint64_t)(uintptr_t)&large_request);
        log_result("jit_lazy_1g_reservation", result == 0u);
        failures += result != 0u;
        if (result == 0u) {
            vex_jit_arena_destroy_req_t large_destroy = { 0 };
            large_destroy.addr = large_request.rw_base;
            large_destroy.arena_id = large_request.arena_id;
            result = vex_syscall1(VEX_SYS_JIT_ARENA_DESTROY,
                (uint64_t)(uintptr_t)&large_destroy);
            log_result("jit_lazy_1g_cleanup", result == 0u);
            failures += result != 0u;
        }

        request.size = PAGE_SIZE;
        result = vex_syscall1(VEX_SYS_JIT_ARENA_CREATE,
            (uint64_t)(uintptr_t)&request);
        if (result == 0u) {
            vex_gpu_info_t *cross_page_result =
                (vex_gpu_info_t *)((uint8_t *)request.rx_base + PAGE_SIZE - 1u);

            result = vex_syscall1(VEX_SYS_GPU_QUERY_INFO,
                (uint64_t)(uintptr_t)cross_page_result);
            log_result("buffer_crosses_unmapped_page", is_failed(result));
            failures += !is_failed(result);

            result = vex_syscall1(VEX_SYS_GPU_QUERY_INFO,
                (uint64_t)(uintptr_t)request.rx_base);
            log_result("write_readonly_page", is_failed(result));
            failures += !is_failed(result);

            protect.addr = request.rw_base;
            protect.length = PAGE_SIZE;
            protect.prot_or_advice = PROT_NONE;
            result = vex_syscall1(VEX_SYS_MPROTECT,
                (uint64_t)(uintptr_t)&protect);
            if (is_failed(result)) {
                log_result("prot_none_inaccessible", 0);
                ++failures;
            } else {
                result = vex_syscall1(VEX_SYS_GPU_QUERY_INFO,
                    (uint64_t)(uintptr_t)request.rw_base);
                log_result("prot_none_inaccessible", is_failed(result));
                failures += !is_failed(result);
            }

            protect.prot_or_advice = PROT_READ | PROT_WRITE;
            result = vex_syscall1(VEX_SYS_MPROTECT,
                (uint64_t)(uintptr_t)&protect);
            if (is_failed(result)) {
                log_result("jit_restore_rw", 0);
                failures++;
            }

            rw = (uint8_t *)request.rw_base;
            fill_return_code(rw);
            rx_function = (int (*)(void))request.rx_base;
            log_result("jit_execute_rx", rx_function() == 42);
            failures += rx_function() != 42;

            protect.addr = request.rx_base;
            protect.length = PAGE_SIZE;
            protect.prot_or_advice = PROT_READ | PROT_WRITE;
            result = vex_syscall1(VEX_SYS_MPROTECT,
                (uint64_t)(uintptr_t)&protect);
            log_result("jit_write_rx_denied", is_failed(result));
            failures += !is_failed(result);

            protect.addr = request.rw_base;
            protect.prot_or_advice = PROT_READ | PROT_EXEC;
            result = vex_syscall1(VEX_SYS_MPROTECT,
                (uint64_t)(uintptr_t)&protect);
            log_result("jit_execute_rw_denied", is_failed(result));
            failures += !is_failed(result);

            protect.addr = request.rw_base;
            protect.prot_or_advice = PROT_READ | PROT_WRITE | PROT_EXEC;
            result = vex_syscall1(VEX_SYS_MPROTECT,
                (uint64_t)(uintptr_t)&protect);
            log_result("jit_wx_denied", is_failed(result));
            failures += !is_failed(result);

            result = vex_syscall3(VEX_SYS_DEBUG_LOG_READ,
                (uint64_t)(uintptr_t)request.rx_base, 1u,
                (uint64_t)(uintptr_t)&sequence);
            log_result("jit_kernel_write_rx_denied", is_failed(result));
            failures += !is_failed(result);

            destroy.addr = request.rw_base;
            destroy.arena_id = request.arena_id;
            result = vex_syscall1(VEX_SYS_JIT_ARENA_DESTROY,
                (uint64_t)(uintptr_t)&destroy);
            log_result("jit_destroy", result == 0u);
            failures += result != 0u;
        } else {
            log_result("jit_execute_rx", 0);
            log_result("jit_write_rx_denied", 0);
            log_result("jit_execute_rw_denied", 0);
            log_result("jit_wx_denied", 0);
            log_result("jit_kernel_write_rx_denied", 0);
            log_result("jit_destroy", 0);
            failures += 5;
        }
    }

    result = syscall_with_invalid_rsp(VEX_SYS_GETPID);
    log_result("syscall_invalid_rsp", result != (uint64_t)-1 && result != 0u);
    failures += result == (uint64_t)-1 || result == 0u;
    result = syscall_with_invalid_rsp(VEX_SYSCALL_NATIVE_TAG | VEX_SYS_GETPID);
    log_result("native_syscall_invalid_rsp", result != (uint64_t)-1 && result != 0u);
    failures += result == (uint64_t)-1 || result == 0u;

    log_result("overall", failures == 0);
    return failures == 0 ? 0 : 1;
}
