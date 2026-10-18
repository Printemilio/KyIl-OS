.global _start
.section .text
/* Kernel orchestrates parent death while CLONE_VM|CLONE_VFORK borrows its
   memory. The child then faults an untouched sparse page and exits or execs. */
_start:
    cmpq $2,(%rsp)
    je success
    mov $9,%eax
    mov $0x600000000,%rdi
    mov $4096,%esi
    mov $3,%edx
    mov $0x32,%r10d
    mov $-1,%r8
    xor %r9d,%r9d
    syscall
    mov $0x600000000,%r14
    cmp %r14,%rax
    jne fail
    mov $9,%eax
    mov $0x610000000,%rdi
    mov $0x1000000,%esi
    syscall
    mov $0x610000000,%r13
    cmp %r13,%rax
    jne fail
    movq $0x1973,(%r13)
    mov $9,%eax
    xor %edi,%edi
    mov $32768,%esi
    syscall
    cmp $-4095,%rax
    jae fail
    mov %rax,clone_args+40(%rip)
    mov $435,%eax
    lea clone_args(%rip),%rdi
    mov $88,%esi
    syscall
    test %rax,%rax
    jnz fail
    mov $186,%eax
    syscall
    mov %eax,(%r14)
wait_release:
    /* Only the test uses a yielding rendezvous; production waits are unchanged. */
    cmpl $0,8(%r14)
    jne released
    mov $24,%eax
    syscall
    jmp wait_release
released:
    cmpq $0x1973,(%r13)
    jne fail
    add $0xfff000,%r13
    cmpq $0,(%r13)
    jne fail
    movq $0x2026,(%r13)
    cmpq $0x2026,(%r13)
    jne fail
    cmpl $0,4(%r14)
    je success
    mov $59,%eax
    lea path(%rip),%rdi
    lea argv(%rip),%rsi
    lea envp(%rip),%rdx
    syscall
fail:
    mov $231,%eax
    mov $91,%edi
    syscall
success:
    mov $231,%eax
    xor %edi,%edi
    syscall
.section .rodata
path: .asciz "/usr/linux-tests/linux_spawn_borrow_smoke"
child_arg: .asciz "exec-child"
.section .data
.align 8
argv: .quad path,child_arg,0
envp: .quad 0
clone_args: .quad 0x4100,0,0,0,17,0,32768,0,0,0,0
.section .note.GNU-stack,"",@progbits
