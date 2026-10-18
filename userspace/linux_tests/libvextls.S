.section .tdata,"awT",@progbits
.globl vex_tls_value
.type vex_tls_value,@object
vex_tls_value:
    .quad 42
.size vex_tls_value, .-vex_tls_value
.text
.globl vex_tls_get
.type vex_tls_get,@function
vex_tls_get:
    mov vex_tls_value@GOTTPOFF(%rip), %rax
    mov %fs:(%rax), %eax
    ret
.size vex_tls_get, .-vex_tls_get
