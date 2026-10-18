.section .text
.global _start
_start:
    /* A self-directed Linux SIGABRT default action terminates the complete
     * process group, even though tgkill names one particular thread. */
    mov $39, %eax                 # getpid
    syscall
    mov %rax, %r12
    mov $186, %eax                # gettid
    syscall
    mov %rax, %rsi
    mov %r12, %rdi                # tgid
    mov $6, %edx                  # SIGABRT
    mov $234, %eax                # tgkill
    syscall

    /* Reaching this point means the fatal signal incorrectly returned. */
    mov $60, %eax
    mov $1, %edi
    syscall
    ud2
