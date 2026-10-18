.text
.globl _start
.type _start,@function
.extern vex_tls_get
_start:
    call vex_tls_get@PLT
    cmp $42, %eax
    jne .Lfail
    mov $1, %eax
    mov $1, %edi
    lea .Lok_msg(%rip), %rsi
    mov $32, %edx
    syscall
    xor %edi, %edi
    mov $60, %eax
    syscall
.Lfail:
    mov $1, %eax
    mov $1, %edi
    lea .Lfail_msg(%rip), %rsi
    mov $34, %edx
    syscall
    mov $1, %edi
    mov $60, %eax
    syscall
.size _start, .-_start
.section .rodata
.Lok_msg:
    .ascii "Vex ELF dynamic TLS smoke: done\n"
.Lfail_msg:
    .ascii "Vex ELF dynamic TLS smoke: failed\n"
