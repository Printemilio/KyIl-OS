.global _start

# Supported native-event translation plus unsupported Linux contracts must be
# explicit.  SIGUSR1 exercises handler delivery and rt_sigreturn; robust-list
# and rseq remain honest ENOSYS probes.

.set SYS_WRITE, 1
.set SYS_EXIT_GROUP, 231
.set SYS_RT_SIGACTION, 13
.set SYS_RT_SIGPROCMASK, 14
.set SYS_RT_SIGRETURN, 15
.set SYS_GETPID, 39
.set SYS_TGKILL, 234
.set SYS_SET_ROBUST_LIST, 273
.set SYS_RSEQ, 334
.set SYS_OPEN, 2
.set SYS_READ, 0
.set SYS_CLOSE, 3
.set SYS_EVENTFD2, 290
.set SYS_FORK, 57
.set SYS_EXIT, 60
.set SYS_MMAP, 9
.set SYS_SCHED_YIELD, 24
.set SYS_CLONE, 56
.set SYS_STATFS, 137
.set SYS_SYSINFO, 99
.set SYS_GETRLIMIT, 97
.set SYS_PRLIMIT64, 302
.set RLIMIT_STACK, 3
.set ENOSYS_NEG, -38
.set SIGUSR1, 10
.set SIGPIPE, 13
.set SIGCANCEL, 32
.set SIGSETXID, 33
.set SIG_BLOCK, 0
.set SIG_UNBLOCK, 1
.set SIG_SETMASK, 2
.set SA_RESTART_RESTORER, 0x14000000
.set SA_SIGINFO_RESTART_RESTORER, 0x1c000004
.set VEXFS_MAGIC, 0x56455846
.set PROT_READ_WRITE, 3
.set MAP_PRIVATE_ANON, 0x22
.set CLONE_VM, 0x00000100
.set CLONE_FS, 0x00000200
.set CLONE_FILES, 0x00000400
.set CLONE_SIGHAND, 0x00000800
.set CLONE_SETTLS, 0x00080000
.set CLONE_PARENT_SETTID, 0x00100000
.set CLONE_CHILD_CLEARTID, 0x00200000
.set CLONE_CHILD_SETTID, 0x01000000
.set CLONE_THREAD, 0x00010000
.set CLONE_FLAGS, CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID | CLONE_THREAD

.section .text
_start:
    /* Native event registration must reject a kernel address supplied by
       ring 3 before it is copied or dereferenced. */
    mov $SYS_RT_SIGACTION, %rax
    mov $SIGUSR1, %rdi
    movabs $0xffff800000000000, %rsi
    xor %rdx, %rdx
    mov $8, %r10
    syscall
    cmp $-14, %rax
    jne fail

    # Structured system/time replies must also reject supervisor addresses
    # before writing. These paths run concurrently with sibling VM updates.
    mov $SYS_SYSINFO, %eax
    movabs $0xffff800000000000, %rdi
    syscall
    cmp $-14, %rax
    jne fail
    mov $SYS_OPEN, %eax
    movabs $0xffff800000000000, %rdi
    xor %esi, %esi
    syscall
    cmp $-14, %rax
    jne fail
    mov $228, %eax
    mov $1, %edi
    movabs $0xffff800000000000, %rsi
    syscall
    cmp $-14, %rax
    jne fail
    mov $229, %eax
    mov $1, %edi
    mov $1, %esi
    syscall
    cmp $-14, %rax
    jne fail

    mov $SYS_RT_SIGACTION, %rax
    mov $SIGUSR1, %rdi
    lea sig_action(%rip), %rsi
    lea old_action(%rip), %rdx
    mov $8, %r10
    syscall
    test %rax, %rax
    jne fail

    /* Linux NPTL's reserved signals are represented by compatibility-only
       Vex event slots.  Exercise SA_SIGINFO, targeted delivery, masking and
       rt_sigreturn without exposing Linux signal numbers to native VexOS. */
    mov $SYS_RT_SIGACTION, %rax
    mov $SIGCANCEL, %rdi
    lea sigcancel_action(%rip), %rsi
    lea old_action(%rip), %rdx
    mov $8, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $SYS_RT_SIGACTION, %rax
    mov $SIGSETXID, %rdi
    lea sigsetxid_action(%rip), %rsi
    lea old_action(%rip), %rdx
    mov $8, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $SYS_GETPID, %rax
    syscall
    mov %eax, %r14d
    mov $186, %rax
    syscall
    mov %eax, %r15d
    mov $SYS_TGKILL, %rax
    mov %r14d, %edi
    mov %r15d, %esi
    mov $SIGSETXID, %edx
    syscall
    test %rax, %rax
    jne fail
    cmpl $1, internal_info_seen(%rip)
    jne fail

    mov $SYS_RT_SIGPROCMASK, %rax
    mov $SIG_BLOCK, %rdi
    lea internal_set(%rip), %rsi
    lea old_set(%rip), %rdx
    mov $8, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $SYS_TGKILL, %rax
    mov %r14d, %edi
    mov %r15d, %esi
    mov $SIGCANCEL, %edx
    syscall
    test %rax, %rax
    jne fail
    cmpl $0, internal_cancel_seen(%rip)
    jne fail
    mov $SYS_RT_SIGPROCMASK, %rax
    mov $SIG_UNBLOCK, %rdi
    lea internal_set(%rip), %rsi
    lea old_set(%rip), %rdx
    mov $8, %r10
    syscall
    test %rax, %rax
    jne fail
    cmpl $1, internal_cancel_seen(%rip)
    jne fail

    /* SIGPIPE has no native Vex event, but SIG_IGN and its complete Linux
       action metadata are still representable and must not reject masks. */
    mov $SYS_RT_SIGACTION, %rax
    mov $SIGPIPE, %rdi
    lea sigpipe_action(%rip), %rsi
    lea old_action(%rip), %rdx
    mov $8, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $SYS_RT_SIGACTION, %rax
    mov $SIGPIPE, %rdi
    xor %rsi, %rsi
    lea old_action(%rip), %rdx
    mov $8, %r10
    syscall
    test %rax, %rax
    jne fail
    cmpq $1, old_action(%rip)
    jne fail
    cmpq $SA_RESTART_RESTORER, old_action+8(%rip)
    jne fail
    mov $0x80000000, %r11d
    cmpq %r11, old_action+24(%rip)
    jne fail

    /* Deliver both internal signals to a distinct CLONE_THREAD task.  The
       shared counters prove target-thread routing rather than self delivery. */
    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $0x4000, %rsi
    mov $PROT_READ_WRITE, %rdx
    mov $MAP_PRIVATE_ANON, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae fail
    lea 0x4000(%rax), %rsi
    and $-16, %rsi
    mov $SYS_CLONE, %rax
    mov $CLONE_FLAGS, %rdi
    mov %rsi, %rsi
    lea internal_parent_tid(%rip), %rdx
    lea internal_child_tid(%rip), %r10
    lea child_tls(%rip), %r8
    syscall
    test %rax, %rax
    jz internal_child_path
    js fail
    mov %eax, internal_child_tid(%rip)
internal_parent_wait_ready:
    cmpl $1, internal_child_ready(%rip)
    je internal_parent_send
    mov $SYS_SCHED_YIELD, %rax
    syscall
    jmp internal_parent_wait_ready
internal_parent_send:
    mov $SYS_TGKILL, %rax
    mov %r14d, %edi
    mov internal_child_tid(%rip), %esi
    mov $SIGCANCEL, %edx
    syscall
    test %rax, %rax
    jne fail
internal_parent_wait_cancel:
    cmpl $2, internal_cancel_seen(%rip)
    jae internal_parent_send_xid
    mov $SYS_SCHED_YIELD, %rax
    syscall
    jmp internal_parent_wait_cancel
internal_parent_send_xid:
    mov $SYS_TGKILL, %rax
    mov %r14d, %edi
    mov internal_child_tid(%rip), %esi
    mov $SIGSETXID, %edx
    syscall
    test %rax, %rax
    jne fail
internal_parent_wait_xid:
    cmpl $2, internal_info_seen(%rip)
    jae internal_parent_stop
    mov $SYS_SCHED_YIELD, %rax
    syscall
    jmp internal_parent_wait_xid
internal_parent_stop:
    movl $1, internal_child_stop(%rip)
internal_parent_wait_child:
    cmpl $1, internal_child_done(%rip)
    je internal_after_thread
    mov $SYS_SCHED_YIELD, %rax
    syscall
    jmp internal_parent_wait_child
internal_after_thread:
    /* Querying the installed action must also be a real operation. */
    mov $SYS_RT_SIGACTION, %rax
    mov $SIGUSR1, %rdi
    xor %rsi, %rsi
    lea old_action(%rip), %rdx
    mov $8, %r10
    syscall
    test %rax, %rax
    jne fail

    /* tgkill self -> native VexOS event -> validated user handler ->
       rt_sigreturn restores the interrupted syscall frame. */
    mov $SYS_GETPID, %rax
    syscall
    mov %eax, %r14d
    mov $186, %rax
    syscall
    mov %eax, %r15d
    mov $SYS_TGKILL, %rax
    mov %r14d, %edi
    mov %r15d, %esi
    mov $SIGUSR1, %edx
    syscall
    test %rax, %rax
    jne fail
    cmpl $1, event_seen(%rip)
    jne fail

    /* Virtual Linux environment: real VexOS-backed /proc and /dev views. */
    mov $SYS_OPEN, %rax
    lea proc_filesystems(%rip), %rdi
    xor %rsi, %rsi
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $SYS_READ, %rax
    mov %r12, %rdi
    lea io_buffer(%rip), %rsi
    mov $64, %rdx
    syscall
    test %rax, %rax
    jle fail
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall
    /* /proc/self/maps is generated from the process_memory inventory.  A
       successful non-empty read is required; the contents are not a fixed
       compatibility string. */
    mov $SYS_OPEN, %rax
    lea proc_self_maps(%rip), %rdi
    xor %rsi, %rsi
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $SYS_READ, %rax
    mov %r12, %rdi
    lea io_buffer(%rip), %rsi
    mov $128, %rdx
    syscall
    test %rax, %rax
    jle fail
    mov %rax, %rcx
    /* The view must contain real permission/mapping markers, not merely a
       non-empty compatibility string.  At least one private readable region
       is guaranteed by this process' text/stack mappings. */
    lea io_buffer(%rip), %rsi
    xor %r8d, %r8d
    xor %r9d, %r9d
maps_perm_scan:
    cmp %rcx, %r8
    jae maps_perm_done
    cmpb $'r', (%rsi,%r8,1)
    jne maps_check_private
    or $1, %r9d
maps_check_private:
    cmpb $'p', (%rsi,%r8,1)
    jne maps_perm_next
    or $2, %r9d
maps_perm_next:
    inc %r8
    jmp maps_perm_scan
maps_perm_done:
    and $3, %r9d
    cmp $3, %r9d
    jne fail
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall
    /* glibc implements getrlimit through prlimit64.  The fixed Vex stack
       reservation must be reported as a real finite bound, including via
       the legacy syscall, and bad output pointers must fail. */
    mov $SYS_PRLIMIT64, %rax
    xor %rdi, %rdi
    mov $RLIMIT_STACK, %rsi
    xor %rdx, %rdx
    lea stack_limit(%rip), %r10
    syscall
    test %rax, %rax
    jne fail
    mov stack_limit(%rip), %r8
    cmp $32768, %r8
    jb fail
    cmp $0x40000000, %r8
    ja fail
    cmp stack_limit+8(%rip), %r8
    jne fail
    mov $SYS_GETRLIMIT, %rax
    mov $RLIMIT_STACK, %rdi
    lea stack_limit_legacy(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    cmp stack_limit_legacy(%rip), %r8
    jne fail
    mov $SYS_PRLIMIT64, %rax
    xor %rdi, %rdi
    mov $RLIMIT_STACK, %rsi
    xor %rdx, %rdx
    mov $1, %r10
    syscall
    cmp $-14, %rax
    jne fail
    /* Native VexOS event counter through Linux eventfd2. */
    mov $SYS_EVENTFD2, %rax
    xor %rdi, %rdi
    mov $0x800, %rsi       /* EFD_NONBLOCK */
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $SYS_READ, %rax
    mov %r12, %rdi
    lea event_value(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $-11, %rax
    jne fail
    movq $5, event_value(%rip)
    mov $SYS_WRITE, %rax
    mov %r12, %rdi
    lea event_value(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $8, %rax
    jne fail
    mov $SYS_READ, %rax
    mov %r12, %rdi
    lea event_value(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $8, %rax
    jne fail
    cmpq $5, event_value(%rip)
    jne fail
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall
    /* Semaphore mode consumes one unit per read and then reports EAGAIN. */
    mov $SYS_EVENTFD2, %rax
    mov $2, %rdi
    mov $0x801, %rsi       /* EFD_SEMAPHORE | EFD_NONBLOCK */
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $SYS_READ, %rax
    mov %r12, %rdi
    lea event_value(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $8, %rax
    jne fail
    cmpq $1, event_value(%rip)
    jne fail
    mov $SYS_READ, %rax
    mov %r12, %rdi
    lea event_value(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $8, %rax
    jne fail
    mov $SYS_READ, %rax
    mov %r12, %rdi
    lea event_value(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $-11, %rax
    jne fail
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall

    /* A blocking counter read is scheduler-backed and is woken by a forked
       sibling writing the same process-shared descriptor. */
    mov $SYS_EVENTFD2, %rax
    xor %rdi, %rdi
    xor %rsi, %rsi
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $SYS_FORK, %rax
    syscall
    test %rax, %rax
    js fail
    jz event_child
    /* Parent: this read must sleep until the child signals the counter. */
    mov $SYS_READ, %rax
    mov %r12, %rdi
    lea event_value(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $8, %rax
    jne fail
    cmpq $7, event_value(%rip)
    jne fail
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall
    jmp event_done
event_child:
    movq $7, event_value(%rip)
    mov $SYS_WRITE, %rax
    mov %r12, %rdi
    lea event_value(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $8, %rax
    jne fail
    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall
event_done:
    mov $SYS_OPEN, %rax
    lea dev_urandom(%rip), %rdi
    xor %rsi, %rsi
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $SYS_READ, %rax
    mov %r12, %rdi
    lea io_buffer(%rip), %rsi
    mov $32, %rdx
    syscall
    cmp $32, %rax
    jne fail
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall
    /* A closed descriptor is not a reusable global capability. */
    mov $SYS_READ, %rax
    mov %r12, %rdi
    lea event_value(%rip), %rsi
    mov $8, %rdx
    syscall
    cmp $-9, %rax
    jne fail
    mov $SYS_STATFS, %rax
    lea root_path(%rip), %rdi
    lea statfs_buffer(%rip), %rsi
    syscall
    test %rax, %rax
    js fail
    cmpl $VEXFS_MAGIC, statfs_buffer(%rip)
    jne fail
    cmpq $512, statfs_buffer+8(%rip)
    jne fail
    mov $SYS_SYSINFO, %rax
    lea sysinfo_buffer(%rip), %rdi
    syscall
    test %rax, %rax
    js fail
    cmpq $0, sysinfo_buffer+32(%rip)
    je fail
    mov $SYS_RT_SIGPROCMASK, %rax
    mov $SIG_BLOCK, %rdi
    lea sig_set(%rip), %rsi
    lea old_set(%rip), %rdx
    mov $8, %r10
    syscall
    test %rax, %rax
    jne fail

    movl $0, event_seen(%rip)
    mov $SYS_TGKILL, %rax
    mov %r14d, %edi
    mov %r15d, %esi
    mov $SIGUSR1, %edx
    syscall
    test %rax, %rax
    jne fail
    cmpl $0, event_seen(%rip)
    jne fail

    mov $SYS_RT_SIGPROCMASK, %rax
    mov $SIG_UNBLOCK, %rdi
    lea sig_set(%rip), %rsi
    lea old_set(%rip), %rdx
    mov $8, %r10
    syscall
    test %rax, %rax
    jne fail
    cmpl $1, event_seen(%rip)
    jne fail

    mov $SYS_SET_ROBUST_LIST, %rax
    xor %rdi, %rdi
    xor %rsi, %rsi
    syscall
    cmp $ENOSYS_NEG, %rax
    jne fail

    mov $SYS_RSEQ, %rax
    xor %rdi, %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp $ENOSYS_NEG, %rax
    jne fail

    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall
    mov $SYS_EXIT_GROUP, %rax
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

internal_child_path:
    movl $1, internal_child_ready(%rip)
internal_child_wait:
    cmpl $1, internal_child_stop(%rip)
    je internal_child_exit
    mov $SYS_SCHED_YIELD, %rax
    syscall
    jmp internal_child_wait
internal_child_exit:
    movl $1, internal_child_done(%rip)
    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall
    jmp fail

.section .rodata
done_message:
    .ascii "Vex Linux internal signal smoke: 32/33 mask/info SIGPIPE ok\nVex Linux virtual env smoke: /proc /dev statfs sysinfo ok\nVex Linux stack limit smoke: prlimit64/getrlimit ok\n"
done_message_len = . - done_message
fail_message:
    .ascii "Vex ELF Linux unsupported syscall smoke: failed\n"
fail_message_len = . - fail_message
.section .rodata
proc_filesystems:
    .asciz "/proc/filesystems"
dev_urandom:
    .asciz "/dev/urandom"
proc_self_maps:
    .asciz "/proc/self/maps"
root_path:
    .asciz "/"

.section .bss
.align 8
event_seen:
    .long 0
internal_cancel_seen:
    .long 0
internal_info_seen:
    .long 0
internal_child_tid:
    .long 0
internal_parent_tid:
    .long 0
internal_child_ready:
    .long 0
internal_child_stop:
    .long 0
internal_child_done:
    .long 0
old_set:
    .quad 0
old_action:
    .skip 40
io_buffer:
    .skip 128
statfs_buffer:
    .skip 128
sysinfo_buffer:
    .skip 384
stack_limit:
    .skip 16
stack_limit_legacy:
    .skip 16
event_value:
    .quad 0

.section .data
.align 8
sig_set:
    .quad 0x200
internal_set:
    .quad 0x180000000
sig_action:
    .quad sigusr1_handler
    .quad 0
    .quad sigusr1_restorer
    .quad 0
    .quad 0

sigcancel_action:
    .quad sigcancel_handler
    .quad SA_RESTART_RESTORER
    .quad sigusr1_restorer
    .quad 0
    .quad 0

sigsetxid_action:
    .quad sigsetxid_handler
    .quad SA_SIGINFO_RESTART_RESTORER
    .quad sigusr1_restorer
    .quad 0
    .quad 0

sigpipe_action:
    .quad 1
    .quad SA_RESTART_RESTORER
    .quad sigusr1_restorer
    .quad 0x80000000
    .quad 0

child_tls:
    .quad 0

.section .text
.align 8
sigusr1_handler:
    movl $1, event_seen(%rip)
    # Handler syscalls must not overwrite the interrupted syscall result.
    mov $SYS_GETPID, %rax
    syscall
    ret

sigcancel_handler:
    incl internal_cancel_seen(%rip)
    ret

sigsetxid_handler:
    /* Linux SI_TKILL: si_code=-6 and si_pid is the sending process. */
    cmpl $-6, 8(%rsi)
    jne sigsetxid_bad
    movl 16(%rsi), %eax
    cmpl %r14d, %eax
    jne sigsetxid_bad
    incl internal_info_seen(%rip)
sigsetxid_bad:
    ret

sigusr1_restorer:
    mov $SYS_RT_SIGRETURN, %rax
    syscall
