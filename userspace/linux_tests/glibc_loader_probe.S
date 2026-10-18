.section .text
.global _start
.type _start,@function
_start:
    mov $1, %eax
    mov $1, %edi
    lea message(%rip), %rsi
    mov $36, %edx
    syscall
    mov $60, %eax
    xor %edi, %edi
    syscall
    hlt

.section .rodata
message:
    .asciz "Vex glibc loader probe: reached entry"
