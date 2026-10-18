.global _start

# execve(argv, envp) and the Linux initial stack contract smoke test.
# The first invocation is the parent.  It forks and execs itself with a real
# argv/envp vector; the post-exec invocation validates argc/argv/envp/auxv.

.set SYS_WRITE, 1
.set SYS_FORK, 57
.set SYS_EXECVE, 59
.set SYS_WAIT4, 61
.set SYS_EXIT, 60
.set SYS_EXIT_GROUP, 231

.section .text
_start:
    mov (%rsp), %rax
    cmp $1, %rax
    jne check_exec_stack

parent:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea start_message(%rip), %rsi
    mov $start_message_len, %rdx
    syscall
    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    js fail
    jz child_exec
    mov %rax, %r12

    mov $SYS_WAIT4, %rax
    mov %r12, %rdi
    lea status_word(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp %r12, %rax
    jne fail
    mov status_word(%rip), %eax
    and $0x7f, %eax
    jne fail
    mov status_word(%rip), %eax
    shr $8, %eax
    and $0xff, %eax
    test %eax, %eax
    jne fail

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall
    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall

child_exec:
    mov $SYS_EXECVE, %rax
    lea self_path(%rip), %rdi
    lea exec_argv(%rip), %rsi
    lea exec_envp(%rip), %rdx
    syscall
    jmp fail

check_exec_stack:
    # Linux x86-64 enters the program with a 16-byte aligned stack.
    test $0xf, %rsp
    jne fail
    mov (%rsp), %rax
    cmp $2, %rax
    jne fail

    mov 8(%rsp), %rdi
    cmpb $'V', 0(%rdi)
    jne fail
    cmpb $'X', 1(%rdi)
    jne fail
    cmpb $'A', 2(%rdi)
    jne fail
    cmpb $'R', 3(%rdi)
    jne fail
    cmpb $'G', 4(%rdi)
    jne fail
    cmpb $'V', 5(%rdi)
    jne fail
    cmpb $'0', 6(%rdi)
    jne fail

    mov 16(%rsp), %rdi
    cmpb $'s', 0(%rdi)
    jne fail
    cmpb $'e', 1(%rdi)
    jne fail
    cmpb $'c', 2(%rdi)
    jne fail
    cmpb $'o', 3(%rdi)
    jne fail
    cmpb $'n', 4(%rdi)
    jne fail
    cmpb $'d', 5(%rdi)
    jne fail

    mov 32(%rsp), %rdi
    cmpb $'V', 0(%rdi)
    jne fail
    cmpb $'E', 1(%rdi)
    jne fail
    cmpb $'X', 2(%rdi)
    jne fail
    cmpb $'_', 3(%rdi)
    jne fail
    cmpb $'T', 4(%rdi)
    jne fail
    cmpb $'E', 5(%rdi)
    jne fail
    cmpb $'S', 6(%rdi)
    jne fail
    cmpb $'T', 7(%rdi)
    jne fail
    cmpb $'=', 8(%rdi)
    jne fail
    cmpb $'o', 9(%rdi)
    jne fail
    cmpb $'k', 10(%rdi)
    jne fail

    # Skip argv[argc], envp and locate AT_RANDOM in auxv.
    lea 32(%rsp), %rdi
skip_env:
    cmpq $0, (%rdi)
    je env_done
    add $8, %rdi
    jmp skip_env
env_done:
    add $8, %rdi
find_aux:
    mov (%rdi), %rax
    test %rax, %rax
    jz fail
    cmp $25, %rax
    je found_random
    add $16, %rdi
    jmp find_aux
found_random:
    mov 8(%rdi), %rdi
    test %rdi, %rdi
    jz fail
    mov 0(%rdi), %rax
    or 8(%rdi), %rax
    test %rax, %rax
    jz fail

    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall

fail:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_message(%rip), %rsi
    mov $fail_message_len, %rdx
    syscall
    mov $SYS_EXIT_GROUP, %rax
    mov $111, %rdi
    syscall

.section .bss
.align 8
status_word:
    .long 0

.section .rodata
self_path:
    .asciz "/usr/linux-tests/linux_exec_args_smoke"
exec_argv:
    .quad argv0
    .quad argv1
    .quad 0
exec_envp:
    .quad env0
    .quad 0
argv0:
    .asciz "VXARGV0"
argv1:
    .asciz "second"
env0:
    .asciz "VEX_TEST=ok"
done_message:
    .ascii "Vex ELF exec argv/env/auxv smoke: done\n"
done_message_len = . - done_message
start_message:
    .ascii "Vex ELF exec argv/env/auxv smoke: start\n"
start_message_len = . - start_message
fail_message:
    .ascii "Vex ELF exec argv/env/auxv smoke: failed\n"
fail_message_len = . - fail_message
