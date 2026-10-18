.text
.data
.globl vex_dyn_global
.type vex_dyn_global,@object
vex_dyn_global:
    .quad 7
.size vex_dyn_global, .-vex_dyn_global
.text
.globl vex_dyn_ctor
.type vex_dyn_ctor,@function
vex_dyn_ctor:
    mov vex_dyn_global@GOTPCREL(%rip), %rax
    movq $99, (%rax)
    ret
.size vex_dyn_ctor, .-vex_dyn_ctor
.section .init_array,"aw",@init_array
    .quad vex_dyn_ctor
.text
.globl vex_dyn_value
.type vex_dyn_value,@function
vex_dyn_value:
    mov $42, %eax
    ret
.size vex_dyn_value, .-vex_dyn_value
