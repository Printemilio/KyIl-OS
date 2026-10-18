.global _start

# Process/thread context smoke:
# - CLONE_VM thread with a distinct FS_BASE/TLS value
# - process identity (getpid) is shared while thread identity (gettid) differs
# - interleaved anonymous mmap allocations must never overlap
# - SSE and, when enabled by the kernel, AVX state survive scheduler yields
# - the child uses the process-wide stdout FD and CLONE_CHILD_CLEARTID

.set SYS_WRITE, 1
.set SYS_MMAP, 9
.set SYS_EXIT, 60
.set SYS_SCHED_YIELD, 24
.set SYS_GETPID, 39
.set SYS_GETTID, 186
.set SYS_CLONE, 56
.set SYS_EXIT_GROUP, 231

.set PROT_READ_WRITE, 3
.set MAP_PRIVATE_ANON, 0x22
.set STACK_SIZE, 0x4000
.set PAGE_SIZE, 0x1000
.set TLS_MAGIC, 0x564558544c530001

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
.align 16
_start:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea start_message(%rip), %rsi
    mov $start_message_len, %rdx
    syscall
    test %rax, %rax
    js fail

    mov $SYS_GETPID, %rax
    syscall
    mov %eax, parent_pid(%rip)
    mov $SYS_GETTID, %rax
    syscall
    mov %eax, parent_tid(%rip)

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
    lea parent_tid_arg(%rip), %rdx
    lea child_tid_slot(%rip), %r10
    lea child_tls(%rip), %r8
    syscall
    test %rax, %rax
    jz child_path
    js fail

parent_path:
    movdqa parent_sse_value(%rip), %xmm0
    lea parent_allocs(%rip), %r13
    mov $3, %r12
parent_alloc_loop:
    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $PAGE_SIZE, %rsi
    mov $PROT_READ_WRITE, %rdx
    mov $MAP_PRIVATE_ANON, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae fail
    mov %rax, -8(%r13,%r12,8)
    movl $0x50415245, (%rax)
    mov $SYS_SCHED_YIELD, %rax
    syscall
    dec %r12
    jnz parent_alloc_loop
    movl $1, parent_alloc_done(%rip)

parent_wait_child_alloc:
    cmpl $1, child_alloc_done(%rip)
    je parent_check_allocs
    mov $SYS_SCHED_YIELD, %rax
    syscall
    jmp parent_wait_child_alloc

parent_check_allocs:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea parent_alloc_check_message(%rip), %rsi
    mov $parent_alloc_check_message_len, %rdx
    syscall
    mov parent_allocs(%rip), %rax
    cmp child_allocs(%rip), %rax
    je parent_fail_alloc
    cmp child_allocs+8(%rip), %rax
    je parent_fail_alloc
    cmp child_allocs+16(%rip), %rax
    je parent_fail_alloc
    mov parent_allocs+8(%rip), %rax
    cmp child_allocs(%rip), %rax
    je parent_fail_alloc
    cmp child_allocs+8(%rip), %rax
    je parent_fail_alloc
    cmp child_allocs+16(%rip), %rax
    je parent_fail_alloc
    mov parent_allocs+16(%rip), %rax
    cmp child_allocs(%rip), %rax
    je parent_fail_alloc
    cmp child_allocs+8(%rip), %rax
    je parent_fail_alloc
    cmp child_allocs+16(%rip), %rax
    je parent_fail_alloc

parent_sse_loop:
    mov $64, %r12
parent_sse_yield:
    mov $SYS_SCHED_YIELD, %rax
    syscall
    movdqa %xmm0, %xmm1
    movdqa parent_sse_value(%rip), %xmm2
    pcmpeqd %xmm2, %xmm1
    pmovmskb %xmm1, %eax
    cmp $0xffff, %eax
    jne parent_fail_sse
    dec %r12
    jnz parent_sse_yield

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea parent_sse_done_message(%rip), %rsi
    mov $parent_sse_done_message_len, %rdx
    syscall

    mov $SYS_GETPID, %rax
    syscall
    cmp parent_pid(%rip), %eax
    jne parent_fail_identity
    mov $SYS_GETTID, %rax
    syscall
    cmp parent_tid(%rip), %eax
    jne parent_fail_identity

parent_wait_child:
    cmpl $1, child_done(%rip)
    je parent_wait_clear_tid
    mov $SYS_SCHED_YIELD, %rax
    syscall
    jmp parent_wait_child

parent_wait_clear_tid:
    cmpl $0, child_tid_slot(%rip)
    je parent_repeat_setup
    mov $SYS_SCHED_YIELD, %rax
    syscall
    jmp parent_wait_clear_tid

parent_repeat_setup:
    # Run the same lifecycle a second time.  The first thread was already
    # auto-reaped after CLONE_CHILD_CLEARTID, so this catches task-object and
    # group-refcount leaks while the leader remains alive.
    movl $0, child_done(%rip)
    movl $0, child_tid_slot(%rip)
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
    lea parent_tid_arg(%rip), %rdx
    lea child_tid_slot(%rip), %r10
    lea child_tls(%rip), %r8
    syscall
    test %rax, %rax
    jz child_path
    js fail

parent_repeat_wait:
    cmpl $1, child_done(%rip)
    je parent_repeat_wait_clear
    mov $SYS_SCHED_YIELD, %rax
    syscall
    jmp parent_repeat_wait

parent_repeat_wait_clear:
    cmpl $0, child_tid_slot(%rip)
    je parent_done
    mov $SYS_SCHED_YIELD, %rax
    syscall
    jmp parent_repeat_wait_clear

parent_done:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea parent_clear_done_message(%rip), %rsi
    mov $parent_clear_done_message_len, %rdx
    syscall
    mov child_seen_pid(%rip), %eax
    cmp parent_pid(%rip), %eax
    jne parent_fail_child_identity
    mov child_seen_tid(%rip), %eax
    cmp parent_tid(%rip), %eax
    je parent_fail_child_identity
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall
    test %rax, %rax
    js fail
    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall
    jmp fail

parent_fail_sse:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea parent_sse_fail_message(%rip), %rsi
    mov $parent_sse_fail_message_len, %rdx
    syscall
    jmp fail

parent_fail_alloc:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea parent_alloc_fail_message(%rip), %rsi
    mov $parent_alloc_fail_message_len, %rdx
    syscall
    jmp fail

parent_fail_identity:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea parent_identity_fail_message(%rip), %rsi
    mov $parent_identity_fail_message_len, %rdx
    syscall
    jmp fail

parent_fail_child_identity:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea parent_child_identity_fail_message(%rip), %rsi
    mov $parent_child_identity_fail_message_len, %rdx
    syscall
    jmp fail

child_path:
    mov %fs:0, %rax
    mov $TLS_MAGIC, %rdx
    cmp %rdx, %rax
    jne child_fail_tls

    mov $SYS_GETPID, %rax
    syscall
    mov %eax, child_seen_pid(%rip)
    mov $SYS_GETTID, %rax
    syscall
    mov %eax, child_seen_tid(%rip)
    mov parent_tid(%rip), %edx
    cmp %edx, %eax
    je child_fail_identity

    movdqa child_sse_value(%rip), %xmm0
    lea child_allocs(%rip), %r13
    mov $3, %r12
child_alloc_loop:
    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $PAGE_SIZE, %rsi
    mov $PROT_READ_WRITE, %rdx
    mov $MAP_PRIVATE_ANON, %r10
    mov $-1, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae child_fail_alloc
    mov %rax, -8(%r13,%r12,8)
    movl $0x4348494c, (%rax)
    mov $SYS_SCHED_YIELD, %rax
    syscall
    dec %r12
    jnz child_alloc_loop
    movl $1, child_alloc_done(%rip)

    mov $64, %r12
child_sse_yield:
    mov $SYS_SCHED_YIELD, %rax
    syscall
    movdqa %xmm0, %xmm1
    movdqa child_sse_value(%rip), %xmm2
    pcmpeqd %xmm2, %xmm1
    pmovmskb %xmm1, %eax
    cmp $0xffff, %eax
    jne child_fail_sse
    dec %r12
    jnz child_sse_yield

    # Exercise YMM state when the kernel has enabled XSAVE/AVX.  The CPUID
    # OSXSAVE bit makes this branch safe on CPUs where only SSE is available.
    mov $1, %eax
    cpuid
    test $(1 << 27), %ecx
    jz child_no_avx
    test $(1 << 28), %ecx
    jz child_no_avx
    vmovdqa child_avx_value(%rip), %ymm0
    mov $32, %r12
child_avx_yield:
    mov $SYS_SCHED_YIELD, %rax
    syscall
    vmovdqa %ymm0, %ymm1
    vmovdqa child_avx_value(%rip), %ymm2
    vpcmpeqd %ymm2, %ymm1, %ymm1
    vpmovmskb %ymm1, %eax
    cmp $-1, %eax
    jne child_fail_avx
    dec %r12
    jnz child_avx_yield
    vzeroupper

child_no_avx:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea child_message(%rip), %rsi
    mov $child_message_len, %rdx
    syscall
    test %rax, %rax
    js child_fail_write
    movl $1, child_done(%rip)
    mov $SYS_EXIT, %rax
    xor %rdi, %rdi
    syscall

child_fail:
    lea fail_message(%rip), %rsi
    mov $fail_message_len, %rdx
    jmp child_fail_report

child_fail_tls:
    lea tls_fail_message(%rip), %rsi
    mov $tls_fail_message_len, %rdx
    jmp child_fail_report

child_fail_identity:
    lea identity_fail_message(%rip), %rsi
    mov $identity_fail_message_len, %rdx
    jmp child_fail_report

child_fail_alloc:
    lea alloc_fail_message(%rip), %rsi
    mov $alloc_fail_message_len, %rdx
    jmp child_fail_report

child_fail_sse:
    lea sse_fail_message(%rip), %rsi
    mov $sse_fail_message_len, %rdx
    jmp child_fail_report

child_fail_avx:
    lea avx_fail_message(%rip), %rsi
    mov $avx_fail_message_len, %rdx
    jmp child_fail_report

child_fail_write:
    lea child_write_fail_message(%rip), %rsi
    mov $child_write_fail_message_len, %rdx

child_fail_report:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    syscall
    movl $1, child_done(%rip)
    mov $SYS_EXIT_GROUP, %rax
    mov $117, %rdi
    syscall
    jmp child_fail

fail:
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_message(%rip), %rsi
    mov $fail_message_len, %rdx
    syscall
    mov $SYS_EXIT_GROUP, %rax
    mov $118, %rdi
    syscall
    jmp fail

.section .data
.balign 8
parent_pid:
    .long 0
parent_tid:
    .long 0
parent_tid_arg:
    .long 0
child_tid_slot:
    .long 0
child_seen_pid:
    .long 0
child_seen_tid:
    .long 0
child_alloc_done:
    .long 0
parent_alloc_done:
    .long 0
child_done:
    .long 0
.balign 8
child_allocs:
    .quad 0, 0, 0
parent_allocs:
    .quad 0, 0, 0
child_tls:
    .quad TLS_MAGIC
.balign 16
child_sse_value:
    .long 0x11111111, 0x22222222, 0x33333333, 0x44444444
parent_sse_value:
    .long 0xaaaaaaaa, 0xbbbbbbbb, 0xcccccccc, 0xdddddddd
.balign 32
child_avx_value:
    .long 0x10101010, 0x20202020, 0x30303030, 0x40404040
    .long 0x50505050, 0x60606060, 0x70707070, 0x80808080

.section .rodata
start_message:
    .ascii "Vex ELF thread context smoke: start\n"
start_message_len = . - start_message
child_message:
    .ascii "Vex ELF thread context smoke: child\n"
child_message_len = . - child_message
done_message:
    .ascii "Vex ELF thread context smoke: done\n"
done_message_len = . - done_message
fail_message:
    .ascii "Vex ELF thread context smoke: failed\n"
fail_message_len = . - fail_message
tls_fail_message:
    .ascii "Vex ELF thread context smoke: TLS failed\n"
tls_fail_message_len = . - tls_fail_message
identity_fail_message:
    .ascii "Vex ELF thread context smoke: identity failed\n"
identity_fail_message_len = . - identity_fail_message
alloc_fail_message:
    .ascii "Vex ELF thread context smoke: allocation failed\n"
alloc_fail_message_len = . - alloc_fail_message
sse_fail_message:
    .ascii "Vex ELF thread context smoke: SSE failed\n"
sse_fail_message_len = . - sse_fail_message
avx_fail_message:
    .ascii "Vex ELF thread context smoke: AVX failed\n"
avx_fail_message_len = . - avx_fail_message
parent_sse_fail_message:
    .ascii "Vex ELF thread context smoke: parent SSE failed\n"
parent_sse_fail_message_len = . - parent_sse_fail_message
parent_alloc_fail_message:
    .ascii "Vex ELF thread context smoke: parent allocation overlap\n"
parent_alloc_fail_message_len = . - parent_alloc_fail_message
parent_identity_fail_message:
    .ascii "Vex ELF thread context smoke: parent identity failed\n"
parent_identity_fail_message_len = . - parent_identity_fail_message
parent_child_identity_fail_message:
    .ascii "Vex ELF thread context smoke: child identity failed\n"
parent_child_identity_fail_message_len = . - parent_child_identity_fail_message
parent_alloc_check_message:
    .ascii "Vex ELF thread context smoke: parent alloc check\n"
parent_alloc_check_message_len = . - parent_alloc_check_message
parent_sse_done_message:
    .ascii "Vex ELF thread context smoke: parent SSE done\n"
parent_sse_done_message_len = . - parent_sse_done_message
parent_clear_done_message:
    .ascii "Vex ELF thread context smoke: child tid clear done\n"
parent_clear_done_message_len = . - parent_clear_done_message
child_write_fail_message:
    .ascii "Vex ELF thread context smoke: child FD write failed\n"
child_write_fail_message_len = . - child_write_fail_message
