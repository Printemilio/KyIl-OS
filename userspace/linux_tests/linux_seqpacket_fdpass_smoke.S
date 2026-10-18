.global _start

# Capability-transfer smoke.  The child is created first, then the parent
# sends one SEQPACKET endpoint through SCM_RIGHTS.  The child closes its
# inherited data endpoints and can communicate only through the newly
# installed local descriptor.
.set SYS_READ, 0
.set SYS_WRITE, 1
.set SYS_CLOSE, 3
.set SYS_SOCKETPAIR, 53
.set SYS_SENDMSG, 46
.set SYS_RECVMSG, 47
.set SYS_FORK, 57
.set SYS_EXIT, 60
.set SYS_EXIT_GROUP, 231
.set AF_UNIX, 1
.set SOCK_SEQPACKET, 5
.set SOL_SOCKET, 1
.set SCM_RIGHTS, 1

.section .text
_start:
    mov $SYS_SOCKETPAIR, %rax
    mov $AF_UNIX, %rdi
    mov $SOCK_SEQPACKET, %rsi
    xor %rdx, %rdx
    lea control_pair(%rip), %r10
    syscall
    test %rax, %rax
    js fail

    mov $SYS_SOCKETPAIR, %rax
    mov $AF_UNIX, %rdi
    mov $SOCK_SEQPACKET, %rsi
    xor %rdx, %rdx
    lea data_pair(%rip), %r10
    syscall
    test %rax, %rax
    js fail

    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    jz child
    js fail
    mov %rax, %r12

parent:
    # Keep control[0] and data[0]; data[1] is the capability being sent.
    mov $SYS_CLOSE, %rax
    mov control_pair+4(%rip), %rdi
    syscall

    mov data_pair+4(%rip), %eax
    mov %eax, send_control+16(%rip)
    mov $SYS_SENDMSG, %rax
    mov control_pair(%rip), %rdi
    lea send_message(%rip), %rsi
    xor %rdx, %rdx
    syscall
    cmp $3, %rax
    jne fail_send

    # The queued message owns the object reference independently of this FD.
    mov $SYS_CLOSE, %rax
    mov data_pair+4(%rip), %rdi
    syscall

    mov $SYS_WRITE, %rax
    mov data_pair(%rip), %rdi
    lea ping(%rip), %rsi
    mov $4, %rdx
    syscall
    cmp $4, %rax
    jne fail_parent_write

    mov $SYS_READ, %rax
    mov control_pair(%rip), %rdi
    lea reply(%rip), %rsi
    mov $2, %rdx
    syscall
    cmp $2, %rax
    jne fail_parent_reply
    cmpl $0x00006b6f, reply(%rip)
    jne fail

parent_success:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall
    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall

child:
    # Make the transferred descriptor the only data endpoint in this process.
    mov $SYS_CLOSE, %rax
    mov control_pair(%rip), %rdi
    syscall
    mov $SYS_CLOSE, %rax
    mov data_pair(%rip), %rdi
    syscall
    mov $SYS_CLOSE, %rax
    mov data_pair+4(%rip), %rdi
    syscall

    mov $SYS_RECVMSG, %rax
    mov control_pair+4(%rip), %rdi
    lea receive_message(%rip), %rsi
    xor %rdx, %rdx
    syscall
    cmp $3, %rax
    jne child_fail_recv

    mov receive_control+16(%rip), %eax
    mov %eax, received_fd(%rip)
    mov received_fd(%rip), %edi
    mov $SYS_READ, %rax
    lea received_data(%rip), %rsi
    mov $4, %rdx
    syscall
    cmp $4, %rax
    jne child_fail_read
    cmpl $0x676e6970, received_data(%rip)
    jne child_fail

    mov $SYS_WRITE, %rax
    mov control_pair+4(%rip), %rdi
    lea ok(%rip), %rsi
    mov $2, %rdx
    syscall
    cmp $2, %rax
    jne child_fail_reply
    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall

child_fail:
    mov $SYS_EXIT, %rax
    mov $111, %rdi
    syscall

fail_send:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_send_message(%rip), %rsi
    mov $fail_send_message_len, %rdx
    syscall
    jmp fail
fail_parent_write:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_parent_write_message(%rip), %rsi
    mov $fail_parent_write_message_len, %rdx
    syscall
    jmp fail
fail_parent_reply:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_parent_reply_message(%rip), %rsi
    mov $fail_parent_reply_message_len, %rdx
    syscall
    jmp fail
child_fail_recv:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_recv_message(%rip), %rsi
    mov $fail_recv_message_len, %rdx
    syscall
    jmp child_fail
child_fail_read:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_read_message(%rip), %rsi
    mov $fail_read_message_len, %rdx
    syscall
    jmp child_fail
child_fail_reply:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_reply_message(%rip), %rsi
    mov $fail_reply_message_len, %rdx
    syscall
    jmp child_fail

fail:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_message(%rip), %rsi
    mov $fail_message_len, %rdx
    syscall
    mov $SYS_EXIT_GROUP, %rax
    mov $119, %rdi
    syscall

.section .bss
.align 8
control_pair: .space 8
data_pair: .space 8
reply: .space 8
received_data: .space 8
received_fd: .space 4

.section .rodata
.align 8
send_message:
    .quad 0
    .long 0
    .long 0
    .quad send_iov
    .quad 1
    .quad send_control
    .quad 24
    .long 0
    .long 0
send_iov:
    .quad send_payload
    .quad 3
send_payload:
    .ascii "cap"
.align 8
.section .data
send_control:
    .quad 20
    .long SOL_SOCKET
    .long SCM_RIGHTS
    .long 0
    .long 0
send_message_end:

.section .data
.align 8
receive_message:
    .quad 0
    .long 0
    .long 0
    .quad receive_iov
    .quad 1
    .quad receive_control
    .quad 24
    .long 0
    .long 0
.section .data
receive_iov:
    .quad receive_payload
    .quad 3
receive_payload:
    .space 8
.align 8
receive_control:
    .space 24
receive_message_end:

.section .rodata
ping: .ascii "ping"
ok: .ascii "ok"
done_message: .ascii "Vex ELF SCM_RIGHTS smoke: ok\n"
done_message_len = . - done_message
fail_message: .ascii "Vex ELF SCM_RIGHTS smoke: failed\n"
fail_message_len = . - fail_message
fail_send_message: .ascii "SCM fail: sendmsg\n"
fail_send_message_len = . - fail_send_message
fail_parent_write_message: .ascii "SCM fail: parent write\n"
fail_parent_write_message_len = . - fail_parent_write_message
fail_parent_reply_message: .ascii "SCM fail: parent reply\n"
fail_parent_reply_message_len = . - fail_parent_reply_message
fail_recv_message: .ascii "SCM fail: recvmsg\n"
fail_recv_message_len = . - fail_recv_message
fail_read_message: .ascii "SCM fail: child read\n"
fail_read_message_len = . - fail_read_message
fail_reply_message: .ascii "SCM fail: child reply\n"
fail_reply_message_len = . - fail_reply_message
