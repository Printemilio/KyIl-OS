.text
.globl _start
.type _start,@function
_start:
    /* The kernel enters the interpreter with the normal Linux initial stack;
       locate AT_ENTRY and tail-jump to the relocated program. */
    mov (%rsp), %rcx
    lea 8(%rsp,%rcx,8), %rsi
    add $8, %rsi
1:
    cmpq $0, (%rsi)
    je 2f
    add $8, %rsi
    jmp 1b
2:
    add $8, %rsi
3:
    mov (%rsi), %rax
    test %rax, %rax
    je 5f
    cmp $9, %rax
    je 4f
    add $16, %rsi
    jmp 3b
4:
    mov 8(%rsi), %rax
    jmp *%rax
5:
    mov $60, %eax
    mov $127, %edi
    syscall
    hlt
.size _start, .-_start
