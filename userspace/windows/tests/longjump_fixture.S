.def @feat.00; .scl 3; .type 0; .endef
.set @feat.00, 0x800
.section .gfids$y,"dr"
.symidx cfg_dummy
.section .gljmp$y,"dr"
.symidx longjump_landing
.text
.global cfg_dummy
.p2align 4
cfg_dummy: ret
.global longjump_fixture,longjump_landing
.seh_proc longjump_fixture
longjump_fixture:
    push %rbx
    .seh_pushreg %rbx
    push %r12
    .seh_pushreg %r12
    sub $72,%rsp
    .seh_stackalloc 72
    movdqu %xmm6,32(%rsp)
    .seh_savexmm %xmm6,32
    .seh_endprologue
    mov %rsp,longjump_buffer(%rip)
    mov %rbx,longjump_buffer+8(%rip)
    mov %rsp,longjump_buffer+16(%rip)
    mov %rbp,longjump_buffer+24(%rip)
    mov %rsi,longjump_buffer+32(%rip)
    mov %rdi,longjump_buffer+40(%rip)
    mov %r12,longjump_buffer+48(%rip)
    mov %r13,longjump_buffer+56(%rip)
    mov %r14,longjump_buffer+64(%rip)
    mov %r15,longjump_buffer+72(%rip)
    lea longjump_landing(%rip),%rax
    mov %rax,longjump_buffer+80(%rip)
    stmxcsr longjump_buffer+88(%rip)
    fnstcw longjump_buffer+92(%rip)
    movdqu %xmm6,longjump_buffer+96(%rip)
    movdqu %xmm7,longjump_buffer+112(%rip)
    movdqu %xmm8,longjump_buffer+128(%rip)
    movdqu %xmm9,longjump_buffer+144(%rip)
    movdqu %xmm10,longjump_buffer+160(%rip)
    movdqu %xmm11,longjump_buffer+176(%rip)
    movdqu %xmm12,longjump_buffer+192(%rip)
    movdqu %xmm13,longjump_buffer+208(%rip)
    movdqu %xmm14,longjump_buffer+224(%rip)
    movdqu %xmm15,longjump_buffer+240(%rip)
    call do_longjump
    ud2
longjump_landing:
    cmp $42,%eax
    jne 1f
    movabs $0x1234567887654321,%rdx
    cmp %rdx,%rbx
    jne 1f
    movabs $0x1122334455667788,%rdx
    cmp %rdx,%r12
    jne 1f
    movq %xmm6,%rax
    movabs $0x9988776655443322,%rdx
    cmp %rdx,%rax
    jne 1f
    mov $1,%eax
    jmp 2f
1:  xor %eax,%eax
2:  movdqu 32(%rsp),%xmm6
    add $72,%rsp
    pop %r12
    pop %rbx
    ret
.seh_endproc
