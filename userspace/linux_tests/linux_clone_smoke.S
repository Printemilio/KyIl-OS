.global _start

# clone() raw thread smoke:
# - mmap a private child stack
# - clone(CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD)
# - child writes a marker and exits via exit(0)
# - parent spins briefly, writes done, then exit_group(0)

.set SYS_WRITE, 1
.set SYS_MMAP, 9
.set SYS_EXIT, 60
.set SYS_CLONE, 56
.set SYS_EXIT_GROUP, 231

.set PROT_READ_WRITE, 3
.set MAP_PRIVATE_ANON, 0x22
.set STACK_SIZE, 0x4000

.set CLONE_VM, 0x00000100
.set CLONE_FS, 0x00000200
.set CLONE_FILES, 0x00000400
.set CLONE_SIGHAND, 0x00000800
.set CLONE_THREAD, 0x00010000
.set CLONE_FLAGS, CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD

.section .text
_start:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea start_message(%rip), %rsi
    mov $start_message_len, %rdx
    syscall

    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $STACK_SIZE, %rsi
    mov $PROT_READ_WRITE, %rdx
    mov $MAP_PRIVATE_ANON, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae fail

    lea STACK_SIZE(%rax), %rsi
    and $-16, %rsi

    mov $SYS_CLONE, %rax
    mov $CLONE_FLAGS, %rdi
    xor %rdx, %rdx
    xor %r10, %r10
    xor %r8, %r8
    syscall

    test %rax, %rax
    jz child_path
    js fail

parent_path:
    mov $2000000, %rcx
parent_spin:
    pause
    loop parent_spin

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall

    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall

child_path:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea child_message(%rip), %rsi
    mov $child_message_len, %rdx
    syscall

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
    mov $114, %rdi
    syscall

.section .rodata
start_message:
    .ascii "Vex ELF clone smoke: start\n"
start_message_len = . - start_message
child_message:
    .ascii "Vex ELF clone smoke: child\n"
child_message_len = . - child_message
done_message:
    .ascii "Vex ELF clone smoke: done\n"
done_message_len = . - done_message
fail_message:
    .ascii "Vex ELF clone smoke: failed\n"
fail_message_len = . - fail_message
