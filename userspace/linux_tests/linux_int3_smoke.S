.global _start

.section .text
_start:
    mov $1, %rax
    mov $1, %rdi
    lea before_message(%rip), %rsi
    mov $before_message_len, %rdx
    syscall

    /* A CPL3 software breakpoint must enter vector 3.  The default VexOS
       disposition terminates this smoke process; returning here would mean
       that the gate was not delivered or the exception was swallowed. */
    int3

    mov $1, %rax
    mov $2, %rdi
    lea returned_message(%rip), %rsi
    mov $returned_message_len, %rdx
    syscall
    mov $231, %rax
    mov $111, %rdi
    syscall

.section .rodata
before_message:
    .ascii "Vex INT3 smoke: before\n"
before_message_len = . - before_message
returned_message:
    .ascii "Vex INT3 smoke: returned unexpectedly\n"
returned_message_len = . - returned_message
