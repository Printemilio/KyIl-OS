.global _start

# wait4 smoke:
# - fork a real child process
# - child exits with 42
# - parent blocks in wait4(child_pid, &status, 0, NULL)
# - parent verifies Linux wait status encoding and reports exit=42

.set SYS_WRITE, 1
.set SYS_FORK, 57
.set SYS_EXIT, 60
.set SYS_WAIT4, 61
.set SYS_EXIT_GROUP, 231

.section .text
_start:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea start_message(%rip), %rsi
    mov $start_message_len, %rdx
    syscall

    mov $SYS_FORK, %rax
    syscall
    cmp $0, %rax
    je child
    js fail

parent:
    mov %rax, %r12
    mov $SYS_WAIT4, %rax
    mov %r12, %rdi
    lea status_word(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp %r12, %rax
    jne fail

    mov status_word(%rip), %eax
    mov %eax, %ebx
    and $0x7f, %ebx
    jne fail
    shr $8, %eax
    and $0xff, %eax
    cmp $42, %eax
    jne fail

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall

    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall

child:
    mov $7, %rax
    imul $6, %rax
    cmp $42, %rax
    jne child_fail

    mov $SYS_EXIT, %rax
    mov $42, %rdi
    syscall

child_fail:
    mov $SYS_EXIT, %rax
    mov $111, %rdi
    syscall

fail:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_message(%rip), %rsi
    mov $fail_message_len, %rdx
    syscall

    mov $SYS_EXIT_GROUP, %rax
    mov $118, %rdi
    syscall

.section .bss
.align 4
status_word:
    .long 0

.section .rodata
start_message:
    .ascii "Vex ELF wait4 smoke: start\n"
start_message_len = . - start_message
done_message:
    .ascii "Vex ELF wait4 smoke: exit=42\n"
done_message_len = . - done_message
fail_message:
    .ascii "Vex ELF wait4 smoke: failed\n"
fail_message_len = . - fail_message
