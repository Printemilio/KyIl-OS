.global _start
.section .text
_start:
    mov $9,%eax
    mov $0x630000000,%rdi
    mov $4096,%esi
    mov $3,%edx
    mov $0x32,%r10d
    mov $-1,%r8
    xor %r9d,%r9d
    syscall
    mov $0x630000000,%r13
    cmp %r13,%rax
    jne fail
    mov $22,%eax
    lea descriptors(%rip),%rdi
    syscall
    test %rax,%rax
    jnz fail
    mov $9,%eax
    xor %edi,%edi
    mov $4096,%esi
    mov $3,%edx
    mov $0x22,%r10d
    mov $-1,%r8
    xor %r9d,%r9d
    syscall
    cmp $-4095,%rax
    jae fail
    mov %rax,%r14
    mov $9,%eax
    mov $32768,%esi
    syscall
    cmp $-4095,%rax
    jae fail
    lea 32768(%rax),%rsi
    mov $56,%eax
    mov $0x01210f00,%edi
    xor %edx,%edx
    lea 20(%r13),%r10
    xor %r8d,%r8d
    syscall
    test %rax,%rax
    js fail
    jz child
    mov %eax,8(%r13)
wait_inspector:
    cmpl $0,12(%r13)
    jne invalidate
    mov $24,%eax
    syscall
    jmp wait_inspector
invalidate:
    mov %r14,%rdi
    mov $4096,%esi
    cmpl $0,16(%r13)
    je unmap
    mov $10,%eax
    mov $1,%edx
    syscall
    jmp invalidated
unmap:
    mov $11,%eax
    syscall
invalidated:
    test %rax,%rax
    jnz fail
    mov $1,%eax
    mov descriptors+4(%rip),%edi
    lea bytes(%rip),%rsi
    mov $8,%edx
    syscall
    cmp $8,%rax
    jne fail
join:
    mov 20(%r13),%edx
    test %edx,%edx
    jz success
    mov $202,%eax
    lea 20(%r13),%rdi
    mov $128,%esi
    xor %r10d,%r10d
    xor %r8d,%r8d
    xor %r9d,%r9d
    syscall
    jmp join
child:
    movl $1,(%r13)
    xor %eax,%eax
    mov descriptors(%rip),%edi
    mov %r14,%rsi
    mov $8,%edx
    syscall
    cmp $-14,%rax
    jne fail
    mov $60,%eax
    xor %edi,%edi
    syscall
success:
    mov $231,%eax
    xor %edi,%edi
    syscall
fail:
    mov $231,%eax
    mov $93,%edi
    syscall
.section .rodata
bytes: .quad 0x19732026
.section .bss
.align 4
descriptors: .zero 8
.section .note.GNU-stack,"",@progbits
