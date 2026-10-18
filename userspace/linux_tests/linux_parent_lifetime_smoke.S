.global _start

/* A sibling Thread exits after fork, while the Process still owns the child.
 * The child must remain waitable by its parent Process. */
.set SYS_READ, 0
.set SYS_WRITE, 1
.set SYS_MMAP, 9
.set SYS_CLONE, 56
.set SYS_FORK, 57
.set SYS_EXIT, 60
.set SYS_WAIT4, 61
.set SYS_FUTEX, 202
.set SYS_EXIT_GROUP, 231
.set SYS_PIPE2, 293
.set CLONE_FLAGS, 0x01210f00 /* VM|FS|FILES|SIGHAND|THREAD|CHILD_SETTID|CHILD_CLEARTID */
.set FUTEX_WAIT_PRIVATE, 128

.section .text
_start:
    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $0x4000, %rsi
    mov $3, %rdx
    mov $0x22, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae fail

    lea 0x4000(%rax), %rsi
    and $-16, %rsi
    mov $SYS_CLONE, %rax
    mov $CLONE_FLAGS, %rdi
    xor %rdx, %rdx
    lea thread_tid(%rip), %r10
    xor %r8, %r8
    syscall
    test %rax, %rax
    js fail
    jz thread_path

    mov $SYS_PIPE2, %rax
    lea pipe_fds(%rip), %rdi
    xor %rsi, %rsi
    syscall
    test %rax, %rax
    js fail

    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    js fail
    jz fork_child
    mov %rax, %r12

    /* Only now may the sibling exit.  CLONE_CHILD_CLEARTID confirms that
     * process_exit_status() has completed before we release the fork child. */
    movl $1, release_thread(%rip)
join_thread:
    mov thread_tid(%rip), %edx
    test %edx, %edx
    jz thread_joined
    mov $SYS_FUTEX, %rax
    lea thread_tid(%rip), %rdi
    mov $FUTEX_WAIT_PRIVATE, %rsi
    xor %r10, %r10
    xor %r8, %r8
    xor %r9, %r9
    syscall
    jmp join_thread

thread_joined:
    mov $SYS_WRITE, %rax
    mov pipe_fds + 4(%rip), %edi
    lea pipe_byte(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $1, %rax
    jne fail

    mov $SYS_WAIT4, %rax
    mov %r12, %rdi
    lea child_status(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp %r12, %rax
    jne fail
    cmpl $0, child_status(%rip)
    jne fail

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea ok_message(%rip), %rsi
    mov $ok_message_len, %rdx
    syscall
    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall

thread_path:
1:
    cmpl $0, release_thread(%rip)
    jne 2f
    pause
    jmp 1b
2:
    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall

fork_child:
    mov $SYS_READ, %rax
    mov pipe_fds(%rip), %edi
    lea pipe_byte(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $1, %rax
    jne child_fail
    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall
child_fail:
    mov $SYS_EXIT, %rax
    mov $10, %rdi
    syscall

fail:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_message(%rip), %rsi
    mov $fail_message_len, %rdx
    syscall
    mov $SYS_EXIT_GROUP, %rax
    mov $1, %rdi
    syscall

.section .rodata
ok_message:
    .ascii "Vex ELF parent-lifetime smoke: ok\n"
ok_message_len = . - ok_message
fail_message:
    .ascii "Vex ELF parent-lifetime smoke: failed\n"
fail_message_len = . - fail_message

.section .bss
.align 8
thread_tid: .zero 4
release_thread: .zero 4
pipe_fds: .zero 8
pipe_byte: .zero 1
.align 4
child_status: .zero 4
