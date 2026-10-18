.global _start
/* Spawn/exec from a non-leader thread while its process leader is blocked.
   Both successful exec and early failed-exec exit must release the vfork wait. */
.section .text
_start:
    cmpq $2,(%rsp)
    jne parent
    mov 16(%rsp),%rax
    cmpl $0x6c696863,(%rax) /* "chil" */
    jne fail
    mov $231,%eax
    xor %edi,%edi
    syscall
parent:
    mov $9,%eax
    xor %edi,%edi
    mov $32768,%esi
    mov $3,%edx
    mov $0x22,%r10d
    mov $-1,%r8
    xor %r9d,%r9d
    syscall
    cmp $-4095,%rax
    jae fail
    lea 32768(%rax),%rsi
    mov $56,%eax
    mov $0x01210f00,%edi
    xor %edx,%edx
    lea tid(%rip),%r10
    xor %r8d,%r8d
    syscall
    test %rax,%rax
    js fail
    jz worker
join:
    mov tid(%rip),%edx
    test %edx,%edx
    jz done
    mov $202,%eax
    lea tid(%rip),%rdi
    mov $128,%esi
    xor %r10d,%r10d
    xor %r8d,%r8d
    xor %r9d,%r9d
    syscall
    jmp join
done:
    mov $1,%eax
    mov $1,%edi
    lea message(%rip),%rsi
    mov $message_len,%edx
    syscall
    mov $231,%eax
    xor %edi,%edi
    syscall
worker:
    mov $9,%eax
    xor %edi,%edi
    mov $16384,%esi
    mov $3,%edx
    mov $0x22,%r10d
    mov $-1,%r8
    xor %r9d,%r9d
    syscall
    cmp $-4095,%rax
    jae fail
    lea 16384(%rax),%r15
    mov %rax,clone_args+40(%rip)
    xor %r12d,%r12d
spawn:
    test $2,%r12d
    jz legacy_clone
    mov $435,%eax
    lea clone_args(%rip),%rdi
    mov $88,%esi
    syscall
    jmp cloned
legacy_clone:
    mov $56,%eax
    mov $0x4111,%edi /* CLONE_VM | CLONE_VFORK | SIGCHLD */
    mov %r15,%rsi
    xor %edx,%edx
    xor %r10d,%r10d
    xor %r8d,%r8d
    syscall
cloned:
    test %rax,%rax
    js fail
    jz child
    mov %rax,%r13
    mov $61,%eax
    mov %r13,%rdi
    lea status(%rip),%rsi
    xor %edx,%edx
    xor %r10d,%r10d
    syscall
    cmp %r13,%rax
    jne fail
    mov %r12d,%edx
    and $1,%edx
    imul $0x700,%edx,%edx
    cmp status(%rip),%edx
    jne fail
    inc %r12d
    cmp $16,%r12d
    jb spawn
    mov $60,%eax
    xor %edi,%edi
    syscall
child:
    lea path(%rip),%rdi
    test $1,%r12d
    jz exec
    lea absent(%rip),%rdi
exec:
    mov $59,%eax
    lea argv(%rip),%rsi
    lea envp(%rip),%rdx
    syscall
    cmp $-2,%rax
    jne fail
    mov $60,%eax
    mov $7,%edi
    syscall
fail:
    mov $1,%eax
    mov $2,%edi
    lea error(%rip),%rsi
    mov $error_len,%edx
    syscall
    mov $231,%eax
    mov $1,%edi
    syscall
.section .rodata
path: .asciz "/usr/linux-tests/linux_spawn_thread_smoke"
child_arg: .asciz "child"
absent: .asciz "/usr/linux-tests/no_such_spawn_thread_image"
message: .ascii "SMP spawn non-leader: 16 exec/failed-exec/wait handshakes PASS\n"
message_len=.-message
error: .ascii "SMP spawn non-leader: FAILED\n"
error_len=.-error
.section .data
.align 8
argv: .quad path,child_arg,0
envp: .quad 0
clone_args: .quad 0x4100,0,0,0,17,0,16384,0,0,0,0
.section .bss
.align 4
tid: .zero 4
status: .zero 4
.section .note.GNU-stack,"",@progbits
