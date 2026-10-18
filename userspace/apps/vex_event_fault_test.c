#include <stdint.h>

#include <unos/vex_abi.h>

#include "vex_syscall.h"

#define VEX_EVENT_SYNC_FAULT 4u
#define VEX_EVENT_SIGINFO_FLAG 4u

typedef struct native_fault_payload {
    uint32_t version;
    uint32_t size;
    uint64_t vector;
    uint64_t error_code;
    uint64_t address;
    uint64_t instruction;
    uint64_t access;
    uint64_t address_valid;
} native_fault_payload_t;

typedef struct native_user_context {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
    uint64_t vector, error_code, fault_address;
} native_user_context_t;

extern char vex_event_faulting_instruction[];
extern char vex_event_fault_resume[];

static volatile uint64_t fault_handler_count;
static volatile uint64_t fault_handler_failed;

static void fault_handler(uint64_t event_id,
                         const native_fault_payload_t *fault,
                         native_user_context_t *context) {
    if (event_id != VEX_EVENT_SYNC_FAULT || fault == 0 || context == 0 ||
        fault->version != 1u || fault->size != sizeof(*fault) ||
        fault->vector != 14u || fault->address_valid == 0u ||
        fault->address != 0u || fault->instruction !=
            (uint64_t)(uintptr_t)vex_event_faulting_instruction ||
        context->rip != (uint64_t)(uintptr_t)vex_event_faulting_instruction ||
        context->vector != 14u || context->fault_address != 0u) {
        fault_handler_failed = 1u;
        return;
    }
    ++fault_handler_count;
    context->rip = (uint64_t)(uintptr_t)vex_event_fault_resume;
}

__attribute__((noreturn)) static void fault_restorer(void) {
    (void)vex_syscall0(VEX_SYS_EVENT_RETURN);
    for (;;) {
        __asm__ volatile ("pause");
    }
}

__attribute__((noinline)) static void trigger_nonpresent_fault(void) {
    __asm__ volatile (
        "xor %%rax, %%rax\n"
        ".global vex_event_faulting_instruction\n"
        "vex_event_faulting_instruction:\n"
        "movq (%%rax), %%rax\n"
        ".global vex_event_fault_resume\n"
        "vex_event_fault_resume:\n"
        : : : "rax", "memory");
}

static void log_line(const char *text) {
    (void)vex_syscall1(VEX_SYS_DEBUG_LOG, (uint64_t)(uintptr_t)text);
}

int main(void) {
    vex_event_register_req_t registration = {0};

    registration.event_id = VEX_EVENT_SYNC_FAULT;
    registration.handler = (uint64_t)(uintptr_t)fault_handler;
    registration.restorer = (uint64_t)(uintptr_t)fault_restorer;
    registration.flags = VEX_EVENT_SIGINFO_FLAG;
    registration.delivery_value = VEX_EVENT_SYNC_FAULT;
    if (vex_syscall1(VEX_SYS_EVENT_REGISTER,
                     (uint64_t)(uintptr_t)&registration) != 0u) {
        log_line("vex_event_fault: registration failed\n");
        return 1;
    }
    trigger_nonpresent_fault();
    if (fault_handler_count != 1u || fault_handler_failed != 0u) {
        log_line("vex_event_fault: context/restart failed\n");
        return 1;
    }
    log_line("vex_event_fault: native sync fault resumed\n");
    return 0;
}
