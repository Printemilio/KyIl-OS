.global _start

.set SYS_READ, 0
.set SYS_WRITE, 1
.set SYS_CLOSE, 3
.set SYS_SOCKETPAIR, 53
.set SYS_GETSOCKNAME, 51
.set SYS_GETPEERNAME, 52
.set SYS_GETSOCKOPT, 55
.set SYS_FCNTL, 72
.set SYS_FORK, 57
.set SYS_WAIT4, 61
.set SYS_EXIT, 60
.set SYS_EXIT_GROUP, 231
.set AF_UNIX, 1
.set SOCK_SEQPACKET, 5
.set SOCK_NONBLOCK, 0x800
.set SOCK_CLOEXEC, 0x80000
.set F_GETFD, 1
.set F_SETFD, 2
.set FD_CLOEXEC, 1
.set SOL_SOCKET, 1
.set SO_TYPE, 3
.set SO_ERROR, 4

.section .text
_start:
    mov $SYS_SOCKETPAIR, %rax
    mov $AF_UNIX, %rdi
    mov $(SOCK_SEQPACKET | SOCK_CLOEXEC), %rsi
    xor %rdx, %rdx
    lea pair(%rip), %r10
    syscall
    test %rax, %rax
    js fail

    # An unnamed Vex message endpoint is exposed as AF_UNIX with only the
    # family field; no synthetic filesystem pathname is returned.
    mov $SYS_GETSOCKNAME, %rax
    mov pair(%rip), %rdi
    lea socket_name(%rip), %rsi
    mov $16, %edx
    movl %edx, socket_name_len(%rip)
    lea socket_name_len(%rip), %rdx
    syscall
    test %rax, %rax
    js fail
    cmpl $2, socket_name_len(%rip)
    jne fail
    cmpw $AF_UNIX, socket_name(%rip)
    jne fail

    # Socket options must use the message-endpoint contract without treating
    # its opaque context as a network socket structure.
    mov $SYS_GETSOCKOPT, %rax
    mov pair(%rip), %rdi
    mov $SOL_SOCKET, %rsi
    mov $SO_TYPE, %rdx
    lea socket_option(%rip), %r10
    movl $4, socket_option_len(%rip)
    lea socket_option_len(%rip), %r8
    syscall
    test %rax, %rax
    js fail
    cmpl $SOCK_SEQPACKET, socket_option(%rip)
    jne fail
    cmpl $4, socket_option_len(%rip)
    jne fail

    mov $SYS_GETSOCKOPT, %rax
    mov pair(%rip), %rdi
    mov $SOL_SOCKET, %rsi
    mov $SO_ERROR, %rdx
    lea socket_option(%rip), %r10
    movl $4, socket_option_len(%rip)
    lea socket_option_len(%rip), %r8
    syscall
    test %rax, %rax
    js fail
    cmpl $0, socket_option(%rip)
    jne fail

    # A short Linux sockaddr buffer is truncated, while *addrlen reports the
    # complete unnamed address length.
    mov $SYS_GETSOCKNAME, %rax
    mov pair(%rip), %rdi
    lea socket_name(%rip), %rsi
    mov $1, %edx
    mov %edx, socket_name_len(%rip)
    lea socket_name_len(%rip), %rdx
    syscall
    test %rax, %rax
    js fail
    cmpl $2, socket_name_len(%rip)
    jne fail
    cmpb $AF_UNIX, socket_name(%rip)
    jne fail

    # The peer of the unnamed local endpoint has the same compatibility
    # identity and still has no synthetic pathname.
    mov $SYS_GETPEERNAME, %rax
    mov pair+4(%rip), %rdi
    lea socket_name(%rip), %rsi
    movl $16, socket_name_len(%rip)
    lea socket_name_len(%rip), %rdx
    syscall
    test %rax, %rax
    js fail
    cmpl $2, socket_name_len(%rip)
    jne fail
    cmpw $AF_UNIX, socket_name(%rip)
    jne fail

    mov $SYS_GETPEERNAME, %rax
    mov pair+4(%rip), %rdi
    lea socket_name(%rip), %rsi
    movl $1, socket_name_len(%rip)
    lea socket_name_len(%rip), %rdx
    syscall
    test %rax, %rax
    js fail
    cmpl $2, socket_name_len(%rip)
    jne fail
    cmpb $AF_UNIX, socket_name(%rip)
    jne fail

    # Invalid descriptors and invalid userspace output pointers are errors.
    mov $SYS_GETSOCKNAME, %rax
    mov $999, %rdi
    lea socket_name(%rip), %rsi
    movl $16, socket_name_len(%rip)
    lea socket_name_len(%rip), %rdx
    syscall
    cmp $-9, %rax
    jne fail
    mov $SYS_GETSOCKNAME, %rax
    mov pair(%rip), %rdi
    mov $0x1000, %rsi
    lea socket_name_len(%rip), %rdx
    syscall
    cmp $-14, %rax
    jne fail
    mov $SYS_GETSOCKNAME, %rax
    mov pair(%rip), %rdi
    lea socket_name(%rip), %rsi
    movl $16, socket_name_len(%rip)
    mov $0x1000, %rdx
    syscall
    cmp $-14, %rax
    jne fail
    mov $SYS_GETPEERNAME, %rax
    mov $999, %rdi
    lea socket_name(%rip), %rsi
    movl $16, socket_name_len(%rip)
    lea socket_name_len(%rip), %rdx
    syscall
    cmp $-9, %rax
    jne fail
    mov $SYS_GETPEERNAME, %rax
    mov pair+4(%rip), %rdi
    mov $0x1000, %rsi
    lea socket_name_len(%rip), %rdx
    syscall
    cmp $-14, %rax
    jne fail

    # One write is one message; a second write must not merge with it.
    mov $SYS_WRITE, %rax
    mov pair(%rip), %rdi
    lea first_message(%rip), %rsi
    mov $3, %rdx
    syscall
    cmp $3, %rax
    jne fail
    mov $SYS_WRITE, %rax
    mov pair(%rip), %rdi
    lea second_message(%rip), %rsi
    mov $6, %rdx
    syscall
    cmp $6, %rax
    jne fail
    mov $SYS_READ, %rax
    mov pair+4(%rip), %rdi
    lea buffer(%rip), %rsi
    mov $32, %rdx
    syscall
    cmp $3, %rax
    jne fail
    cmpl $0x00656e6f, buffer(%rip)
    jne fail
    mov $SYS_READ, %rax
    mov pair+4(%rip), %rdi
    lea buffer(%rip), %rsi
    mov $32, %rdx
    syscall
    cmp $6, %rax
    jne fail
    cmpl $0x6f636573, buffer(%rip)
    jne fail

    # Bidirectional delivery uses the opposite queue.
    mov $SYS_WRITE, %rax
    mov pair+4(%rip), %rdi
    lea reply_message(%rip), %rsi
    mov $5, %rdx
    syscall
    cmp $5, %rax
    jne fail
    mov $SYS_READ, %rax
    mov pair(%rip), %rdi
    lea buffer(%rip), %rsi
    mov $32, %rdx
    syscall
    cmp $5, %rax
    jne fail

    # A short receive reports EMSGSIZE and leaves the packet queued.
    mov $SYS_WRITE, %rax
    mov pair(%rip), %rdi
    lea long_message(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $8, %rax
    jne fail
    mov $SYS_READ, %rax
    mov pair+4(%rip), %rdi
    lea buffer(%rip), %rsi
    mov $4, %rdx
    syscall
    cmp $-90, %rax
    jne fail
    mov $SYS_READ, %rax
    mov pair+4(%rip), %rdi
    lea buffer(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $8, %rax
    jne fail

    # CLOEXEC is visible through fcntl and inherited until exec.
    mov $SYS_FCNTL, %rax
    mov pair(%rip), %rdi
    mov $F_GETFD, %rsi
    xor %rdx, %rdx
    syscall
    test $FD_CLOEXEC, %eax
    jz fail
    mov $SYS_FCNTL, %rax
    mov pair(%rip), %rdi
    mov $F_SETFD, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    js fail
    mov $SYS_FCNTL, %rax
    mov pair(%rip), %rdi
    mov $F_GETFD, %rsi
    xor %rdx, %rdx
    syscall
    test $FD_CLOEXEC, %eax
    jnz fail
    mov $SYS_FCNTL, %rax
    mov pair(%rip), %rdi
    mov $F_SETFD, %rsi
    mov $FD_CLOEXEC, %rdx
    syscall
    test %rax, %rax
    js fail
    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    js fail
    jz cloexec_child
    mov %rax, %r12
    mov $SYS_WAIT4, %rax
    mov %r12, %rdi
    lea status(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp %r12, %rax
    jne fail

    # Nonblocking empty receive is EAGAIN; after peer close it is EOF.
    mov $SYS_SOCKETPAIR, %rax
    mov $AF_UNIX, %rdi
    mov $(SOCK_SEQPACKET | SOCK_NONBLOCK), %rsi
    xor %rdx, %rdx
    lea nonblock_pair(%rip), %r10
    syscall
    test %rax, %rax
    js fail
    mov $SYS_READ, %rax
    mov nonblock_pair+4(%rip), %rdi
    lea buffer(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $-11, %rax
    jne fail

    # A nonblocking sender eventually observes bounded-queue backpressure;
    # draining the peer then makes every queued message independently visible.
    xor %r12d, %r12d
fill_send:
    mov $SYS_WRITE, %rax
    mov nonblock_pair(%rip), %rdi
    lea one_byte(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $-11, %rax
    je fill_send_done
    cmp $1, %rax
    jne fail
    inc %r12
    jmp fill_send
fill_send_done:
    test %r12, %r12
    jz fail
    xor %r13d, %r13d
fill_recv:
    mov $SYS_READ, %rax
    mov nonblock_pair+4(%rip), %rdi
    lea buffer(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $-11, %rax
    je fill_recv_done
    cmp $1, %rax
    jne fail
    inc %r13
    jmp fill_recv
fill_recv_done:
    cmp %r12, %r13
    jne fail

    # A blocking receive sleeps in the scheduler and is woken by a child
    # writing the same inherited endpoint.
    mov $SYS_SOCKETPAIR, %rax
    mov $AF_UNIX, %rdi
    mov $SOCK_SEQPACKET, %rsi
    xor %rdx, %rdx
    lea blocking_pair(%rip), %r10
    syscall
    test %rax, %rax
    js fail
    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    js fail
    jz blocking_child
    mov %rax, %r14
    mov $SYS_READ, %rax
    mov blocking_pair+4(%rip), %rdi
    lea buffer(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $1, %rax
    jne fail
    mov $SYS_WAIT4, %rax
    mov %r14, %rdi
    lea status(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp %r14, %rax
    jne fail
    mov $SYS_CLOSE, %rax
    mov blocking_pair(%rip), %rdi
    syscall
    mov $SYS_CLOSE, %rax
    mov blocking_pair+4(%rip), %rdi
    syscall

    mov $SYS_CLOSE, %rax
    mov nonblock_pair(%rip), %rdi
    syscall
    test %rax, %rax
    js fail
    mov $SYS_READ, %rax
    mov nonblock_pair+4(%rip), %rdi
    lea buffer(%rip), %rsi
    mov $8, %rdx
    syscall
    test %rax, %rax
    jne fail

    mov $SYS_CLOSE, %rax
    mov pair(%rip), %rdi
    syscall
    mov $SYS_CLOSE, %rax
    mov pair+4(%rip), %rdi
    syscall
    mov $SYS_CLOSE, %rax
    mov nonblock_pair+4(%rip), %rdi
    syscall

    # Invalid descriptor handles are rejected and repeated pair lifetime does
    # not leak an endpoint or its channel backing.
    mov $SYS_READ, %rax
    mov $999, %rdi
    lea buffer(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $-9, %rax
    jne fail
    mov $SYS_CLOSE, %rax
    mov $999, %rdi
    syscall
    cmp $-9, %rax
    jne fail
    mov $4, %r15d
pair_cycle:
    mov $SYS_SOCKETPAIR, %rax
    mov $AF_UNIX, %rdi
    mov $SOCK_SEQPACKET, %rsi
    xor %rdx, %rdx
    lea pair(%rip), %r10
    syscall
    test %rax, %rax
    js fail
    mov $SYS_CLOSE, %rax
    mov pair(%rip), %rdi
    syscall
    mov $SYS_CLOSE, %rax
    mov pair+4(%rip), %rdi
    syscall
    dec %r15d
    jnz pair_cycle

    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall
    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall

blocking_child:
    mov $SYS_WRITE, %rax
    mov blocking_pair(%rip), %rdi
    lea one_byte(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $1, %rax
    jne fail
    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall

cloexec_child:
    mov $SYS_FCNTL, %rax
    mov pair(%rip), %rdi
    mov $F_GETFD, %rsi
    xor %rdx, %rdx
    syscall
    test $FD_CLOEXEC, %eax
    jz fail
    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall

fail:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_message(%rip), %rsi
    mov $fail_message_len, %rdx
    syscall
    mov $SYS_EXIT_GROUP, %rax
    mov $127, %rdi
    syscall

.section .bss
.align 4
pair:
    .long 0, 0
nonblock_pair:
    .long 0, 0
blocking_pair:
    .long 0, 0
status:
    .long 0
buffer:
    .space 64
socket_name:
    .space 16
socket_name_len:
    .long 0
socket_option:
    .long 0
socket_option_len:
    .long 0

.section .rodata
one_byte:
    .byte 0x5a
first_message:
    .ascii "one"
second_message:
    .ascii "second"
reply_message:
    .ascii "reply"
long_message:
    .ascii "long-msg"
done_message:
    .ascii "Vex ELF seqpacket smoke: done\n"
done_message_len = . - done_message
fail_message:
    .ascii "Vex ELF seqpacket smoke: failed\n"
fail_message_len = . - fail_message
