.text
.global wr_fault_entry
.type wr_fault_entry,@function
wr_fault_entry:
    lea wr_fault_dispatch(%rip),%r8
    jmp wr_event_entry
.global wr_control_entry
.type wr_control_entry,@function
wr_control_entry:
    lea wr_control_dispatch(%rip),%r8
wr_event_entry:
    # Preserve interrupted floating state BEFORE any C/SSE instructions.
    # CPUID/XCR0 detection is cached at startup, including the old-CPU path.
    push %rbp
    mov %rsp,%rbp
    push %rdi
    push %rsi
    push %rdx
    push %r8
    sub wr_xstate_size(%rip),%rsp
    sub $64,%rsp
    and $-64,%rsp
    # XSAVE leaves reserved/header bytes untouched. Initialize the buffer
    # using GPRs only, without destroying the floating state being captured.
    cld
    mov %rsp,%rdi
    mov wr_xstate_size(%rip),%rcx
    shr $3,%rcx
    xor %eax,%eax
    rep stosq
    mov wr_xstate_mask(%rip),%rax
    test %rax,%rax
    jz 1f
    mov %rax,%rdx
    shr $32,%rdx
    xsave64 (%rsp)
    jmp 2f
1:  fxsave64 (%rsp)
2:  mov -8(%rbp),%rdi
    mov -16(%rbp),%rsi
    mov -24(%rbp),%rdx
    mov %rsp,%rcx
    call *-32(%rbp)
    mov wr_xstate_mask(%rip),%rax
    test %rax,%rax
    jz 3f
    mov %rax,%rdx
    shr $32,%rdx
    xrstor64 (%rsp)
    jmp 4f
3:  fxrstor64 (%rsp)
4:  mov %rbp,%rsp
    pop %rbp
    ret
.section .note.GNU-stack,"",@progbits
