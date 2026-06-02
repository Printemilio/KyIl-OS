.global _start
.section .text
_start:
    mov $2, %rax
    lea path(%rip), %rdi
    mov $0x242, %rsi
    mov $0644, %rdx
    syscall
    cmp $0, %rax
    jl fail

    mov %rax, %r12

    mov $1, %rax
    mov %r12, %rdi
    lea message(%rip), %rsi
    mov $message_len, %rdx
    syscall
    cmp $0, %rax
    jl close_and_fail

    mov $8, %rax
    mov %r12, %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    cmp $0, %rax
    jl close_and_fail

    mov $0, %rax
    mov %r12, %rdi
    lea buffer(%rip), %rsi
    mov $63, %rdx
    syscall
    cmp $0, %rax
    jle close_and_fail

    mov %rax, %r13

    mov $5, %rax
    mov %r12, %rdi
    lea statbuf(%rip), %rsi
    syscall
    cmp $0, %rax
    jl close_and_fail

    mov $1, %rax
    mov $1, %rdi
    lea buffer(%rip), %rsi
    mov %r13, %rdx
    syscall

    mov $3, %rax
    mov %r12, %rdi
    syscall
    mov $60, %rax
    xor %rdi, %rdi
    syscall

close_and_fail:
    mov $3, %rax
    mov %r12, %rdi
    syscall
fail:
    mov $1, %rax
    mov $1, %rdi
    lea failmsg(%rip), %rsi
    mov $linux_fail_len, %rdx
    syscall
    mov $60, %rax
    mov $1, %rdi
    syscall

.section .rodata
path:
    .asciz "/home/root/linux_file_test.txt"
message:
    .ascii "linux file abi works\n"
message_len = . - message
failmsg:
    .ascii "linux file abi failed\n"
linux_fail_len = . - failmsg

.section .bss
.balign 8
buffer:
    .skip 64
statbuf:
    .skip 160
