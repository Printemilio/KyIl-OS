.global _start

# Memory ownership smoke:
# - MAP_SHARED survives fork with one physical backing
# - MAP_PRIVATE is copied on fork
# - partial mprotect/munmap keep the process-memory inventory usable

.set SYS_WRITE, 1
.set SYS_MMAP, 9
.set SYS_MPROTECT, 10
.set SYS_MUNMAP, 11
.set SYS_FORK, 57
.set SYS_EXIT, 60
.set SYS_WAIT4, 61
.set SYS_EXIT_GROUP, 231
.set PROT_READ, 1
.set PROT_WRITE, 2
.set PROT_READ_WRITE, 3
.set MAP_SHARED_ANON, 0x21
.set MAP_PRIVATE_ANON, 0x22
.set PAGE, 4096
.set LARGE_LEN, 0x200000

.section .text
.type _start,@function
_start:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea start_message(%rip), %rsi
    mov $start_message_len, %rdx
    syscall

    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $PAGE, %rsi
    mov $PROT_READ_WRITE, %rdx
    mov $MAP_SHARED_ANON, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae fail
    mov %rax, %r12
    movl $0x11223344, (%r12)
    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    jz shared_child
    js fail
    mov %rax, %r13
    mov $SYS_WAIT4, %rax
    mov %r13, %rdi
    lea status_word(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp %r13, %rax
    jne fail
    cmpl $0x55667788, (%r12)
    jne fail
    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $PAGE, %rsi
    mov $PROT_READ_WRITE, %rdx
    mov $MAP_PRIVATE_ANON, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae fail
    mov %rax, %r14
    movl $0x77889900, (%r14)

    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    jz private_child
    js fail
    mov %rax, %r13
    mov $SYS_WAIT4, %rax
    mov %r13, %rdi
    lea status_word(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp %r13, %rax
    jne fail
    cmpl $0x77889900, (%r14)
    jne fail
    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $(PAGE * 3), %rsi
    mov $PROT_READ_WRITE, %rdx
    mov $MAP_PRIVATE_ANON, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae fail
    mov %rax, %r15
    mov $SYS_MPROTECT, %rax
    lea PAGE(%r15), %rdi
    mov $PAGE, %rsi
    mov $PROT_READ, %rdx
    syscall
    test %rax, %rax
    js fail
    mov $SYS_MUNMAP, %rax
    lea PAGE(%r15), %rdi
    mov $PAGE, %rsi
    syscall
    test %rax, %rax
    js fail

    # A large anonymous mapping uses the direct VMM path rather than the
    # small MRAC staging buffer; fork must still snapshot its current bytes.
    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $LARGE_LEN, %rsi
    mov $PROT_READ_WRITE, %rdx
    mov $MAP_PRIVATE_ANON, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae fail
    mov %rax, %rbx
    movl $0x13572468, (%rbx)
    movl $0x24681357, LARGE_LEN - 4(%rbx)
    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    jz large_child
    js fail
    mov %rax, %r13
    mov $SYS_WAIT4, %rax
    mov %r13, %rdi
    lea status_word(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp %r13, %rax
    jne fail
    cmpl $0x13572468, (%rbx)
    jne fail
    cmpl $0x24681357, LARGE_LEN - 4(%rbx)
    jne fail
    # Regress the MRAC lifetime path repeatedly: each child creates a
    # private backing, splits it with mprotect, unmaps both pieces, and exits.
    # The parent waits before creating the next child so no stale process or
    # namespace reference may survive into the following iteration.
    mov $4, %r15d
mrac_cycle_parent:
    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    jz mrac_cycle_child
    js fail
    mov %rax, %r13
    mov $SYS_WAIT4, %rax
    mov %r13, %rdi
    lea status_word(%rip), %rsi
    xor %rdx, %rdx
    xor %r9, %r9
    xor %r10, %r10
    syscall
    cmp %r13, %rax
    jne fail
    dec %r15d
    jnz mrac_cycle_parent
    jmp mrac_cycle_done

mrac_cycle_child:
    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $(PAGE * 2), %rsi
    mov $PROT_READ_WRITE, %rdx
    mov $MAP_PRIVATE_ANON, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae fail
    mov %rax, %rbx
    mov $SYS_MPROTECT, %rax
    lea PAGE(%rbx), %rdi
    mov $PAGE, %rsi
    mov $PROT_READ, %rdx
    syscall
    test %rax, %rax
    js fail
    mov $SYS_MUNMAP, %rax
    mov %rbx, %rdi
    mov $PAGE, %rsi
    syscall
    test %rax, %rax
    js fail
    mov $SYS_MUNMAP, %rax
    lea PAGE(%rbx), %rdi
    mov $PAGE, %rsi
    syscall
    test %rax, %rax
    js fail
    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall

mrac_cycle_done:

    # Reverse lifetime order: the parent exits first, while the child keeps
    # using the shared backing.  This exercises refcount release on orphaning.
    movl $0x31415926, (%r12)
    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    jz parent_exit_child
    js fail
    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall

parent_exit_child:
    cmpl $0x31415926, (%r12)
    jne fail
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea parent_exit_message(%rip), %rsi
    mov $parent_exit_message_len, %rdx
    syscall

done_success:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall
    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall

shared_child:
    movl $0x55667788, (%r12)
    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall

private_child:
    movl $0x00112233, (%r14)
    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall

large_child:
    movl $0xdeadbeef, (%rbx)
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
    mov $127, %rdi
    syscall

.section .bss
.align 4
status_word:
    .long 0

.section .rodata
start_message:
    .ascii "Vex ELF memory-ownership smoke: start\n"
start_message_len = . - start_message
done_message:
    .ascii "Vex ELF memory-ownership smoke: done\n"
done_message_len = . - done_message
fail_message:
    .ascii "Vex ELF memory-ownership smoke: failed\n"
fail_message_len = . - fail_message
parent_exit_message:
    .ascii "Vex ELF memory-ownership smoke: parent-exit child ok\n"
parent_exit_message_len = . - parent_exit_message
