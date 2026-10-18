.global _start

# Lot 4 filesystem/pipe smoke:
# - persist/read back a file larger than the old 32 KiB limit
# - overwrite it repeatedly and preserve an empty file
# - close the last writer and require a real pipe EOF

.set SYS_READ, 0
.set SYS_WRITE, 1
.set SYS_OPEN, 2
.set SYS_CLOSE, 3
.set SYS_FSYNC, 74
.set SYS_PIPE, 22
.set SYS_FORK, 57
.set SYS_WAIT4, 61
.set SYS_EXIT_GROUP, 231

.set O_RDONLY, 0
.set O_RDWR_CREAT_TRUNC, 0x242

.section .text
_start:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea start_message(%rip), %rsi
    mov $start_message_len, %rdx
    syscall

    # Regression for ISO9660 Rock Ridge long-name lookup: the WPE bundle is
    # staged below a hyphenated directory name and must be reachable through
    # the VexFS ISO reader, not only through the host-side staging tree.
    mov $SYS_OPEN, %rax
    lea injected_bundle_path(%rip), %rdi
    mov $O_RDONLY, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall

    # Write 40 KiB, fsync, close, reopen and read it back.
    mov $SYS_OPEN, %rax
    lea large_path(%rip), %rdi
    mov $O_RDWR_CREAT_TRUNC, %rsi
    mov $0644, %rdx
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $SYS_WRITE, %rax
    mov %r12, %rdi
    lea large_payload(%rip), %rsi
    mov $large_payload_len, %rdx
    syscall
    cmp $large_payload_len, %rax
    jne fail_close_large
    mov $SYS_FSYNC, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    js fail_close_large
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall

    # Repeated truncating overwrites must reuse the logical extent without
    # making the file disappear or exhausting the allocator.
    mov $2, %r14
overwrite:
    mov $SYS_OPEN, %rax
    lea large_path(%rip), %rdi
    mov $O_RDWR_CREAT_TRUNC, %rsi
    mov $0644, %rdx
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $SYS_WRITE, %rax
    mov %r12, %rdi
    lea large_payload(%rip), %rsi
    mov $large_payload_len, %rdx
    syscall
    cmp $large_payload_len, %rax
    jne fail_close_large
    mov $SYS_FSYNC, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    js fail_close_large
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall
    dec %r14
    jnz overwrite

    mov $SYS_OPEN, %rax
    lea large_path(%rip), %rdi
    mov $O_RDONLY, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $SYS_READ, %rax
    mov %r12, %rdi
    lea readback(%rip), %rsi
    mov $large_payload_len, %rdx
    syscall
    cmp $large_payload_len, %rax
    jne fail_close_large
    mov readback(%rip), %eax
    cmp $0x5a5a5a5a, %eax
    jne fail_close_large
    mov readback + large_payload_len - 4(%rip), %eax
    cmp $0x5a5a5a5a, %eax
    jne fail_close_large
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall

    # O_TRUNC followed by close must leave an empty file, not delete it.
    mov $SYS_OPEN, %rax
    lea empty_path(%rip), %rdi
    mov $O_RDWR_CREAT_TRUNC, %rsi
    mov $0644, %rdx
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $SYS_FSYNC, %rax
    mov %r12, %rdi
    syscall
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall
    mov $SYS_OPEN, %rax
    lea empty_path(%rip), %rdi
    mov $O_RDONLY, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $SYS_READ, %rax
    mov %r12, %rdi
    lea readback(%rip), %rsi
    mov $1, %rdx
    syscall
    test %rax, %rax
    jne fail_close_large
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall

    # Parent closes its writer; child writes one byte then exits.  The second
    # read must return zero after the child closes its writer endpoint.
    mov $SYS_PIPE, %rax
    lea pipe_fds(%rip), %rdi
    syscall
    test %rax, %rax
    js fail
    mov $SYS_FORK, %rax
    syscall
    cmp $0, %rax
    je child
    js fail
    mov %rax, %r13
    mov $SYS_CLOSE, %rax
    mov pipe_fds + 4(%rip), %rdi
    syscall
    mov $SYS_READ, %rax
    mov pipe_fds(%rip), %edi
    lea pipe_byte(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $1, %rax
    jne fail
    mov $SYS_READ, %rax
    mov pipe_fds(%rip), %edi
    lea pipe_byte(%rip), %rsi
    mov $1, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $SYS_WAIT4, %rax
    mov %r13, %rdi
    lea status(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall
    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall

child:
    mov $SYS_CLOSE, %rax
    mov pipe_fds(%rip), %rdi
    syscall
    mov $SYS_WRITE, %rax
    mov pipe_fds + 4(%rip), %edi
    lea pipe_byte(%rip), %rsi
    mov $1, %rdx
    syscall
    mov $SYS_CLOSE, %rax
    mov pipe_fds + 4(%rip), %rdi
    syscall
    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall

fail_close_large:
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
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
.align 8
pipe_fds:
    .long 0
    .long 0
status:
    .long 0
pipe_byte:
    .byte 0
readback:
    .zero large_payload_len

.section .rodata
start_message:
    .ascii "Vex ELF fs-ipc smoke: start\n"
start_message_len = . - start_message
done_message:
    .ascii "Vex ELF fs-ipc smoke: done\n"
done_message_len = . - done_message
fail_message:
    .ascii "Vex ELF fs-ipc smoke: failed\n"
fail_message_len = . - fail_message
large_path:
    .asciz "/home/root/fs_ipc_large.bin"
empty_path:
    .asciz "/home/root/fs_ipc_empty.bin"
injected_bundle_path:
    .asciz "/usr/local/lib/wpe-webkit-2.0/injected-bundle/libWPEInjectedBundle.so"
large_payload:
    .rept 40960
    .byte 0x5a
    .endr
large_payload_len = . - large_payload
