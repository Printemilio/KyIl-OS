.global _start
.section .text
_start:
    xor %r15d, %r15d
cycle:
    /* Child and worker own the same writer. Only Process-wide termination
     * can produce EOF; killing only the leader leaves the worker/FD alive. */
    mov $293, %eax
    lea fds(%rip), %rdi
    mov $0x80000, %esi
    syscall
    test %rax, %rax
    js fail
    mov $57, %eax
    syscall
    test %rax, %rax
    js fail
    jz child
    mov %rax, %r12
    mov $3, %eax
    mov fds+4(%rip), %edi
    syscall
    xor %eax, %eax
    mov fds(%rip), %edi
    lea worker_tid(%rip), %rsi
    mov $4, %edx
    syscall
    cmp $4, %rax
    jne fail
    mov worker_tid(%rip), %r13d
    test $1, %r15d
    jnz kill_worker
    mov $62, %eax             # kill(Process, SIGKILL)
    mov %r12, %rdi
    mov $9, %esi
    syscall
    jmp killed
kill_worker:
    mov $234, %eax            # tgkill(Process, worker, SIGKILL)
    mov %r12, %rdi
    mov %r13, %rsi
    mov $9, %edx
    syscall
killed:
    test %rax, %rax
    jne fail
    xor %eax, %eax
    mov fds(%rip), %edi
    lea worker_tid(%rip), %rsi
    mov $4, %edx
    syscall
    test %rax, %rax           # must be real EOF, not EAGAIN or fake success
    jne fail
    mov $61, %eax
    mov %r12, %rdi
    lea status(%rip), %rsi
    xor %edx, %edx
    xor %r10d, %r10d
    syscall
    cmp %r12, %rax
    jne fail
    cmpl $9, status(%rip)    # WIFSIGNALED + WTERMSIG(SIGKILL), not an exit(137)
    jne fail
    mov $3, %eax
    mov fds(%rip), %edi
    syscall
    mov $62, %eax
    mov $0x7fffffff, %edi
    mov $9, %esi
    syscall
    cmp $-3, %rax             # absent Process -> ESRCH
    jne fail
    inc %r15d
    cmp $4, %r15d
    jb cycle
    mov $1, %eax
    mov $1, %edi
    lea ok(%rip), %rsi
    mov $ok_len, %edx
    syscall
    mov $231, %eax
    xor %edi, %edi
    syscall
child:
    mov $3, %eax
    mov fds(%rip), %edi
    syscall
    mov $9, %eax
    xor %edi, %edi
    mov $16384, %esi
    mov $3, %edx
    mov $0x22, %r10d
    mov $-1, %r8
    xor %r9d, %r9d
    syscall
    cmp $-4095, %rax
    jae fail
    lea 16384(%rax), %rsi
    mov $56, %eax
    mov $0x10f00, %edi        # VM|FS|FILES|SIGHAND|THREAD
    xor %edx, %edx
    xor %r10d, %r10d
    xor %r8d, %r8d
    syscall
    test %rax, %rax
    js fail
    jz sleep_thread
    mov %eax, worker_tid(%rip)
    mov $1, %eax
    mov fds+4(%rip), %edi
    lea worker_tid(%rip), %rsi
    mov $4, %edx
    syscall
    cmp $4, %rax
    jne fail
sleep_thread:
    mov $202, %eax
    lea word(%rip), %rdi
    mov $128, %esi
    xor %edx, %edx
    xor %r10d, %r10d
    xor %r8d, %r8d
    xor %r9d, %r9d
    syscall
    jmp sleep_thread
fail:
    mov $231, %eax
    mov $1, %edi
    syscall
    ud2
.section .rodata
ok: .ascii "Linux SIGKILL Process/worker targeting, all-thread EOF and repeated cleanup PASS\n"
.set ok_len, .-ok
.section .bss
.balign 4
fds: .skip 8
worker_tid: .skip 4
status: .skip 4
word: .skip 4
.section .note.GNU-stack,"",@progbits
