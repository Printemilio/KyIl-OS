#include <unos/scheduler.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/boot_requests.h>
#include <unos/gdt.h>
#include <unos/gpu.h>
#include <unos/heap.h>
#include <unos/compat.h>
#include <unos/mrac.h>
#include <unos/pmm.h>
#include <unos/process_memory.h>
#include <unos/vex_syscall.h>
#include <unos/vmm.h>

#define PROCESS_STACK_SIZE 8192ull
#define WATCHDOG_LIMIT 100000ull
#define PAGE_ADDR_MASK 0x000ffffffffff000ull
#define USER_ALLOC_BASE 0x0000000100000000ull
#define MSR_FS_BASE 0xc0000100u

/* Size in bytes of the register frame pushed by syscall_entry.S (14 pushes of
   8 bytes each + a 24-byte scratch area). resume_syscall_return() pops this
   frame and lands the user rsp at frame_rsp + SYSCALL_FRAME_SIZE. A cloned
   thread therefore places a synthetic copy of this frame at the top of its new
   stack so that, after the sysretq, the user rsp equals the requested stack. */
#define SYSCALL_FRAME_SIZE 136ull

/* Futex wait queue. The nodes live on the waiting thread's own (kernel) stack
   because the syscall runs on that stack and is preserved across the blocking
   context switch, mirroring how process_wait_pid() blocks in place. A futex is
   uniquely identified by (cr3, uaddr): CLONE_VM threads share the same cr3, so
   the userspace address alone disambiguates the futex within a process. */
typedef struct futex_waiter {
    uint64_t cr3;
    uint64_t uaddr;
    uint32_t pid;
    volatile uint8_t woken;
    struct futex_waiter *next;
} futex_waiter_t;

static futex_waiter_t *futex_waiters;

extern void scheduler_context_switch(uint64_t *old_rsp, uint64_t new_rsp, uint64_t new_cr3);
extern void resume_syscall_return(uint64_t frame_rsp, uint64_t return_value);
extern void user_enter(uint64_t entry, uint64_t user_rsp);

static process_t kernel_process;
static process_t *process_list;
static process_t *current_process;
static uint32_t next_pid;
static uint64_t watchdog_cycles;
static volatile uint64_t scheduler_ticks;

#define PROCESS_WAIT_ANY (-1)
#define PROCESS_WAIT_NOHANG 0x1u

static process_t *find_process(uint32_t pid);

static uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PAGE_ADDR_MASK;
}

static void write_msr(uint32_t msr, uint64_t value) {
    const uint32_t low = (uint32_t)value;
    const uint32_t high = (uint32_t)(value >> 32u);

    __asm__ volatile ("wrmsr" :: "c"(msr), "a"(low), "d"(high) : "memory");
}

static void scheduler_apply_current_tls_state(void) {
    if (current_process == NULL) {
        return;
    }

    write_msr(MSR_FS_BASE, current_process->fs_base);
}

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static uint64_t process_kernel_stack_top(const process_t *process) {
    return process == NULL ? 0u : (uint64_t)process->stack + PROCESS_STACK_SIZE;
}

static void process_trampoline(void (*entry)(void)) {
    if (current_process != NULL && current_process->user_mode != 0) {
        scheduler_apply_current_tls_state();
        user_enter((uint64_t)entry, current_process->user_rsp);
    }

    entry();
    process_exit();
    halt_forever();
}

static void fork_resume_trampoline(void) {
    if (current_process == NULL || current_process->resume_syscall_rsp == 0u) {
        process_exit_status(1);
    }

    current_process->user_mode = 1u;
    gdt_set_tss_rsp0(process_kernel_stack_top(current_process));
    scheduler_apply_current_tls_state();
    resume_syscall_return(current_process->resume_syscall_rsp, 0u);
    halt_forever();
}

static void exec_resume_trampoline(void) {
    if (current_process == NULL || current_process->pending_user_entry == 0u || current_process->user_rsp == 0u) {
        process_exit_status(1);
    }

    if (current_process->pending_old_cr3 != 0u) {
        vmm_destroy_address_space(vmm_cr3_to_address_space(current_process->pending_old_cr3));
        current_process->pending_old_cr3 = 0u;
    }

    current_process->user_mode = 1u;
    gdt_set_tss_rsp0(process_kernel_stack_top(current_process));
    scheduler_apply_current_tls_state();
    user_enter(current_process->pending_user_entry, current_process->user_rsp);
    process_exit_status(1);
}

static void append_process(process_t *process) {
    process->next = NULL;

    if (process_list == NULL) {
        process_list = process;
        return;
    }

    process_t *tail = process_list;
    while (tail->next != NULL) {
        tail = tail->next;
    }

    tail->next = process;
}

static void destroy_process(process_t *process) {
    const uint64_t page_count = PROCESS_STACK_SIZE / 4096ull;

    if (process == NULL || process->pid == 0u) {
        return;
    }

    /* A cloned thread shares its cr3 with the rest of the process group; only
       the address-space owner (the original process) is allowed to tear the
       page tables down, otherwise a thread exit would unmap memory still in
       use by its siblings. */
    if (process->user_mode != 0 && process->cr3 != 0u && process->shares_address_space == 0u) {
        vex_jit_reap_pid(process->pid, process->cr3);
        vmm_destroy_address_space(vmm_cr3_to_address_space(process->cr3));
    }

    gpu_reap_pid(process->pid);
    MRAC_reap_pid(process->pid);

    if (process->stack != NULL && hhdm_request.response != NULL) {
        pmm_free_contiguous_pages(
            (void *)((uint64_t)process->stack - hhdm_request.response->offset),
            page_count
        );
    }

    kfree(process);
}

static int parent_alive(uint32_t pid) {
    return pid != 0u && find_process(pid) != NULL;
}

static void reap_zombies(process_t *skip) {
    process_t *previous = NULL;
    process_t *process = process_list;

    while (process != NULL) {
        process_t *next = process->next;

        if (process != skip && process->state == PROC_ZOMBIE &&
            (process->wait_collected != 0u || !parent_alive(process->parent_pid))) {
            if (previous == NULL) {
                process_list = next;
            } else {
                previous->next = next;
            }

            destroy_process(process);
            process = next;
            continue;
        }

        previous = process;
        process = next;
    }
}

static uint64_t hhdm_stack_top(uint64_t physical_stack_base) {
    return hhdm_request.response->offset + physical_stack_base + PROCESS_STACK_SIZE;
}

static uint64_t initial_process_rsp(uint64_t stack_top, void (*entry)(void)) {
    uint64_t *sp = (uint64_t *)(stack_top - sizeof(uint64_t));

    /* The assembly switch restores r15..rax, then ret. This frame makes the
       first switch enter process_trampoline(entry) with a clean register set. */
    *--sp = (uint64_t)process_trampoline;
    *--sp = 0;                /* rax */
    *--sp = 0;                /* rbx */
    *--sp = 0;                /* rcx */
    *--sp = 0;                /* rdx */
    *--sp = 0;                /* rbp */
    *--sp = 0;                /* rsi */
    *--sp = (uint64_t)entry;  /* rdi */
    *--sp = 0;                /* r8 */
    *--sp = 0;                /* r9 */
    *--sp = 0;                /* r10 */
    *--sp = 0;                /* r11 */
    *--sp = 0;                /* r12 */
    *--sp = 0;                /* r13 */
    *--sp = 0;                /* r14 */
    *--sp = 0;                /* r15 */

    return (uint64_t)sp;
}

static process_t *find_process(uint32_t pid) {
    for (process_t *process = process_list; process != NULL; process = process->next) {
        if (process->pid == pid) {
            return process;
        }
    }

    return NULL;
}

static process_t *better_ready(process_t *selected, process_t *candidate) {
    if (candidate == NULL || candidate->state != PROC_READY) {
        return selected;
    }

    if (selected == NULL || candidate->priority > selected->priority) {
        return candidate;
    }

    return selected;
}

static process_t *select_next_ready(void) {
    process_t *selected = NULL;

    if (current_process != NULL) {
        for (process_t *process = current_process->next; process != NULL; process = process->next) {
            selected = better_ready(selected, process);
        }
    }

    for (process_t *process = process_list; process != NULL && process != current_process; process = process->next) {
        selected = better_ready(selected, process);
    }

    if (selected == NULL && current_process != NULL && current_process->state == PROC_READY) {
        selected = current_process;
    } else if (current_process != NULL && current_process->state == PROC_READY && selected != NULL &&
               current_process->priority > selected->priority) {
        selected = current_process;
    }

    if (current_process == NULL) {
        for (process_t *process = process_list; process != NULL; process = process->next) {
            selected = better_ready(selected, process);
        }
    }

    return selected;
}

uint32_t scheduler_current_pid(void) {
    return current_process == NULL ? 0 : current_process->pid;
}

uint32_t scheduler_current_ppid(void) {
    return current_process == NULL ? 0u : current_process->parent_pid;
}

uint8_t scheduler_process_accepts_input(uint32_t pid) {
    process_t *process = find_process(pid);

    if (process == NULL) {
        return 0;
    }

    if (process->state == PROC_WAITING || process->state == PROC_SLEEPING || process->state == PROC_ZOMBIE) {
        return 0;
    }

    for (process_t *candidate = process_list; candidate != NULL; candidate = candidate->next) {
        if (candidate->parent_pid == pid &&
            candidate->user_mode != 0 &&
            candidate->state != PROC_ZOMBIE) {
            return 0;
        }
    }

    return 1;
}

void scheduler_init(void) {
    kernel_process.pid = 0;
    kernel_process.stack = NULL;
    kernel_process.rsp = 0;
    kernel_process.cr3 = read_cr3();
    kernel_process.state = PROC_RUNNING;
    kernel_process.priority = 0;
    kernel_process.uid = 0;
    kernel_process.is_admin = 1;
    kernel_process.user_mode = 0;
    kernel_process.abi = VEX_ABI_NATIVE;
    kernel_process.user_rsp = 0;
    kernel_process.user_alloc_base = 0;
    kernel_process.user_alloc_next = 0;
    kernel_process.sleep_until_tick = 0;
    kernel_process.parent_pid = 0;
    kernel_process.exit_status = 0;
    kernel_process.wait_collected = 0;
    kernel_process.syscall_frame_rsp = 0;
    kernel_process.resume_syscall_rsp = 0;
    kernel_process.pending_user_entry = 0;
    kernel_process.pending_old_cr3 = 0;
    kernel_process.fs_base = 0;
    kernel_process.shares_address_space = 0;
    kernel_process.clear_child_tid = 0;
    kernel_process.next = NULL;

    process_list = &kernel_process;
    current_process = &kernel_process;
    next_pid = 1;
    watchdog_cycles = 0;
    scheduler_ticks = 0;
}

static uint32_t create_process(void (*entry)(void), uint8_t priority, uint64_t cr3, uint8_t user_mode, uint64_t user_rsp, uint8_t abi) {
    if (entry == NULL || hhdm_request.response == NULL || current_process == NULL) {
        return 0;
    }

    process_t *process = kmalloc(sizeof(process_t));
    if (process == NULL) {
        return 0;
    }

    void *physical_stack = pmm_alloc_contiguous_pages(PROCESS_STACK_SIZE / 4096ull);
    if (physical_stack == NULL) {
        kfree(process);
        return 0;
    }

    const uint64_t stack_base = (uint64_t)physical_stack;
    const uint64_t stack_top = hhdm_stack_top(stack_base);

    process->pid = next_pid++;
    process->stack = (uint64_t *)(stack_top - PROCESS_STACK_SIZE);
    process->rsp = initial_process_rsp(stack_top, entry);
    process->cr3 = cr3 == 0 ? read_cr3() : cr3;
    process->state = PROC_READY;
    process->priority = priority;
    process->uid = current_process->uid;
    process->is_admin = current_process->is_admin;
    process->user_mode = user_mode != 0;
    process->abi = user_mode != 0 ? abi : VEX_ABI_NATIVE;
    process->user_rsp = user_rsp;
    process->user_alloc_base = user_mode != 0 ? USER_ALLOC_BASE : 0;
    process->user_alloc_next = user_mode != 0 ? USER_ALLOC_BASE : 0;
    process->sleep_until_tick = 0;
    process->parent_pid = current_process->pid;
    process->exit_status = 0;
    process->wait_collected = 0;
    process->syscall_frame_rsp = 0;
    process->resume_syscall_rsp = 0;
    process->pending_user_entry = 0;
    process->pending_old_cr3 = 0;
    process->fs_base = 0u;
    process->shares_address_space = 0u;
    process->clear_child_tid = 0u;
    process->next = NULL;

    if (process->cr3 != 0u) {
        vmm_sync_kernel_mappings(vmm_cr3_to_address_space(process->cr3));
    }

    append_process(process);
    return process->pid;
}

uint32_t process_create_in_address_space(void (*entry)(void), uint8_t priority, uint64_t cr3) {
    return create_process(entry, priority, cr3, 0, 0, VEX_ABI_NATIVE);
}

uint32_t process_create_user(void (*entry)(void), uint8_t priority, uint64_t cr3, uint64_t user_rsp) {
    return process_create_user_abi(entry, priority, cr3, user_rsp, VEX_ABI_NATIVE);
}

uint32_t process_create_user_abi(void (*entry)(void), uint8_t priority, uint64_t cr3, uint64_t user_rsp, uint8_t abi) {
    if (user_rsp == 0) {
        return 0;
    }

    return create_process(entry, priority, cr3, 1, user_rsp, abi);
}

uint32_t process_create(void (*entry)(void), uint8_t priority) {
    return process_create_in_address_space(entry, priority, 0);
}

void scheduler_record_syscall_frame(uint64_t rsp) {
    if (current_process != NULL) {
        current_process->syscall_frame_rsp = rsp;
    }
}

uint32_t scheduler_current_uid(void) {
    return current_process == NULL ? 0 : current_process->uid;
}

uint8_t scheduler_current_is_admin(void) {
    return current_process != NULL && current_process->is_admin != 0;
}

uint8_t scheduler_current_user_mode(void) {
    return current_process != NULL && current_process->user_mode != 0;
}

uint8_t scheduler_current_abi(void) {
    return current_process == NULL ? VEX_ABI_NATIVE : current_process->abi;
}

void scheduler_set_process_abi(uint32_t pid, uint8_t abi) {
    process_t *process = find_process(pid);
    if (process == NULL) {
        return;
    }
    process->abi = abi;
}

void scheduler_set_process_user_alloc_next(uint32_t pid, uint64_t user_alloc_next) {
    process_t *process = find_process(pid);

    if (process == NULL) {
        return;
    }

    process->user_alloc_next = user_alloc_next < process->user_alloc_base
        ? process->user_alloc_base
        : user_alloc_next;
}

void scheduler_set_current_credentials(uint32_t uid, uint8_t is_admin) {
    if (current_process == NULL) {
        return;
    }

    current_process->uid = uid;
    current_process->is_admin = is_admin != 0;
}

process_t *scheduler_current_process(void) {
    return current_process;
}

void scheduler_map_kernel_page_all(uint64_t virt, uint64_t phys, uint64_t flags) {
    for (process_t *process = process_list; process != NULL; process = process->next) {
        void *pml4;

        if (process->cr3 == 0u) {
            continue;
        }

        pml4 = vmm_cr3_to_address_space(process->cr3);
        if (pml4 == NULL) {
            continue;
        }

        vmm_map_page(pml4, virt, phys, flags);
    }
}

void scheduler_unmap_kernel_page_all(uint64_t virt) {
    for (process_t *process = process_list; process != NULL; process = process->next) {
        void *pml4;

        if (process->cr3 == 0u) {
            continue;
        }

        pml4 = vmm_cr3_to_address_space(process->cr3);
        if (pml4 == NULL) {
            continue;
        }

        vmm_unmap_page(pml4, virt);
    }
}

uint32_t process_fork_current(void) {
    process_t *child;
    uint32_t child_pid;
    uint64_t child_cr3;
    void *child_pml4;

    if (current_process == NULL || current_process->user_mode == 0 || current_process->syscall_frame_rsp == 0u) {
        return 0u;
    }

    child_pml4 = vmm_new_address_space();
    if (child_pml4 == NULL) {
        return 0u;
    }

    child_cr3 = vmm_address_space_to_cr3(child_pml4);
    if (child_cr3 == 0u) {
        vmm_destroy_address_space(child_pml4);
        return 0u;
    }

    child_pid = process_create_in_address_space(fork_resume_trampoline, current_process->priority, child_cr3);
    if (child_pid == 0u) {
        vmm_destroy_address_space(child_pml4);
        return 0u;
    }

    child = find_process(child_pid);
    if (child == NULL) {
        vmm_destroy_address_space(child_pml4);
        return 0u;
    }

    if (process_memory_clone_namespace(current_process->pid, current_process->cr3, child_pid, child_cr3) != 0) {
        process_kill(child_pid);
        return 0u;
    }

    child->abi = current_process->abi;
    child->user_rsp = current_process->user_rsp;
    child->user_alloc_base = current_process->user_alloc_base;
    child->user_alloc_next = current_process->user_alloc_next;
    child->resume_syscall_rsp = current_process->syscall_frame_rsp;
    child->syscall_frame_rsp = 0u;
    child->user_mode = 0u;
    child->fs_base = current_process->fs_base;
    return child_pid;
}

/* CLONE flags relevant to thread creation (see linux/sched.h). */
#define CLONE_VM_FLAG            0x00000100ull
#define CLONE_SETTLS_FLAG        0x00080000ull
#define CLONE_PARENT_SETTID_FLAG 0x00100000ull
#define CLONE_CHILD_CLEARTID_FLAG 0x00200000ull
#define CLONE_CHILD_SETTID_FLAG  0x01000000ull

uint32_t process_clone_thread(uint64_t child_stack, uint64_t flags, uint64_t parent_tid, uint64_t child_tid, uint64_t tls) {
    process_t *child;
    uint32_t child_pid;
    uint64_t child_frame;

    if (current_process == NULL || current_process->user_mode == 0 ||
        current_process->syscall_frame_rsp == 0u || child_stack == 0u) {
        return 0u;
    }

    /* Place a synthetic syscall frame at the top of the new thread stack. After
       resume_syscall_return() pops it the user rsp will equal child_stack. */
    child_frame = child_stack - SYSCALL_FRAME_SIZE;

    /* Copy the parent's recorded frame so the child resumes at the same user RIP
       and rflags with identical registers (rax is forced to 0 on resume). The
       new thread shares the parent's cr3, so child_frame is already mapped. */
    {
        const volatile uint64_t *src = (const volatile uint64_t *)current_process->syscall_frame_rsp;
        volatile uint64_t *dst = (volatile uint64_t *)child_frame;
        for (uint64_t i = 0u; i < SYSCALL_FRAME_SIZE / sizeof(uint64_t); ++i) {
            dst[i] = src[i];
        }
    }

    child_pid = process_create_in_address_space(fork_resume_trampoline,
                                                current_process->priority,
                                                current_process->cr3);
    if (child_pid == 0u) {
        return 0u;
    }

    child = find_process(child_pid);
    if (child == NULL) {
        return 0u;
    }

    child->abi = current_process->abi;
    child->user_mode = 0u; /* fork_resume_trampoline flips this to 1 */
    child->user_rsp = child_stack;
    child->user_alloc_base = current_process->user_alloc_base;
    child->user_alloc_next = current_process->user_alloc_next;
    child->resume_syscall_rsp = child_frame;
    child->syscall_frame_rsp = 0u;
    child->shares_address_space = 1u;
    child->fs_base = (flags & CLONE_SETTLS_FLAG) != 0u ? tls : current_process->fs_base;

    if ((flags & CLONE_CHILD_CLEARTID_FLAG) != 0u && child_tid != 0u) {
        child->clear_child_tid = child_tid;
    }
    if ((flags & CLONE_CHILD_SETTID_FLAG) != 0u && child_tid != 0u) {
        *(volatile uint32_t *)child_tid = child_pid;
    }
    if ((flags & CLONE_PARENT_SETTID_FLAG) != 0u && parent_tid != 0u) {
        *(volatile uint32_t *)parent_tid = child_pid;
    }

    return child_pid;
}

void process_set_clear_child_tid(uint64_t addr) {
    if (current_process != NULL) {
        current_process->clear_child_tid = addr;
    }
}

void process_exit_group(int status) {
    /* Terminate every task sharing this address space (all threads of the
       process plus the original owner), then exit the caller. */
    if (current_process != NULL) {
        const uint64_t cr3 = current_process->cr3;

        for (process_t *process = process_list; process != NULL; process = process->next) {
            if (process == current_process || process->pid == 0u || process->cr3 != cr3) {
                continue;
            }
            if (process->state == PROC_ZOMBIE) {
                continue;
            }

            if (process->parent_pid != 0u) {
                process_wake(process->parent_pid);
            }
            process->sleep_until_tick = 0u;
            process->exit_status = status;
            process->wait_collected = 1u;
            process->state = PROC_ZOMBIE;
        }
    }

    process_exit_status(status);
}

static void futex_unlink(futex_waiter_t *node) {
    futex_waiter_t **link = &futex_waiters;

    while (*link != NULL) {
        if (*link == node) {
            *link = node->next;
            return;
        }
        link = &(*link)->next;
    }
}

int futex_wait(uint64_t uaddr, uint32_t expected) {
    futex_waiter_t node;

    if (current_process == NULL || uaddr == 0u) {
        return -1;
    }

    /* FUTEX_WAIT only sleeps if the word still holds the expected value. */
    if (*(volatile uint32_t *)uaddr != expected) {
        return -1;
    }

    node.cr3 = current_process->cr3;
    node.uaddr = uaddr;
    node.pid = current_process->pid;
    node.woken = 0u;
    node.next = futex_waiters;
    futex_waiters = &node;

    while (node.woken == 0u) {
        process_wait();
    }

    futex_unlink(&node);
    return 0;
}

int futex_wake(uint64_t uaddr, uint32_t count) {
    const uint64_t cr3 = current_process == NULL ? 0u : current_process->cr3;
    int woken = 0;

    if (uaddr == 0u) {
        return 0;
    }

    for (futex_waiter_t *waiter = futex_waiters; waiter != NULL; waiter = waiter->next) {
        if ((uint32_t)woken >= count) {
            break;
        }
        if (waiter->woken == 0u && waiter->cr3 == cr3 && waiter->uaddr == uaddr) {
            waiter->woken = 1u;
            process_wake(waiter->pid);
            ++woken;
        }
    }

    return woken;
}

int process_exec_image(uint64_t new_cr3, uint64_t user_entry, uint64_t user_rsp, uint8_t abi, uint64_t user_alloc_next) {
    uint64_t abandoned_rsp = 0u;
    uint64_t bootstrap_rsp;

    if (current_process == NULL || new_cr3 == 0u || user_entry == 0u || user_rsp == 0u) {
        return -1;
    }

    bootstrap_rsp = initial_process_rsp(process_kernel_stack_top(current_process), exec_resume_trampoline);
    current_process->pending_old_cr3 = current_process->cr3;
    current_process->pending_user_entry = user_entry;
    current_process->cr3 = new_cr3;
    current_process->abi = abi;
    current_process->user_rsp = user_rsp;
    current_process->user_alloc_base = USER_ALLOC_BASE;
    current_process->user_alloc_next = user_alloc_next < USER_ALLOC_BASE ? USER_ALLOC_BASE : user_alloc_next;
    current_process->resume_syscall_rsp = 0u;
    current_process->syscall_frame_rsp = 0u;
    current_process->rsp = bootstrap_rsp;
    current_process->user_mode = 0u;
    current_process->state = PROC_RUNNING;

    scheduler_context_switch(&abandoned_rsp, current_process->rsp, current_process->cr3);
    halt_forever();
    return -1;
}

void scheduler_set_current_fs_base(uint64_t fs_base) {
    if (current_process == NULL) {
        return;
    }

    current_process->fs_base = fs_base;
    scheduler_apply_current_tls_state();
}

uint64_t scheduler_current_fs_base(void) {
    return current_process == NULL ? 0u : current_process->fs_base;
}

const char *scheduler_state_name(uint32_t pid) {
    process_t *process = find_process(pid);
    if (process == NULL) {
        return "missing";
    }
    return scheduler_state_name_value((uint8_t)process->state);
}

const char *scheduler_state_name_value(uint8_t state) {
    switch (state) {
    case PROC_RUNNING:
        return "running";
    case PROC_READY:
        return "ready";
    case PROC_WAITING:
        return "waiting";
    case PROC_SLEEPING:
        return "sleeping";
    case PROC_ZOMBIE:
        return "zombie";
    default:
        return "unknown";
    }
}

void scheduler_set_process_priority(uint32_t pid, uint8_t priority) {
    process_t *process = find_process(pid);
    if (process == NULL) {
        return;
    }
    process->priority = priority;
}

void process_yield(void) {
    if (current_process != NULL && current_process->state == PROC_RUNNING) {
        current_process->state = PROC_READY;
    }

    watchdog_cycles = 0;
    schedule();
}

void process_yield_to(uint32_t pid) {
    process_t *next = find_process(pid);
    if (current_process == NULL || next == NULL || next == current_process ||
        next->state == PROC_ZOMBIE || next->state == PROC_WAITING || next->state == PROC_SLEEPING) {
        process_yield();
        return;
    }

    process_t *previous = current_process;
    if (previous->state == PROC_RUNNING) {
        previous->state = previous->pid == 0 ? PROC_WAITING : PROC_READY;
    }

    next->state = PROC_RUNNING;
    current_process = next;
    watchdog_cycles = 0;
    if (next->user_mode != 0)
        gdt_set_tss_rsp0((uint64_t)next->stack + PROCESS_STACK_SIZE);
    scheduler_context_switch(&previous->rsp, next->rsp, next->cr3);
}

void process_sleep_ticks(uint64_t ticks) {
    if (current_process == NULL || current_process->pid == 0u) {
        process_yield();
        return;
    }

    if (ticks == 0u) {
        process_yield();
        return;
    }

    current_process->sleep_until_tick = scheduler_ticks + ticks;
    current_process->state = PROC_SLEEPING;
    watchdog_cycles = 0;
    schedule();
}

void process_wait(void) {
    if (current_process != NULL) {
        current_process->state = PROC_WAITING;
    }

    watchdog_cycles = 0;
    schedule();
}

void process_exit(void) {
    process_exit_status(0);
}

void process_exit_status(int status) {
    if (current_process != NULL) {
        /* CLONE_CHILD_CLEARTID / set_tid_address contract: on thread exit clear
           the user word and wake one futex waiter so pthread_join() returns.
           The address space is still active here, so the store is valid. */
        if (current_process->clear_child_tid != 0u) {
            *(volatile uint32_t *)current_process->clear_child_tid = 0u;
            futex_wake(current_process->clear_child_tid, 1u);
            current_process->clear_child_tid = 0u;
        }

        for (process_t *child = process_list; child != NULL; child = child->next) {
            if (child->parent_pid == current_process->pid) {
                child->parent_pid = 0u;
            }
        }

        if (current_process->parent_pid != 0) {
            process_wake(current_process->parent_pid);
        }
        current_process->exit_status = status;
        current_process->wait_collected = 0u;
        current_process->state = PROC_ZOMBIE;
    }

    watchdog_cycles = 0;
    schedule();

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void process_wake(uint32_t pid) {
    process_t *process = find_process(pid);
    if (process != NULL && (process->state == PROC_WAITING || process->state == PROC_SLEEPING)) {
        process->sleep_until_tick = 0u;
        process->state = PROC_READY;
    }
}

int process_kill(uint32_t pid) {
    process_t *process = find_process(pid);

    if (process == NULL || process->pid == 0u || process->state == PROC_ZOMBIE) {
        return -1;
    }

    if (process->parent_pid != 0u) {
        process_wake(process->parent_pid);
    }
    process->sleep_until_tick = 0u;
    process->exit_status = 128 + 9;
    process->wait_collected = 0u;
    process->state = PROC_ZOMBIE;

    if (process == current_process) {
        schedule();
    } else {
        reap_zombies(current_process);
    }
    return 0;
}

int process_wait_pid(int32_t pid, int *status, uint32_t flags, uint32_t *reaped_pid) {
    if (current_process == NULL) {
        return -1;
    }

    for (;;) {
        int has_matching_child = 0;

        for (process_t *process = process_list; process != NULL; process = process->next) {
            if (process->parent_pid != current_process->pid) {
                continue;
            }

            if (pid > 0 && process->pid != (uint32_t)pid) {
                continue;
            }

            has_matching_child = 1;
            if (process->state != PROC_ZOMBIE) {
                continue;
            }

            if (status != NULL) {
                *status = process->exit_status;
            }
            const uint32_t pid_value = process->pid;
            if (reaped_pid != NULL) {
                *reaped_pid = pid_value;
            }

            process->wait_collected = 1u;
            process->parent_pid = 0u;
            reap_zombies(current_process);
            return (int)pid_value;
        }

        if (!has_matching_child) {
            return -1;
        }
        if ((flags & PROCESS_WAIT_NOHANG) != 0u) {
            if (reaped_pid != NULL) {
                *reaped_pid = 0u;
            }
            return 0;
        }

        process_wait();
    }
}

uint64_t scheduler_ticks_now(void) {
    return scheduler_ticks;
}

uint64_t scheduler_process_count(void) {
    uint64_t count = 0u;
    for (process_t *process = process_list; process != NULL; process = process->next) {
        ++count;
    }
    return count;
}

int scheduler_process_info_at(uint64_t index, scheduler_process_info_t *out) {
    uint64_t current = 0u;

    if (out == NULL) {
        return -1;
    }

    for (process_t *process = process_list; process != NULL; process = process->next) {
        if (current == index) {
            out->pid = process->pid;
            out->parent_pid = process->parent_pid;
            out->state = (uint8_t)process->state;
            out->priority = process->priority;
            out->user_mode = process->user_mode;
            out->is_admin = process->is_admin;
            out->uid = process->uid;
            out->sleep_until_tick = process->sleep_until_tick;
            return 0;
        }
        ++current;
    }

    return -1;
}

void scheduler_tick(void) {
    ++scheduler_ticks;

    for (process_t *process = process_list; process != NULL; process = process->next) {
        if (process->state == PROC_SLEEPING && process->sleep_until_tick <= scheduler_ticks) {
            process->sleep_until_tick = 0u;
            process->state = PROC_READY;
        }
    }

    if (current_process == NULL) {
        return;
    }

    ++watchdog_cycles;
    if (watchdog_cycles >= WATCHDOG_LIMIT) {
        watchdog_cycles = 0;
    }
}

void schedule(void) {
    if (current_process == NULL) {
        return;
    }

    reap_zombies(current_process);

    process_t *next = select_next_ready();

    if (next == NULL && current_process != &kernel_process && current_process->state != PROC_RUNNING) {
        kernel_process.state = PROC_READY;
        next = &kernel_process;
    }

    /* There is no timer yet, so the watchdog can only act when the scheduler is
       entered. It prevents a repeatedly scheduled RUNNING task from monopolising
       the cooperative loop once the counter crosses the limit. */
    if (next == NULL && current_process->state == PROC_RUNNING) {
        ++watchdog_cycles;
        if (watchdog_cycles >= WATCHDOG_LIMIT) {
            current_process->state = PROC_READY;
            watchdog_cycles = 0;
            next = select_next_ready();
        }
    }

    if (next == NULL || next == current_process) {
        if (current_process->state == PROC_READY) {
            current_process->state = PROC_RUNNING;
        }
        return;
    }

    process_t *previous = current_process;
    if (previous->state == PROC_RUNNING) {
        previous->state = previous->pid == 0 ? PROC_WAITING : PROC_READY;
    }

    next->state = PROC_RUNNING;
    current_process = next;
    if (next->user_mode != 0)
        gdt_set_tss_rsp0((uint64_t)next->stack + PROCESS_STACK_SIZE);
    scheduler_context_switch(&previous->rsp, next->rsp, next->cr3);
}
