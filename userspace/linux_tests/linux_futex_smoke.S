.global _start

# Futex smoke:
# - clone a thread sharing address space
# - child announces readiness, then FUTEX_WAITs while futex_word == 1
# - parent delays, writes wake marker, stores futex_word = 0, FUTEX_WAKEs
# - child writes after-wake marker only after the wake path

.set SYS_WRITE, 1
.set SYS_MMAP, 9
.set SYS_EXIT, 60
.set SYS_CLONE, 56
.set SYS_KILL, 62
.set SYS_FUTEX, 202
.set SYS_EXIT_GROUP, 231

# Exercise the exact private-futex path used by glibc/NPTL.  The compatibility
# layer must key these operations by the shared address space plus uaddr, not
# by the individual TID.
.set FUTEX_WAIT, 128
.set FUTEX_WAKE, 129

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

parent_wait_ready:
    cmpl $1, child_ready(%rip)
    je parent_delay
    pause
    jmp parent_wait_ready

parent_delay:
    # A waiter on futex_word must not be woken by an unrelated private
    # address in the same address space.  This is the negative case observed
    # in the WPE trace: WAKE_PRIVATE on another word legitimately returns 0.
    mov $SYS_FUTEX, %rax
    lea other_futex_word(%rip), %rdi
    mov $FUTEX_WAKE, %rsi
    mov $1, %rdx
    xor %r10, %r10
    xor %r8, %r8
    xor %r9, %r9
    syscall
    test %rax, %rax
    jne fail

    mov $4000000, %rcx
parent_spin:
    pause
    loop parent_spin

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea wake_message(%rip), %rsi
    mov $wake_message_len, %rdx
    syscall

    movl $0, futex_word(%rip)

    mov $SYS_FUTEX, %rax
    lea futex_word(%rip), %rdi
    mov $FUTEX_WAKE, %rsi
    mov $1, %rdx
    xor %r10, %r10
    xor %r8, %r8
    xor %r9, %r9
    syscall
    test %rax, %rax
    jle fail

parent_wait_done:
    cmpl $1, child_done(%rip)
    je parent_done
    pause
    jmp parent_wait_done

parent_done:
    # Create a second waiter and kill it while it is blocked in FUTEX_WAIT.
    # The kernel must unlink its stack-resident waiter before the wake below.
    movl $1, futex_word(%rip)
    movl $1, kill_phase(%rip)
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
    mov %eax, kill_child_pid(%rip)

parent_wait_kill_ready:
    cmpl $1, kill_child_ready(%rip)
    je parent_kill_waiter
    pause
    jmp parent_wait_kill_ready

parent_kill_waiter:
    # The cancellation subtest deliberately targets ONE native Thread. Linux
    # kill/tgkill(SIGKILL) must terminate its entire Process, so they cannot
    # implement this test's contract. Use the explicit native domain, keeping
    # the Linux private-futex wait/wake and the native task cancellation tested.
    movabs $0x5645584e00000066, %rax # VEX_SYS_PROCESS_KILL=102, task contract
    mov kill_child_pid(%rip), %rdi
    mov $9, %rsi
    syscall
    test %rax, %rax
    js fail
    movl $0, futex_word(%rip)
    mov $SYS_FUTEX, %rax
    lea futex_word(%rip), %rdi
    mov $FUTEX_WAKE, %rsi
    mov $1, %rdx
    xor %r10, %r10
    xor %r8, %r8
    xor %r9, %r9
    syscall
    test %rax, %rax
    jne fail

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea kill_done_message(%rip), %rsi
    mov $kill_done_message_len, %rdx
    syscall

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall

    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall

child_path:
    cmpl $1, kill_phase(%rip)
    je kill_child_path
normal_child_path:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea waiting_message(%rip), %rsi
    mov $waiting_message_len, %rdx
    syscall

    movl $1, child_ready(%rip)

    mov $SYS_FUTEX, %rax
    lea futex_word(%rip), %rdi
    mov $FUTEX_WAIT, %rsi
    mov $1, %rdx
    xor %r10, %r10
    xor %r8, %r8
    xor %r9, %r9
    syscall
    test %rax, %rax
    js fail_child

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea after_wake_message(%rip), %rsi
    mov $after_wake_message_len, %rdx
    syscall

    movl $1, child_done(%rip)

    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall

kill_child_path:
    movl $1, kill_child_ready(%rip)
    mov $SYS_FUTEX, %rax
    lea futex_word(%rip), %rdi
    mov $FUTEX_WAIT, %rsi
    mov $1, %rdx
    xor %r10, %r10
    xor %r8, %r8
    xor %r9, %r9
    syscall
    # Reaching this point means the waiter was woken instead of killed.
    jmp fail_child

fail_child:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea child_fail_message(%rip), %rsi
    mov $child_fail_message_len, %rdx
    syscall

    movl $1, child_done(%rip)

    mov $SYS_EXIT, %rax
    mov $115, %rdi
    syscall

fail:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_message(%rip), %rsi
    mov $fail_message_len, %rdx
    syscall

    mov $SYS_EXIT_GROUP, %rax
    mov $116, %rdi
    syscall

.section .data
.balign 4
futex_word:
    .long 1
other_futex_word:
    .long 1
child_ready:
    .long 0
child_done:
    .long 0
kill_phase:
    .long 0
kill_child_ready:
    .long 0
kill_child_pid:
    .long 0

.section .rodata
start_message:
    .ascii "Vex ELF futex smoke: start\n"
start_message_len = . - start_message
waiting_message:
    .ascii "Vex ELF futex smoke: child waiting\n"
waiting_message_len = . - waiting_message
wake_message:
    .ascii "Vex ELF futex smoke: parent wake\n"
wake_message_len = . - wake_message
after_wake_message:
    .ascii "Vex ELF futex smoke: child after wake\n"
after_wake_message_len = . - after_wake_message
done_message:
    .ascii "Vex ELF futex smoke: done\n"
done_message_len = . - done_message
fail_message:
    .ascii "Vex ELF futex smoke: failed\n"
fail_message_len = . - fail_message
child_fail_message:
    .ascii "Vex ELF futex smoke: child wait failed\n"
child_fail_message_len = . - child_fail_message
kill_done_message:
    .ascii "Vex ELF futex smoke: kill done\n"
kill_done_message_len = . - kill_done_message
