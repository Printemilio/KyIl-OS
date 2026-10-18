.global _start

.section .text
_start:
    mov $1, %rax
    mov $1, %rdi
    lea start_message(%rip), %rsi
    mov $start_message_len, %rdx
    syscall

    mov $12, %rax
    xor %rdi, %rdi
    syscall
    test %rax, %rax
    jz fail

    mov %rax, %rbx
    mov %rax, %rdi
    add $4096, %rdi
    mov $12, %rax
    syscall
    cmp %rbx, %rax
    jbe fail

    mov $158, %rax
    mov $0x1002, %rdi
    xor %rsi, %rsi
    syscall
    test %rax, %rax
    js fail

    mov $9, %rax
    xor %rdi, %rdi
    mov $4096, %rsi
    mov $3, %rdx
    mov $0x22, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae fail

    movb $'O', (%rax)
    movb $'K', 1(%rax)

    mov $1, %rax
    mov $1, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall

    mov $231, %rax
    mov $7, %rdi
    syscall

fail:
    mov $1, %rax
    mov $2, %rdi
    lea fail_message(%rip), %rsi
    mov $fail_message_len, %rdx
    syscall

    mov $231, %rax
    mov $111, %rdi
    syscall

.section .rodata
start_message:
    .ascii "Vex ELF smoke: start\n"
start_message_len = . - start_message
done_message:
    .ascii "Vex ELF smoke: done\n"
done_message_len = . - done_message
fail_message:
    .ascii "Vex ELF smoke: failed\n"
fail_message_len = . - fail_message
