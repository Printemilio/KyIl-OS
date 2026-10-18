.text
.globl spin_registers
spin_registers:
    push %r12
    push %r13
    sub $24,%rsp
    movdqu %xmm6,(%rsp)
    movabs $0x1234567887654321,%r12
    movabs $0xfedcba9801234567,%r13
    movdqu pattern(%rip),%xmm6
    movl $1,4(%rcx)
1:  addq $1,8(%rcx)
    cmpl $0,(%rcx)
    je 1b
    movdqu (%rsp),%xmm6
    add $24,%rsp
    pop %r13
    pop %r12
    xor %eax,%eax
    ret
.section .rdata,"dr"
.p2align 4
pattern: .quad 0x9988776655443322,0x1122334455667788
