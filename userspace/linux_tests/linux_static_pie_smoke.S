.global _start

.section .text
_start:
    mov $1, %rax
    mov $1, %rdi
    lea start_message(%rip), %rsi
    mov $start_message_len, %rdx
    syscall

    mov done_message_ptr(%rip), %rsi
    lea done_message(%rip), %rbx
    cmp %rbx, %rsi
    jne fail_reloc

    mov $9, %rax
    mov $0x4000000000, %rdi
    mov $4096, %rsi
    mov $3, %rdx
    mov $0x22, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    mov $0x4000000000, %rbx
    cmp %rbx, %rax
    jne fail

    movb $'P', (%rax)

    mov $10, %rax
    mov $0x4000000000, %rdi
    mov $4096, %rsi
    mov $1, %rdx
    syscall
    test %rax, %rax
    js fail

    mov $1, %rax
    mov $1, %rdi
    mov done_message_ptr(%rip), %rsi
    mov $done_message_len, %rdx
    syscall

    mov $231, %rax
    mov $9, %rdi
    syscall

fail:
    mov $1, %rax
    mov $2, %rdi
    lea fail_message(%rip), %rsi
    mov $fail_message_len, %rdx
    syscall

    mov $231, %rax
    mov $112, %rdi
    syscall

fail_reloc:
    mov $1, %rax
    mov $2, %rdi
    lea fail_reloc_message(%rip), %rsi
    mov $fail_reloc_message_len, %rdx
    syscall

    mov $231, %rax
    mov $113, %rdi
    syscall

.section .data
.balign 8
done_message_ptr:
    .quad done_message

.section .rodata
start_message:
    .ascii "Vex ELF static-pie smoke: start\n"
start_message_len = . - start_message
done_message:
    .ascii "Vex ELF static-pie smoke: done\n"
done_message_len = . - done_message
fail_message:
    .ascii "Vex ELF static-pie smoke: failed\n"
fail_message_len = . - fail_message
fail_reloc_message:
    .ascii "Vex ELF static-pie smoke: reloc failed\n"
fail_reloc_message_len = . - fail_reloc_message
