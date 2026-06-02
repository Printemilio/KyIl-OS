.section .text
.global syscall_entry
.type syscall_entry, @function

.extern scheduler_record_syscall_frame

# x86_64 syscall entry.
# Userspace convention:
#   rax = syscall number
#   rdi = arg1
#   rsi = arg2
#   rdx = arg3
#   r10 = arg4
#   r8  = arg5
#   r9  = arg6
# Return:
#   rax = syscall return value
syscall_entry:
    push %rcx
    push %r11
    push %rbx
    push %rdx
    push %rbp
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r12
    push %r13
    push %r14
    push %r15

    sub $24, %rsp
    mov %rax, 8(%rsp)
    mov %rsp, %rdi
    call scheduler_record_syscall_frame
    mov 8(%rsp), %rdi
    mov 80(%rsp), %rsi
    mov 88(%rsp), %rdx
    mov 104(%rsp), %rcx
    mov 56(%rsp), %r8
    mov 72(%rsp), %r9
    mov 64(%rsp), %rax
    mov %rax, (%rsp)
    call syscall_dispatch_raw
    add $24, %rsp

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rbp
    pop %rdx
    pop %rbx
    pop %r11
    pop %rcx
    sysretq
