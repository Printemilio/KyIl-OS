.section .text
.global _start
_start:
    xor %rax, %rax
fault_unhandled:
    movq (%rax), %rdi
    mov $60, %eax
    xor %edi, %edi
    syscall
    ud2
