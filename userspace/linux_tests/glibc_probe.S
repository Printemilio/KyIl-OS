.section .text
.extern __libc_start_main
.extern write
.extern malloc
.extern realloc
.extern free
.extern getenv
.extern clock_gettime
.extern getrandom
.extern open
.extern read
.extern close
.extern pthread_key_create
.extern pthread_setspecific
.extern pthread_getspecific
.extern pthread_create
.extern pthread_join
.extern sqrt
.extern dlopen
.extern dlsym
.extern dlclose

.global _start
.type _start,@function
_start:
    mov %rdx, %r9
    mov (%rsp), %esi
    lea 8(%rsp), %rdx
    mov %rsp, %r12
    lea main(%rip), %rdi
    xor %ecx, %ecx
    xor %r8d, %r8d
    and $-16, %rsp
    sub $8, %rsp
    push %r12
    call __libc_start_main@PLT
    hlt

.type main,@function
main:
    push %rbp
    mov %rsp, %rbp
    push %r12
    push %r13

    mov $96, %edi
    call malloc@PLT
    test %rax, %rax
    jz .fail
    mov %rax, %r12
    mov $192, %esi
    mov %r12, %rdi
    call realloc@PLT
    test %rax, %rax
    jz .fail_free
    mov %rax, %r12
    mov %r12, %rdi
    call free@PLT
    lea env_name(%rip), %rdi
    call getenv@PLT
    mov $1, %edi
    lea clock_value(%rip), %rsi
    call clock_gettime@PLT
    lea random_value(%rip), %rdi
    mov $16, %esi
    xor %edx, %edx
    call getrandom@PLT

    lea file_name(%rip), %rdi
    xor %esi, %esi
    xor %edx, %edx
    call open@PLT
    test %eax, %eax
    js .after_file
    mov %eax, file_fd(%rip)
    mov %eax, %edi
    lea file_buffer(%rip), %rsi
    mov $32, %edx
    call read@PLT
    mov %eax, %r13d
    mov file_fd(%rip), %edi
    call close@PLT
.after_file:

    lea dyn_name(%rip), %rdi
    mov $1, %esi
    call dlopen@PLT
    test %rax, %rax
    jz .fail
    mov %rax, %r12
    mov %r12, %rdi
    lea dyn_value_name(%rip), %rsi
    call dlsym@PLT
    test %rax, %rax
    jz .fail_dlclose
    call *%rax
    cmp $42, %eax
    jne .fail_dlclose
    mov %r12, %rdi
    lea dyn_global_name(%rip), %rsi
    call dlsym@PLT
    test %rax, %rax
    jz .fail_dlclose
    mov (%rax), %eax
    cmp $99, %eax
    jne .fail_dlclose
    mov %r12, %rdi
    call dlclose@PLT

    lea libm_name(%rip), %rdi
    mov $1, %esi
    call dlopen@PLT
    test %rax, %rax
    jz .fail
    mov %rax, %r12
    mov %r12, %rdi
    lea sqrt_name(%rip), %rsi
    call dlsym@PLT
    test %rax, %rax
    jz .fail_dlclose
    movsd const_two(%rip), %xmm0
    call *%rax
    mov %r12, %rdi
    call dlclose@PLT

    lea tls_key(%rip), %rdi
    xor %esi, %esi
    call pthread_key_create@PLT
    test %eax, %eax
    jne .fail
    mov tls_key(%rip), %edi
    mov $0x3333333333333333, %rsi
    call pthread_setspecific@PLT
    lea thread_one(%rip), %rdi
    xor %esi, %esi
    lea worker(%rip), %rdx
    lea worker_arg_one(%rip), %rcx
    call pthread_create@PLT
    test %eax, %eax
    jne .fail
    lea thread_two(%rip), %rdi
    xor %esi, %esi
    lea worker(%rip), %rdx
    lea worker_arg_two(%rip), %rcx
    call pthread_create@PLT
    test %eax, %eax
    jne .fail
    mov thread_one(%rip), %rdi
    xor %esi, %esi
    call pthread_join@PLT
    test %eax, %eax
    jne .fail
    mov thread_two(%rip), %rdi
    xor %esi, %esi
    call pthread_join@PLT
    test %eax, %eax
    jne .fail

    mov tls_key(%rip), %edi
    call pthread_getspecific@PLT
    mov $0x3333333333333333, %rcx
    cmp %rcx, %rax
    jne .fail
    mov worker_slot_one(%rip), %rax
    mov $0x1111111111111111, %rcx
    cmp %rcx, %rax
    jne .fail
    mov worker_slot_two(%rip), %rax
    mov $0x2222222222222222, %rcx
    cmp %rcx, %rax
    jne .fail
    mov tls_key(%rip), %edi
    call pthread_getspecific@PLT
    add worker_slot_one(%rip), %rax
    add worker_slot_two(%rip), %rax
    mov $0x6666666666666666, %rcx
    cmp %rcx, %rax
    jne .fail
    mov worker_slot_one(%rip), %rax
    xor worker_slot_two(%rip), %rax
    mov $0x3333333333333333, %rcx
    cmp %rcx, %rax
    jne .fail

    mov $1, %edi
    lea success(%rip), %rsi
    mov $24, %edx
    call write@PLT
    xor %eax, %eax
    pop %r13
    pop %r12
    leave
    ret

.fail_free:
    mov %r12, %rdi
    call free@PLT
.fail:
    mov $1, %edi
    lea failure(%rip), %rsi
    mov $26, %edx
    call write@PLT
    mov $1, %eax
    pop %r13
    pop %r12
    leave
    ret

.fail_dlclose:
    mov %r12, %rdi
    call dlclose@PLT
    jmp .fail

.type worker,@function
worker:
    push %rbp
    mov %rsp, %rbp
    push %r12
    push %r13
    mov %rdi, %r12
    mov tls_key(%rip), %edi
    mov 8(%r12), %rsi
    call pthread_setspecific@PLT
    mov $64, %edi
    call malloc@PLT
    test %rax, %rax
    jz .worker_done
    mov %rax, %r13
    movsd const_two(%rip), %xmm0
    call sqrt@PLT
    mov %r13, %rdi
    call free@PLT
    mov tls_key(%rip), %edi
    call pthread_getspecific@PLT
    mov (%r12), %rcx
    mov %rax, (%rcx)
.worker_done:
    xor %eax, %eax
    pop %r13
    pop %r12
    leave
    ret

.section .rodata
env_name:
    .asciz "VEX_GLIBC_WORKLOAD"
file_name:
    .asciz "/etc/ld.so.cache"
libm_name:
    .asciz "/lib/libm.so.6"
sqrt_name:
    .asciz "sqrt"
dyn_name:
    .asciz "/usr/linux-tests/libvexdyn.so"
dyn_value_name:
    .asciz "vex_dyn_value"
dyn_global_name:
    .asciz "vex_dyn_global"
success:
    .asciz "Vex glibc workload: ok\n"
failure:
    .asciz "Vex glibc workload: FAIL\n"
const_two:
    .double 2.0

.section .bss
.align 8
tls_key:
    .long 0
    .long 0
thread_one:
    .quad 0
thread_two:
    .quad 0
worker_slot_one:
    .quad 0
worker_slot_two:
    .quad 0
.section .data
worker_arg_one:
    .quad worker_slot_one
    .quad 0x1111111111111111
worker_arg_two:
    .quad worker_slot_two
    .quad 0x2222222222222222
clock_value:
    .zero 16
random_value:
    .zero 16
file_buffer:
    .zero 32
file_fd:
    .long -1
