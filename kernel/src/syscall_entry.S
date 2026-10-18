.section .text
.global syscall_entry
.type syscall_entry, @function

.extern scheduler_record_syscall_frame
.extern syscall_entry_fcw_probe

# x86_64 syscall entry.
# Userspace convention:
#   rax = syscall number
#   rdi = arg1
#   rsi = arg2
#   rdx = arg3
#   r10 = arg4
#   r8  = arg5
#   r9  = arg6
# Return:
#   rax = syscall return value
syscall_entry:
    # SYSCALL leaves RSP unchanged.  Disable interrupts for the tiny window
    # in which the old user RSP and syscall number are copied to scratch, then
    # switch to the current process's kernel stack before making any push or
    # call.  The saved user RSP is placed below the register frame and is
    # popped directly into RSP on return.
    cli
    swapgs
    mov %rax, %gs:16
    mov %rsp, %gs:8
    mov %gs:0, %rsp
    /* Temporary passive probe, before entering any C diagnostics. */
    fnstcw syscall_entry_fcw_probe(%rip)
    pushq %gs:8
    push %rcx
    push %r11
    push %rbx
    push %rdx
    push %rbp
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r12
    push %r13
    push %r14
    push %r15

    sub $24, %rsp
    mov %gs:16, %rax
    mov %rax, 16(%rsp)
    mov %rsp, %rdi
    call scheduler_record_syscall_frame
    mov 16(%rsp), %rdi
    mov 80(%rsp), %rsi
    mov 88(%rsp), %rdx
    mov 104(%rsp), %rcx
    mov 56(%rsp), %r8
    mov 72(%rsp), %r9
    mov 64(%rsp), %rax
    mov %rax, (%rsp)
    mov 112(%rsp), %rax
    mov %rax, 8(%rsp)
    # Offset 16 is no longer needed for the syscall number. Return mode is
    # invocation-local: a suspended syscall must not inherit another thread's.
    movq $0, 16(%rsp)
    sti
    call syscall_dispatch_raw
    cli
    add $24, %rsp

    cmpq $0, -8(%rsp)
    jne syscall_return_iret

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rbp
    pop %rdx
    pop %rbx
    pop %r11
    pop %rcx
    pop %rsp
    swapgs
    sysretq

/* rt_sigreturn must restore the complete interrupted register set.  SYSRET
 * consumes RCX/R11 as RIP/RFLAGS, so synchronous-fault returns use IRETQ. */
syscall_return_iret:
    mov %rax, %gs:32
    sub $40, %rsp
    /* Scratch locals at the bottom of the SYSCALL frame hold the requested
       RIP and RFLAGS; the remaining frame is still intact above this IRET. */
    mov 16(%rsp), %rax
    mov %rax, 0(%rsp)
    movq $0x2b, 8(%rsp)
    mov 24(%rsp), %rax
    mov %rax, 16(%rsp)
    mov 152(%rsp), %rax
    mov %rax, 24(%rsp)
    movq $0x23, 32(%rsp)

    mov 40(%rsp), %r15
    mov 48(%rsp), %r14
    mov 56(%rsp), %r13
    mov 64(%rsp), %r12
    mov 72(%rsp), %r10
    mov 80(%rsp), %r9
    mov 88(%rsp), %r8
    mov 96(%rsp), %rdi
    mov 104(%rsp), %rsi
    mov 112(%rsp), %rbp
    mov 120(%rsp), %rdx
    mov 128(%rsp), %rbx
    mov 136(%rsp), %r11
    mov 144(%rsp), %rcx
    mov %gs:32, %rax
    swapgs
    iretq
