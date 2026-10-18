.section .text
.global scheduler_context_switch
.type scheduler_context_switch, @function

# void scheduler_context_switch(uint64_t *old_rsp, uint64_t new_rsp,
#                               uint64_t new_cr3, void *old_fpu,
#                               const void *new_fpu, uint64_t xsave_mask,
#                               uint64_t new_fs_base, uint64_t user_gs_base)
# rdi = old rsp storage, rsi = new rsp, rdx = new cr3 physical address,
# rcx/r8 = old/new extended FPU state, r9 = XSAVE mask (0 = FXSAVE),
# stack arg 7 = FS_BASE for the task being resumed.
scheduler_context_switch:
    pushfq
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rbp
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    # The argument registers have now been preserved in the old task's
    # frame.  r10/r11 are scratch registers for the incoming FS_BASE/CR3.
    # Reading FS_BASE after the pushes avoids clobbering the old task's r10.
    mov 136(%rsp), %r10
    mov 144(%rsp), %rbx   # callee-saved across the VMM helper; old RBX is saved
    mov %rdx, %r11

    test %r9, %r9
    jz 0f
    mov %r9, %rax
    mov %r9, %rdx
    shr $32, %rdx
    xsave64 (%rcx)
    jmp 1f
0:
    fxsave64 (%rcx)
1:

    mov %rsp, (%rdi)

    # Pin the incoming root before loading CR3, and drop the outgoing CPU
    # lease only afterwards. Saved callee registers keep switch arguments
    # intact across the private VMM helper (originals are already in frame).
    mov %rsi,%r12
    mov %r8,%r13
    mov %r9,%r14
    mov %r10,%r15
    mov %r11,%rdi
    sub $8,%rsp
    call vmm_activate_cr3
    add $8,%rsp
    test %eax,%eax
    jnz 9f
    mov %r12,%rsi
    mov %r13,%r8
    mov %r14,%r9
    mov %r15,%r10
    mov %rsi, %rsp

    test %r9, %r9
    jz 2f
    mov %r9, %rax
    mov %r9, %rdx
    shr $32, %rdx
    xrstor64 (%r8)
    jmp 3f
2:
    fxrstor64 (%r8)
3:
    mov $0xc0000100, %ecx
    mov %r10, %rax
    mov %r10, %rdx
    shr $32, %rdx
    wrmsr
    # Kernel GS remains the incoming CPU's per-CPU object. Update only the
    # inactive SWAPGS user slot. -1 means the slot is already correct; this
    # preserves the zero-GS mono-CPU fast path for ordinary native/Linux code.
    cmp $-1,%rbx
    je 4f
    mov $0xc0000102,%ecx
    mov %rbx,%rax
    mov %rbx,%rdx
    shr $32,%rdx
    wrmsr
4:

    # Incoming frame is 8 mod 16; keep SysV alignment at the C call site.
    # All incoming volatile registers are still saved below this point.
    sub $8, %rsp
    call scheduler_running
    add $8, %rsp

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rbp
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    popfq
    ret

9:  # Loading a retired/unregistered root is a kernel lifetime violation.
    cli
    ud2
