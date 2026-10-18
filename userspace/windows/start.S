.section .note.vexos,"a",@note
.balign 4
.long 6,4,1
.asciz "VEXOS"
.balign 4
.long 0x5645584e
.section .text
.global _start
.type _start,@function
_start:
    xor %ebp,%ebp
    mov (%rsp),%rdi
    lea 8(%rsp),%rsi
    and $-16,%rsp
    call wr_main
    mov %eax,%edi
    call wr_exit
.global wr_enter_on_stack
.type wr_enter_on_stack,@function
wr_enter_on_stack:
    push %rbx
    mov %rsp,%rbx
    mov %rsi,%rax
    mov %rdi,%rsp
    and $-16,%rsp
    mov %rdx,%rdi
    call *%rax
    mov %rbx,%rsp
    pop %rbx
    ret
.section .note.GNU-stack,"",@progbits
