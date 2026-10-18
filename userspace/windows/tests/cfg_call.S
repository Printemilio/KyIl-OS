.section .rdata,"dr"
.global cfg_function_rvas
cfg_function_rvas:
    .rva valid_target
.text
.global cfg_invoke
cfg_invoke:
    sub $56,%rsp
    mov $1,%ecx
    mov $2,%edx
    mov $3,%r8d
    mov $4,%r9d
    movq $5,32(%rsp)
    movq $6,40(%rsp)
    lea valid_target(%rip),%rax
#ifdef CFG_BAD
    inc %rax
#endif
    call *guard_dispatch(%rip)
    add $56,%rsp
    ret
