.section .text
.global scheduler_context_switch
.type scheduler_context_switch, @function

# void scheduler_context_switch(uint64_t *old_rsp, uint64_t new_rsp, uint64_t new_cr3)
# rdi = old_rsp storage, rsi = new rsp, rdx = new cr3 physical address.
scheduler_context_switch:
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

    mov %rsp, (%rdi)

    mov %cr3, %rax
    cmp %rax, %rdx
    je 1f
    mov %rdx, %cr3
1:
    mov %rsi, %rsp

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
    ret

