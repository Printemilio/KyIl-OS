#include <unos/events.h>
#include <unos/compat.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/debug_log.h>
#include <unos/irq.h>
#include <unos/scheduler.h>
#include <unos/vex_syscall.h>
#include <unos/vmm.h>
#include <unos/vex_legacy_compat.h>
#include <unos/process_memory.h>
static kernel_gate_t events_gate=KERNEL_GATE_INIT;
kernel_guard_t vex_event_state_begin(void){return kernel_guard_enter(&events_gate);}
void vex_event_state_end(kernel_guard_t *guard){kernel_guard_leave(guard);}
int vex_event_interrupt_pending(void) {
    process_t *p=scheduler_current_process();
    if(!p || p->event_active || !p->group)return 0;
    uint32_t ready=__atomic_load_n(&p->event_pending,__ATOMIC_ACQUIRE)&~p->event_mask;
    KERNEL_GUARD(events_gate);
    for(unsigned i=0;i<VEX_EVENT_SLOT_COUNT;++i)
        if(ready&(1u<<i)) {
            if(p->group->events[i].handler!=1u)return 1;
            __atomic_fetch_and(&p->event_pending,~(1u<<i),__ATOMIC_ACQ_REL);
        }
    return 0;
}

#define EVENT_FRAME_RDI_OFFSET 80u
#define EVENT_FRAME_RSI_OFFSET 88u
#define EVENT_FRAME_RDX_OFFSET 104u
#define EVENT_FRAME_RCX_OFFSET 128u
#define EVENT_FRAME_RSP_OFFSET 136u
#define EVENT_SA_SIGINFO VEX_EVENT_SIGINFO
#define EVENT_SIGINFO_SIZE 128u
#define EVENT_UCONTEXT_SIZE 256u

_Static_assert(sizeof(interrupt_frame_t) == SCHEDULER_INTERRUPT_FRAME_SIZE,
               "synchronous event context must cover the full CPU frame");

static process_group_t *event_group(process_t *process) {
    return process == NULL ? NULL : process->group;
}

static int event_on_alt_stack(const process_t *process, uint64_t rsp) {
    return process->event_alt_stack_size != 0u &&
        rsp > process->event_alt_stack_base &&
        rsp <= process->event_alt_stack_base + process->event_alt_stack_size;
}

/* The stack a handler frame is built on: the alternate stack when the
   registration asks for it, the thread has one and is not already on it;
   otherwise the interrupted stack. */
static uint64_t event_handler_stack(const process_t *process,
                                    const vex_event_registration_t *registration,
                                    uint64_t interrupted_rsp) {
    if ((registration->flags & VEX_EVENT_ON_ALT_STACK) != 0u &&
        process->event_alt_stack_size != 0u &&
        !event_on_alt_stack(process, interrupted_rsp))
        return process->event_alt_stack_base + process->event_alt_stack_size;
    return interrupted_rsp;
}

int vex_event_leave(void) {
    process_t *process = scheduler_current_process();
    if (process == NULL || process->user_mode == 0u) return -EINVAL;
    if (process->event_active == 0u) return 0;
    /* The interrupted context is abandoned with its saved FPU state; the
       caller keeps running with its current registers. */
    process->event_saved_fpu_valid = 0u;
    process->event_saved_event = 0u;
    process->event_context_kind = VEX_EVENT_CONTEXT_NONE;
    process->event_user_context_addr = 0u;
    process->event_active = 0u;
    return 0;
}

int vex_event_set_alt_stack(vex_event_alt_stack_req_t *req) {
    process_t *process = scheduler_current_process();
    uint64_t rsp;

    if (process == NULL || process->user_mode == 0u || req == NULL) return -EINVAL;
    rsp = process->syscall_frame_rsp != 0u
        ? ((const uint64_t *)(uintptr_t)process->syscall_frame_rsp)[EVENT_FRAME_RSP_OFFSET / sizeof(uint64_t)]
        : 0u;
    req->old_base = process->event_alt_stack_base;
    req->old_size = process->event_alt_stack_size;
    req->old_active = (uint32_t)event_on_alt_stack(process, rsp);
    if (req->set == 0u) return 0;
    if (req->old_active) return -EPERM;
    if (req->size != 0u) {
        if (req->size < VEX_EVENT_ALT_STACK_MIN) return -ENOMEM;
        if (req->base == 0u || req->base > UINT64_MAX - req->size ||
            !vex_syscall_user_range_valid(req->base, (size_t)req->size, 1)) return -EFAULT;
    }
    process->event_alt_stack_base = req->size != 0u ? req->base : 0u;
    process->event_alt_stack_size = req->size;
    return 0;
}

static int event_code_valid(process_t *process, uint64_t address) {
    void *pml4;
    uint64_t flags;

    if (process == NULL || address == 0u ||
        !vex_syscall_user_range_valid(address, 1u, 0)) {
        return 0;
    }
    pml4 = vmm_cr3_to_address_space(process->cr3);
    if (pml4 == NULL) {
        return 0;
    }
    flags = vmm_get_page_flags(pml4, address);
    return (flags & (VMM_PAGE_PRESENT | VMM_PAGE_USER | VMM_PAGE_NX)) ==
        (VMM_PAGE_PRESENT | VMM_PAGE_USER);
}

int vex_event_validate_user_code(struct process *opaque_process, uint64_t address) {
    return event_code_valid((process_t *)opaque_process, address);
}

int vex_event_register(uint16_t event_id, const vex_event_registration_t *registration) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    KERNEL_GUARD(events_gate);
    process_t *process = scheduler_current_process();
    process_group_t *group = event_group(process);

    if (process == NULL || process->user_mode == 0u || group == NULL ||
        registration == NULL || event_id >= VEX_EVENT_SLOT_COUNT) {
        return -1;
    }
    if (registration->handler > 1u && !event_code_valid(process, registration->handler)) {
        return -1;
    }
    if (registration->restorer != 0u && !event_code_valid(process, registration->restorer)) {
        return -1;
    }
    group->events[event_id] = *registration;
    return 0;
}

int vex_event_query(uint16_t event_id, vex_event_registration_t *registration) {
    KERNEL_GUARD(events_gate);
    process_t *process = scheduler_current_process();
    process_group_t *group = event_group(process);

    if (process == NULL || group == NULL || registration == NULL ||
        event_id >= VEX_EVENT_SLOT_COUNT) {
        return -1;
    }
    *registration = group->events[event_id];
    return 0;
}

int vex_event_set_mask(uint32_t how, uint64_t set, uint64_t *old_mask) {
    process_t *process = scheduler_current_process();
    const uint32_t supported = (1u << VEX_EVENT_SLOT_COUNT) - 1u;
    uint32_t mask;

    if (process == NULL || process->group == NULL || (set & ~((uint64_t)supported)) != 0u) {
        return -1;
    }
    if (old_mask != NULL) {
        *old_mask = process->event_mask;
    }
    mask = process->event_mask;
    switch (how) {
    case 0u: /* block */
        mask |= (uint32_t)set;
        break;
    case 1u: /* unblock */
        mask &= ~(uint32_t)set;
        break;
    case 2u: /* replace */
        mask = (uint32_t)set;
        break;
    default:
        return -1;
    }
    process->event_mask = mask;
    return 0;
}

int vex_event_post(uint32_t process_id, uint32_t thread_id, uint16_t event_id) {
    SCHEDULER_TASK_REF(target_ref);
    process_t *target;
    const uint32_t owner_id = scheduler_current_process_id();

    if (event_id >= VEX_EVENT_SLOT_COUNT || process_id == 0u) {
        return -1;
    }
    target = scheduler_task_acquire(&target_ref,thread_id?thread_id:process_id,!thread_id);
    if (target == NULL || target->user_mode == 0u || target->state == PROC_ZOMBIE ||
        (process_id != 0u && target->group != NULL && target->group->process_id != process_id) ||
        !scheduler_can_control_process(target->group == NULL ? target->pid : target->group->process_id)) {
        return -1;
    }
    __atomic_store_n(&target->event_sender_pid[event_id],owner_id,__ATOMIC_RELAXED);
    __atomic_fetch_or(&target->event_pending,1u<<event_id,__ATOMIC_RELEASE);

    if (target->state == PROC_WAITING || target->state == PROC_SLEEPING) {
        flow_context_t flow=scheduler_flow_current();
        if(flow.id)scheduler_wake_flow(target->pid,flow);
        else scheduler_wake_origin(target->pid,FLOW_EVENT,2000000ull);
    }
    (void)owner_id;
    return 0;
}

static void event_memory_end(kernel_guard_t *guard) {
    if(guard->gate){process_memory_end_update(guard);guard->gate=NULL;}
}
void vex_event_deliver_pending(void) {
    process_t *process = scheduler_current_process();
    process_group_t *group = event_group(process);
    uint32_t ready;
    uint16_t event_id;
    vex_event_registration_t *registration;
    uint64_t *frame;
    uint64_t user_rsp;
    uint64_t handler_rsp;
    uint64_t siginfo_addr = 0u;
    uint64_t ucontext_addr = 0u;
    uint32_t context_kind=VEX_EVENT_CONTEXT_ASYNC;
    size_t context_size=EVENT_UCONTEXT_SIZE;

    if (process == NULL || process->user_mode == 0u || group == NULL ||
        process->event_active != 0u || process->syscall_frame_rsp == 0u) {
        return;
    }
    ready = __atomic_load_n(&process->event_pending,__ATOMIC_ACQUIRE) & ~process->event_mask;
    if (ready == 0u) {
        return;
    }
    for (event_id = 0u; event_id < VEX_EVENT_SLOT_COUNT; ++event_id) {
        if ((ready & (1u << event_id)) != 0u) {
            break;
        }
    }
    if (event_id >= VEX_EVENT_SLOT_COUNT) {
        return;
    }
    vex_event_registration_t action;
    {KERNEL_GUARD(events_gate);action=group->events[event_id];}
    registration=&action;
    kernel_guard_t memory_update __attribute__((cleanup(event_memory_end)))=process_memory_begin_update();
    __atomic_fetch_and(&process->event_pending,~(1u<<event_id),__ATOMIC_ACQ_REL);

    if (registration->handler == 1u) { /* native ignore disposition */
        return;
    }
    if (registration->handler == 0u || registration->restorer == 0u) {
        /* A default event or an unreturnable handler cannot be represented by
         * a safe user frame.  Terminate the faulty process cleanly. */
        event_memory_end(&memory_update);process_exit_group(128 + (int)(registration->delivery_value != 0u
            ? registration->delivery_value : event_id));
        return;
    }

    frame = (uint64_t *)(uintptr_t)process->syscall_frame_rsp;
    user_rsp = event_handler_stack(process, registration,
                                   frame[EVENT_FRAME_RSP_OFFSET / sizeof(uint64_t)]);
    if (user_rsp < 128u + sizeof(uint64_t)) {
        event_memory_end(&memory_update);process_exit_group(128 + (int)(registration->delivery_value != 0u
            ? registration->delivery_value : event_id));
        return;
    }
    /* Preserve the interrupted SysV red zone and enter a C handler with
       RSP == 8 mod 16, independently of the caller's syscall instruction. */
    handler_rsp = ((user_rsp-128u)&~15ull)-sizeof(uint64_t);
    if (!vex_syscall_user_range_valid(handler_rsp, sizeof(uint64_t), 1) ||
        !event_code_valid(process, registration->handler) ||
        !event_code_valid(process, registration->restorer)) {
        event_memory_end(&memory_update);process_exit_group(128 + (int)(registration->delivery_value != 0u
            ? registration->delivery_value : event_id));
        return;
    }

    scheduler_capture_fpu_state(process->event_saved_fpu_state);
    process->event_saved_fpu_valid=1u;
    if(process->abi==VEX_ABI_LINUX && (registration->flags&EVENT_SA_SIGINFO)) {
        context_size=compat_linux_async_context_size();context_kind=VEX_EVENT_CONTEXT_ASYNC_COMPAT;
    }
    if ((registration->flags & EVENT_SA_SIGINFO) != 0u) {
        /* Linux sigaction handlers with SA_SIGINFO receive real pointers,
           even though the native event ABI only stores the handler.  Build a
           bounded Linux-compatible prefix in the interrupted user stack;
           rt_sigreturn still restores the complete saved Vex frame. */
        siginfo_addr = handler_rsp + sizeof(uint64_t) - EVENT_SIGINFO_SIZE;
        ucontext_addr = (siginfo_addr - context_size)&~15ull;
        if (siginfo_addr > handler_rsp || ucontext_addr > siginfo_addr ||
            !vex_syscall_user_range_valid(ucontext_addr,
                (size_t)(siginfo_addr-ucontext_addr)+EVENT_SIGINFO_SIZE, 1)) {
            event_memory_end(&memory_update);process_exit_group(128 + (int)(registration->delivery_value != 0u
                ? registration->delivery_value : event_id));
            return;
        }
        handler_rsp=ucontext_addr-sizeof(uint64_t);
        if(handler_rsp>ucontext_addr || !vex_syscall_user_range_valid(handler_rsp,sizeof(uint64_t),1)) {
            event_memory_end(&memory_update);process_exit_group(128+(int)registration->delivery_value);
            return;
        }
        memset((void *)(uintptr_t)ucontext_addr, 0,
               (size_t)(siginfo_addr-ucontext_addr)+EVENT_SIGINFO_SIZE);
        *(volatile int *)(uintptr_t)(siginfo_addr + 0u) =
            (int)registration->delivery_value;
        *(volatile int *)(uintptr_t)(siginfo_addr + 8u) = -6; /* SI_TKILL */
        *(volatile uint32_t *)(uintptr_t)(siginfo_addr + 16u) =
            process->event_sender_pid[event_id] != 0u ? process->event_sender_pid[event_id] :
            (group->process_id != 0u ? group->process_id : process->pid);
    }

    if(context_kind==VEX_EVENT_CONTEXT_ASYNC_COMPAT)
        compat_linux_build_async_context((void *)(uintptr_t)ucontext_addr,frame,process->event_saved_rax,registration->delivery_value);
    else memcpy(process->event_saved_frame,frame,SCHEDULER_SYSCALL_FRAME_SIZE);
    process->event_context_kind=context_kind;
    process->event_user_context_addr = ucontext_addr;
    *(volatile uint64_t *)(uintptr_t)handler_rsp = registration->restorer;
    frame[EVENT_FRAME_RDI_OFFSET / sizeof(uint64_t)] = registration->delivery_value;
    if ((registration->flags & EVENT_SA_SIGINFO) != 0u) {
        frame[EVENT_FRAME_RSI_OFFSET / sizeof(uint64_t)] = siginfo_addr;
        frame[EVENT_FRAME_RDX_OFFSET / sizeof(uint64_t)] = ucontext_addr;
    }
    frame[EVENT_FRAME_RCX_OFFSET / sizeof(uint64_t)] = registration->handler;
    frame[EVENT_FRAME_RSP_OFFSET / sizeof(uint64_t)] = handler_rsp;
    process->event_saved_mask = process->event_mask;
    /* A compatibility personality may attach a native-event mask to an
       action (for example Linux sa_mask).  Keeping the mask in the shared
       group metadata lets the generic delivery path block those events for
       the duration of the handler without exposing Linux signal numbers in
       the native ABI. */
    process->event_mask |= 1u << event_id;
    process->event_mask |= group->event_action_mask[event_id];
    process->event_saved_event = (uint8_t)event_id;
    process->event_active = 1u;
}

static void event_clear_active(process_t *process) {
    process->event_mask = process->event_saved_mask;
    process->event_saved_event = 0u;
    process->event_context_kind = VEX_EVENT_CONTEXT_NONE;
    process->event_user_context_addr = 0u;
    process->event_active = 0u;
}

int vex_event_enter_sync(struct interrupt_frame *opaque_frame,
                         const vex_sync_fault_t *fault,
                         uint16_t event_id,
                         const vex_event_registration_t *registration,
                         const void *siginfo, uint32_t siginfo_size,
                         const void *user_context, uint32_t context_size,
                         uint32_t context_kind) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    process_t *process = scheduler_current_process();
    interrupt_frame_t *frame = (interrupt_frame_t *)opaque_frame;
    uint64_t context_addr;
    uint64_t siginfo_addr = 0u;
    uint64_t handler_rsp;
    uint64_t top;

    if (process == NULL || process->user_mode == 0u || frame == NULL ||
        fault == NULL || registration == NULL || event_id >= VEX_EVENT_SLOT_COUNT ||
        registration->handler <= 1u || registration->restorer == 0u ||
        user_context == NULL || context_size == 0u ||
        (context_kind != VEX_EVENT_CONTEXT_SYNC &&
         context_kind != VEX_EVENT_CONTEXT_SYNC_COMPAT) ||
        process->event_active != 0u ||
        !event_code_valid(process, registration->handler) ||
        !event_code_valid(process, registration->restorer)) {
        return -1;
    }

    top = event_handler_stack(process, registration, frame->rsp);
    if (siginfo_size != 0u && siginfo == NULL) {
        return -1;
    }
    if (top < (uint64_t)context_size + (uint64_t)siginfo_size + 64u) {
        return -1;
    }
    context_addr = (top - context_size) & ~15ull;
    if (context_addr > top || context_addr < siginfo_size + 32u) {
        return -1;
    }
    if (siginfo_size != 0u) {
        siginfo_addr = (context_addr - siginfo_size) & ~15ull;
        if (siginfo_addr > context_addr || siginfo_addr < 16u) {
            return -1;
        }
        handler_rsp = siginfo_addr - sizeof(uint64_t);
    } else {
        handler_rsp = context_addr - sizeof(uint64_t);
    }
    /* The handler sees the SysV function-entry alignment: RSP == 8 mod 16,
       with the registered restorer as its return address. */
    handler_rsp &= ~15ull;
    handler_rsp |= 8u;
    if (handler_rsp >= (siginfo_size != 0u ? siginfo_addr : context_addr) ||
        !vex_syscall_user_range_valid(handler_rsp, sizeof(uint64_t), 1) ||
        !vex_syscall_user_range_valid(context_addr, context_size, 1) ||
        (siginfo_size != 0u && !vex_syscall_user_range_valid(
            siginfo_addr, siginfo_size, 1))) {
        return -1;
    }

    memcpy(process->event_saved_frame, frame, sizeof(*frame));
    process->event_context_kind = (uint8_t)context_kind;
    process->event_user_context_addr = context_addr;
    process->event_saved_event = (uint8_t)event_id;
    process->event_saved_mask = process->event_mask;
    process->event_mask |= 1u << event_id;
    process->event_active = 1u;

    memcpy((void *)(uintptr_t)context_addr, user_context, context_size);
    if (siginfo_size != 0u) {
        memcpy((void *)(uintptr_t)siginfo_addr, siginfo, siginfo_size);
    }
    *(volatile uint64_t *)(uintptr_t)handler_rsp = registration->restorer;

    frame->rdi = registration->delivery_value;
    if ((registration->flags & EVENT_SA_SIGINFO) != 0u) {
        frame->rsi = siginfo_addr;
        frame->rdx = context_addr;
    }
    frame->rip = registration->handler;
    frame->rsp = handler_rsp;
    /* Do not let a handler inherit privileged or virtual-8086 flags from a
       userspace-crafted context.  The saved context remains exact for return. */
    frame->rflags &= ~((3ull << 12u) | (1ull << 14u) | (1ull << 17u));
    if (process->event_saved_fpu_valid != 0u) {
        scheduler_restore_fpu_state(process->event_saved_fpu_state);
    }
    return 0;
}

int vex_event_deliver_sync_native(struct interrupt_frame *opaque_frame,
                                  const vex_sync_fault_t *fault) {
    process_t *process = scheduler_current_process();
    vex_event_registration_t registration;
    vex_event_fault_payload_t payload;
    vex_event_user_context_t context;
    interrupt_frame_t *frame = (interrupt_frame_t *)opaque_frame;
    uint16_t event_id = VEX_EVENT_SYNCHRONOUS_FAULT;

    if (process == NULL || process->group == NULL || frame == NULL || fault == NULL ||
        vex_event_query(event_id, &registration) != 0 ||
        registration.handler <= 1u || registration.restorer == 0u ||
        (registration.flags & EVENT_SA_SIGINFO) == 0u) {
        return -1;
    }
    memset(&payload, 0, sizeof(payload));
    payload.version = 1u;
    payload.size = sizeof(payload);
    payload.vector = fault->vector;
    payload.error_code = fault->error_code;
    payload.address = fault->address;
    payload.instruction = fault->instruction;
    payload.access = fault->access;
    payload.address_valid = fault->address_valid;

    memset(&context, 0, sizeof(context));
    context.r15 = frame->r15; context.r14 = frame->r14;
    context.r13 = frame->r13; context.r12 = frame->r12;
    context.r11 = frame->r11; context.r10 = frame->r10;
    context.r9 = frame->r9; context.r8 = frame->r8;
    context.rdi = frame->rdi; context.rsi = frame->rsi;
    context.rbp = frame->rbp; context.rdx = frame->rdx;
    context.rcx = frame->rcx; context.rbx = frame->rbx;
    context.rax = frame->rax; context.rip = frame->rip;
    context.cs = frame->cs; context.rflags = frame->rflags;
    context.rsp = frame->rsp; context.ss = frame->ss;
    context.vector = fault->vector; context.error_code = fault->error_code;
    context.fault_address = fault->address_valid != 0u ? fault->address : 0u;

    return vex_event_enter_sync(opaque_frame, fault, event_id, &registration,
        &payload, sizeof(payload), &context, sizeof(context),
        VEX_EVENT_CONTEXT_SYNC);
}

int vex_event_sigreturn_context(const vex_event_user_context_t *context) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    process_t *process = scheduler_current_process();
    const interrupt_frame_t *saved;
    uint64_t *frame;
    uint64_t rsp_probe;
    const uint64_t forbidden_flags = (3ull << 12u) | (1ull << 14u) | (1ull << 17u);

    if (process == NULL || process->event_active == 0u ||
        process->syscall_frame_rsp == 0u || context == NULL ||
        (process->event_context_kind != VEX_EVENT_CONTEXT_SYNC &&
         process->event_context_kind != VEX_EVENT_CONTEXT_SYNC_COMPAT &&
         process->event_context_kind != VEX_EVENT_CONTEXT_ASYNC_COMPAT)) {
        return -1;
    }
    saved = (const interrupt_frame_t *)(const void *)process->event_saved_frame;
    if ((context->cs & 3u) != 3u || context->cs != saved->cs ||
        context->ss != saved->ss || (context->rflags & forbidden_flags) != 0u ||
        !event_code_valid(process, context->rip) || context->rsp == 0u ||
        context->rsp > UINT64_MAX - sizeof(uint64_t)) {
        return -1;
    }
    rsp_probe = context->rsp - 1u;
    if (!vex_syscall_user_range_valid(rsp_probe, 1u, 1)) {
        return -1;
    }

    frame = (uint64_t *)(uintptr_t)process->syscall_frame_rsp;
    frame[24u / 8u] = context->r15;
    frame[32u / 8u] = context->r14;
    frame[40u / 8u] = context->r13;
    frame[48u / 8u] = context->r12;
    frame[56u / 8u] = context->r10;
    frame[64u / 8u] = context->r9;
    frame[72u / 8u] = context->r8;
    frame[80u / 8u] = context->rdi;
    frame[88u / 8u] = context->rsi;
    frame[96u / 8u] = context->rbp;
    frame[104u / 8u] = context->rdx;
    frame[112u / 8u] = context->rbx;
    frame[120u / 8u] = context->r11;
    frame[128u / 8u] = context->rcx;
    frame[136u / 8u] = context->rsp;
    frame[0u] = context->rip;
    /* Ring 3 cannot disable interrupt delivery through a crafted context. */
    frame[8u / 8u] = (context->rflags & 0x3f7fd7ull) | 0x202ull;
    process->event_return_rax = context->rax;
    process->event_return_rax_valid = 1u;
    if (process->event_saved_fpu_valid != 0u) {
        scheduler_restore_fpu_state(process->event_saved_fpu_state);
        process->event_saved_fpu_valid = 0u;
    }
    event_clear_active(process);
    return 0;
}

uint64_t vex_event_user_context_address(void) {
    process_t *process = scheduler_current_process();
    return process == NULL ? 0u : process->event_user_context_addr;
}

uint32_t vex_event_context_kind(void) {
    process_t *process = scheduler_current_process();
    return process == NULL ? VEX_EVENT_CONTEXT_NONE : process->event_context_kind;
}

int vex_event_sigreturn(void) {
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    process_t *process = scheduler_current_process();

    if (process == NULL || process->event_active == 0u ||
        process->syscall_frame_rsp == 0u) {
        return -1;
    }
    if (process->event_context_kind == VEX_EVENT_CONTEXT_SYNC) {
        vex_event_user_context_t context;
        if (process->event_user_context_addr == 0u ||
            !vex_syscall_user_range_valid(process->event_user_context_addr,
                                          sizeof(context), 0)) {
            return -1;
        }
        memcpy(&context, (const void *)(uintptr_t)process->event_user_context_addr,
               sizeof(context));
        return vex_event_sigreturn_context(&context);
    }
    if (process->event_context_kind != VEX_EVENT_CONTEXT_ASYNC) {
        return -1;
    }
    memcpy((void *)(uintptr_t)process->syscall_frame_rsp,
           process->event_saved_frame, SCHEDULER_SYSCALL_FRAME_SIZE);
    /* The complete-register return uses IRET, whose RIP/RFLAGS come from
       scratch slots 0/1. In a saved SYSCALL frame those slots originally
       contain outgoing C arguments, not the user return address. */
    uint64_t *return_frame=(uint64_t *)(uintptr_t)process->syscall_frame_rsp;
    return_frame[0]=return_frame[EVENT_FRAME_RCX_OFFSET/sizeof(uint64_t)];
    return_frame[1]=return_frame[120u/sizeof(uint64_t)];
    if(process->event_saved_fpu_valid) {
        scheduler_restore_fpu_state(process->event_saved_fpu_state);
        process->event_saved_fpu_valid=0u;
    }
    process->event_return_rax=process->event_saved_rax;
    process->event_return_rax_valid=1u;
    event_clear_active(process);
    return 0;
}

void vex_event_thread_exit(process_t *process) {
    if (process == NULL) {
        return;
    }
    process->event_pending = 0u;
    process->event_mask = 0u;
    memset(process->event_sender_pid, 0, sizeof(process->event_sender_pid));
    process->event_active = 0u;
    process->event_context_kind = VEX_EVENT_CONTEXT_NONE;
    process->event_user_context_addr = 0u;
    process->event_saved_fpu_valid = 0u;
    process->event_return_rax_valid = 0u;
    process->event_return_rax = 0u;
    memset(process->event_saved_frame, 0, sizeof(process->event_saved_frame));
    memset(process->event_saved_fpu_state, 0, sizeof(process->event_saved_fpu_state));
}
