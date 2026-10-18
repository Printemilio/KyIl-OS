.section .text
.global resume_syscall_return
.type resume_syscall_return, @function

# void resume_syscall_return(uint64_t frame_rsp, uint64_t return_value)
# rdi = saved syscall frame rsp, rsi = return rax value.
resume_syscall_return:
    cli                         # scheduler may have restored IF before entry
    mov %rdi, %rsp
    mov %rsi, %rax
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
    pop %rsp
    swapgs
    sysretq
