.global _start

_start:
    mov $1, %rax
    mov $1, %rdi
    lea message(%rip), %rsi
    mov $message_len, %rdx
    syscall

    mov $60, %rax
    xor %rdi, %rdi
    syscall

message:
    .ascii "hello from linux elf\n"
message_len = . - message
