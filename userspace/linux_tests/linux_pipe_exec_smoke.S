.global _start

# pipe+fork+dup2+execve smoke:
# - parent creates a pipe and forks
# - child redirects stdout to the pipe and execve()s linux_exec_child
# - parent reads the payload and wait4() recovers exit code 43

.set SYS_READ, 0
.set SYS_WRITE, 1
.set SYS_OPEN, 2
.set SYS_CLOSE, 3
.set SYS_PIPE2, 293
.set SYS_FCNTL, 72
.set SYS_DUP2, 33
.set SYS_FORK, 57
.set SYS_EXECVE, 59
.set SYS_WAIT4, 61
.set SYS_EXIT, 60
.set SYS_EXIT_GROUP, 231
.set O_CLOEXEC, 0x80000
.set F_GETFD, 1
.set F_DUPFD_CLOEXEC, 1030
.set FD_CLOEXEC, 1

.set O_RDWR_CREAT_TRUNC, 0x242

.section .text
_start:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea start_message(%rip), %rsi
    mov $start_message_len, %rdx
    syscall

    lea dummy0_path(%rip), %rdi
    call open_dummy
    lea dummy1_path(%rip), %rdi
    call open_dummy
    lea dummy2_path(%rip), %rdi
    call open_dummy

    /* The exec error pipe used by GLib relies on pipe2(O_CLOEXEC). */
    mov $SYS_PIPE2, %rax
    lea pipe_fds(%rip), %rdi
    mov $O_CLOEXEC, %rsi
    syscall
    test %rax, %rax
    js fail_pipe

    mov $SYS_FCNTL, %rax
    mov pipe_fds(%rip), %edi
    mov $F_GETFD, %esi
    xor %edx, %edx
    syscall
    test $FD_CLOEXEC, %eax
    jz fail_pipe_cloexec
    mov $SYS_FCNTL, %rax
    mov pipe_fds + 4(%rip), %edi
    mov $F_GETFD, %esi
    xor %edx, %edx
    syscall
    test $FD_CLOEXEC, %eax
    jz fail_pipe_cloexec

    /* GIO's posix_spawn setup duplicates its private error descriptor with
       F_DUPFD_CLOEXEC.  Verify both allocation and the close-on-exec bit. */
    mov $SYS_FCNTL, %rax
    mov pipe_fds(%rip), %edi
    mov $F_DUPFD_CLOEXEC, %esi
    mov $3, %edx
    syscall
    test %rax, %rax
    js fail_pipe_dup
    mov %rax, %r12
    mov $SYS_FCNTL, %rax
    mov %r12, %rdi
    mov $F_GETFD, %esi
    xor %edx, %edx
    syscall
    test $FD_CLOEXEC, %eax
    jz fail_pipe_dup
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall

    mov $SYS_FORK, %rax
    syscall
    cmp $0, %rax
    je child
    js fail_fork

parent:
    mov %rax, %r12

    mov $SYS_READ, %rax
    mov pipe_fds(%rip), %edi
    lea read_buffer(%rip), %rsi
    mov $expected_payload_len, %rdx
    syscall
    cmp $expected_payload_len, %rax
    jne fail_read_len

    mov read_buffer(%rip), %rax
    mov expected_payload(%rip), %rbx
    cmp %rbx, %rax
    jne fail_payload
    mov read_buffer + 8(%rip), %al
    cmp expected_payload + 8(%rip), %al
    jne fail_payload

    mov $SYS_WAIT4, %rax
    mov %r12, %rdi
    lea status_word(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp %r12, %rax
    jne fail_wait_pid

    mov status_word(%rip), %eax
    mov %eax, %ebx
    and $0x7f, %ebx
    jne fail_wait_status
    shr $8, %eax
    and $0xff, %eax
    cmp $43, %eax
    jne fail_wait_status

    /* A failed exec must be reported in the child and reaped by the parent;
       returning a PID before this handshake would make the spawn contract
       indistinguishable from success. */
    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    js fail_fork
    jz child_exec_missing
    mov %rax, %r12
    mov $SYS_WAIT4, %rax
    mov %r12, %rdi
    lea status_word(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp %r12, %rax
    jne fail_exec_missing_wait
    mov status_word(%rip), %eax
    shr $8, %eax
    and $0xff, %eax
    cmp $120, %eax
    jne fail_exec_missing_status

    /* An existing non-ELF file is an exec refusal and must follow the same
       child-error path rather than becoming a successful PID. */
    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    js fail_fork
    jz child_exec_refused
    mov %rax, %r12
    mov $SYS_WAIT4, %rax
    mov %r12, %rdi
    lea status_word(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp %r12, %rax
    jne fail_exec_refused_wait
    mov status_word(%rip), %eax
    shr $8, %eax
    and $0xff, %eax
    cmp $120, %eax
    jne fail_exec_refused_status

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall

    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall

child:
    mov $SYS_DUP2, %rax
    mov pipe_fds + 4(%rip), %edi
    mov $1, %rsi
    syscall
    cmp $1, %rax
    jne child_fail_dup2

    mov $SYS_EXECVE, %rax
    lea child_path(%rip), %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall

child_fail_exec:
    mov $SYS_EXIT, %rax
    mov $120, %rdi
    syscall

child_exec_missing:
    mov $SYS_EXECVE, %rax
    lea missing_path(%rip), %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    jmp child_fail_exec

child_exec_refused:
    mov $SYS_EXECVE, %rax
    lea dummy0_path(%rip), %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    jmp child_fail_exec

child_fail_dup2:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea child_dup2_message(%rip), %rsi
    mov $child_dup2_message_len, %rdx
    syscall

    mov $SYS_EXIT, %rax
    mov $122, %rdi
    syscall

open_dummy:
    mov $SYS_OPEN, %rax
    mov $O_RDWR_CREAT_TRUNC, %rsi
    mov $0644, %rdx
    syscall
    test %rax, %rax
    js fail_open_dummy
    ret

fail_open_dummy:
    lea fail_open_dummy_message(%rip), %rsi
    mov $fail_open_dummy_message_len, %rdx
    jmp fail_write

fail_pipe:
    lea fail_pipe_message(%rip), %rsi
    mov $fail_pipe_message_len, %rdx
    jmp fail_write

fail_pipe_cloexec:
    lea fail_pipe_cloexec_message(%rip), %rsi
    mov $fail_pipe_cloexec_message_len, %rdx
    jmp fail_write

fail_pipe_dup:
    lea fail_pipe_dup_message(%rip), %rsi
    mov $fail_pipe_dup_message_len, %rdx
    jmp fail_write

fail_fork:
    lea fail_fork_message(%rip), %rsi
    mov $fail_fork_message_len, %rdx
    jmp fail_write

fail_read_len:
    lea fail_read_len_message(%rip), %rsi
    mov $fail_read_len_message_len, %rdx
    jmp fail_write

fail_payload:
    lea fail_payload_message(%rip), %rsi
    mov $fail_payload_message_len, %rdx
    jmp fail_write

fail_wait_pid:
    lea fail_wait_pid_message(%rip), %rsi
    mov $fail_wait_pid_message_len, %rdx
    jmp fail_write

fail_wait_status:
    lea fail_wait_status_message(%rip), %rsi
    mov $fail_wait_status_message_len, %rdx
    jmp fail_write

fail_exec_missing_wait:
    lea fail_exec_missing_wait_message(%rip), %rsi
    mov $fail_exec_missing_wait_message_len, %rdx
    jmp fail_write

fail_exec_missing_status:
    lea fail_exec_missing_status_message(%rip), %rsi
    mov $fail_exec_missing_status_message_len, %rdx
    jmp fail_write

fail_exec_refused_wait:
    lea fail_exec_refused_wait_message(%rip), %rsi
    mov $fail_exec_refused_wait_message_len, %rdx
    jmp fail_write

fail_exec_refused_status:
    lea fail_exec_refused_status_message(%rip), %rsi
    mov $fail_exec_refused_status_message_len, %rdx
    jmp fail_write

fail_write:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    syscall

    mov $SYS_EXIT_GROUP, %rax
    mov $121, %rdi
    syscall

.section .bss
.align 8
pipe_fds:
    .long 0
    .long 0
status_word:
    .long 0
read_buffer:
    .zero 16

.section .rodata
start_message:
    .ascii "Vex ELF pipe-exec smoke: start\n"
start_message_len = . - start_message
done_message:
    .ascii "Vex ELF pipe-exec smoke: payload+exit ok\n"
done_message_len = . - done_message
fail_message:
    .ascii "Vex ELF pipe-exec smoke: failed\n"
fail_message_len = . - fail_message
fail_pipe_message:
    .ascii "Vex ELF pipe-exec smoke: failed pipe\n"
fail_pipe_message_len = . - fail_pipe_message
fail_pipe_cloexec_message:
    .ascii "Vex ELF pipe-exec smoke: failed pipe cloexec\n"
fail_pipe_cloexec_message_len = . - fail_pipe_cloexec_message
fail_pipe_dup_message:
    .ascii "Vex ELF pipe-exec smoke: failed F_DUPFD_CLOEXEC\n"
fail_pipe_dup_message_len = . - fail_pipe_dup_message
fail_fork_message:
    .ascii "Vex ELF pipe-exec smoke: failed fork\n"
fail_fork_message_len = . - fail_fork_message
fail_read_len_message:
    .ascii "Vex ELF pipe-exec smoke: failed read-len\n"
fail_read_len_message_len = . - fail_read_len_message
fail_payload_message:
    .ascii "Vex ELF pipe-exec smoke: failed payload\n"
fail_payload_message_len = . - fail_payload_message
fail_wait_pid_message:
    .ascii "Vex ELF pipe-exec smoke: failed wait-pid\n"
fail_wait_pid_message_len = . - fail_wait_pid_message
fail_wait_status_message:
    .ascii "Vex ELF pipe-exec smoke: failed wait-status\n"
fail_wait_status_message_len = . - fail_wait_status_message
fail_exec_missing_wait_message:
    .ascii "Vex ELF pipe-exec smoke: failed missing-exec wait\n"
fail_exec_missing_wait_message_len = . - fail_exec_missing_wait_message
fail_exec_missing_status_message:
    .ascii "Vex ELF pipe-exec smoke: failed missing-exec status\n"
fail_exec_missing_status_message_len = . - fail_exec_missing_status_message
fail_exec_refused_wait_message:
    .ascii "Vex ELF pipe-exec smoke: failed refused-exec wait\n"
fail_exec_refused_wait_message_len = . - fail_exec_refused_wait_message
fail_exec_refused_status_message:
    .ascii "Vex ELF pipe-exec smoke: failed refused-exec status\n"
fail_exec_refused_status_message_len = . - fail_exec_refused_status_message
child_dup2_message:
    .ascii "Vex ELF pipe-exec smoke: child failed dup2\n"
child_dup2_message_len = . - child_dup2_message
fail_open_dummy_message:
    .ascii "Vex ELF pipe-exec smoke: failed open-dummy\n"
fail_open_dummy_message_len = . - fail_open_dummy_message
child_path:
    .asciz "/usr/linux-tests/linux_exec_child"
dummy0_path:
    .asciz "/home/root/pipe_exec_dummy0"
dummy1_path:
    .asciz "/home/root/pipe_exec_dummy1"
dummy2_path:
    .asciz "/home/root/pipe_exec_dummy2"
missing_path:
    .asciz "/home/root/pipe_exec_missing"
expected_payload:
    .ascii "VEXEXECOK"
expected_payload_len = . - expected_payload
