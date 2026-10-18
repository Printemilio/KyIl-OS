.section .text
.global wr_cfg_check
.type wr_cfg_check,@function
wr_cfg_check:
    # Windows guard ABI: target RAX, preserve arguments and all XMM registers.
    # XMM6-15 are Windows nonvolatile but SysV volatile: saving only XMM0-5
    # would accidentally depend on the validator never using these registers.
    push %rax
    push %rcx
    push %rdx
    push %r8
    push %r9
    push %r10
    push %r11
    push %rdi
    push %rsi
    sub $256,%rsp
    movdqu %xmm0,0(%rsp)
    movdqu %xmm1,16(%rsp)
    movdqu %xmm2,32(%rsp)
    movdqu %xmm3,48(%rsp)
    movdqu %xmm4,64(%rsp)
    movdqu %xmm5,80(%rsp)
    movdqu %xmm6,96(%rsp)
    movdqu %xmm7,112(%rsp)
    movdqu %xmm8,128(%rsp)
    movdqu %xmm9,144(%rsp)
    movdqu %xmm10,160(%rsp)
    movdqu %xmm11,176(%rsp)
    movdqu %xmm12,192(%rsp)
    movdqu %xmm13,208(%rsp)
    movdqu %xmm14,224(%rsp)
    movdqu %xmm15,240(%rsp)
    mov 320(%rsp),%rdi
    call wr_cfg_allowed
    test %eax,%eax
    jz 1f
    movdqu 0(%rsp),%xmm0
    movdqu 16(%rsp),%xmm1
    movdqu 32(%rsp),%xmm2
    movdqu 48(%rsp),%xmm3
    movdqu 64(%rsp),%xmm4
    movdqu 80(%rsp),%xmm5
    movdqu 96(%rsp),%xmm6
    movdqu 112(%rsp),%xmm7
    movdqu 128(%rsp),%xmm8
    movdqu 144(%rsp),%xmm9
    movdqu 160(%rsp),%xmm10
    movdqu 176(%rsp),%xmm11
    movdqu 192(%rsp),%xmm12
    movdqu 208(%rsp),%xmm13
    movdqu 224(%rsp),%xmm14
    movdqu 240(%rsp),%xmm15
    add $256,%rsp
    pop %rsi
    pop %rdi
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdx
    pop %rcx
    pop %rax
    ret
1:  call wr_cfg_failure
    ud2
.global wr_cfg_dispatch
.type wr_cfg_dispatch,@function
wr_cfg_dispatch:
    sub $8,%rsp
    call wr_cfg_check
    add $8,%rsp
    jmp *%rax
.section .note.GNU-stack,"",@progbits
