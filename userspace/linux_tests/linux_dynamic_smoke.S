.text
.globl _start
.type _start,@function
.extern vex_dyn_value
.extern vex_dyn_global
_start:
    mov vex_dyn_global@GOTPCREL(%rip), %rax
    mov (%rax), %eax
    cmp $7, %eax
    jne .Lfail
    call vex_dyn_value@PLT
    cmp $42, %eax
    jne .Lfail
    mov $1, %eax
    mov $1, %edi
    lea .Lok_msg(%rip), %rsi
    mov $38, %edx
    syscall
    xor %edi, %edi
    mov $60, %eax
    syscall
.Lfail:
    mov $1, %eax
    mov $1, %edi
    lea .Lfail_msg(%rip), %rsi
    mov $41, %edx
    syscall
    mov $1, %edi
    mov $60, %eax
    syscall
.size _start, .-_start
.section .rodata
.Lok_msg:
    .ascii "Vex ELF dynamic GOT/PLT smoke: done\n"
.Lfail_msg:
    .ascii "Vex ELF dynamic GOT/PLT smoke: failed\n"
