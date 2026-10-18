.global _start
.section .text
/* The kernel supplies N and moves the parked threads to distinct CPUs. All
   threads retain ONE process identity, VA allocator, CR3 and descriptor table. */
_start:
    mov $9,%eax
    mov $0x600000000,%rdi
    mov $8192,%esi
    mov $3,%edx
    mov $0x32,%r10d
    mov $-1,%r8
    xor %r9d,%r9d
    syscall
    mov $0x600000000,%r13
    cmp %r13,%rax
    jne fail
wait_count:
    cmpl $0,(%r13)
    jne create
    mov $24,%eax
    syscall
    jmp wait_count
create:
    xor %r12d,%r12d
create_thread:
    mov $9,%eax
    xor %edi,%edi
    mov $32768,%esi
    mov $3,%edx
    mov $0x22,%r10d
    mov $-1,%r8
    xor %r9d,%r9d
    syscall
    cmp $-4095,%rax
    jae fail
    mov %r12,(%rax)
    lea 32768(%rax),%rsi
    mov %rax,%r8
    mov $56,%eax
    mov $0x01290f00,%edi /* VM/FILES/THREAD/SETTLS/CHILD_TID/CLEARTID */
    xor %edx,%edx
    lea 576(%r13,%r12,4),%r10
    syscall
    test %rax,%rax
    js fail
    jz worker
    mov %eax,64(%r13,%r12,4)
    inc %r12d
    cmp (%r13),%r12d
    jb create_thread
    movl $1,4(%r13)
wait_placement:
    cmpl $0,12(%r13)
    jne release_workers
    mov $24,%eax
    syscall
    jmp wait_placement
release_workers:
    movl $1,8(%r13)
    mov $202,%eax
    lea 8(%r13),%rdi
    mov $129,%esi
    mov $0x7fffffff,%edx
    syscall
wait_compute_ready:
    cmpl $0,32(%r13)
    jne fail
    mov 40(%r13),%edx
    cmp (%r13),%edx
    je wait_compute_start
    mov $202,%eax
    lea 40(%r13),%rdi
    mov $128,%esi
    xor %r10d,%r10d
    xor %r8d,%r8d
    xor %r9d,%r9d
    syscall
    jmp wait_compute_ready
wait_compute_start:
    cmpl $0,44(%r13)
    jne release_compute
    mov $24,%eax
    syscall
    jmp wait_compute_start
release_compute:
    mov $202,%eax
    lea 44(%r13),%rdi
    mov $129,%esi
    mov $0x7fffffff,%edx
    syscall
wait_done:
    mov 20(%r13),%edx
    cmp (%r13),%edx
    je join_threads
    mov $202,%eax
    lea 20(%r13),%rdi
    mov $128,%esi
    xor %r10d,%r10d
    xor %r8d,%r8d
    xor %r9d,%r9d
    syscall
    jmp wait_done
join_threads:
    xor %r12d,%r12d
join_one:
    mov 576(%r13,%r12,4),%edx
    test %edx,%edx
    jz joined
    mov $202,%eax
    lea 576(%r13,%r12,4),%rdi
    mov $128,%esi
    syscall
    jmp join_one
joined:
    inc %r12d
    cmp (%r13),%r12d
    jb join_one
    cmpl $0,32(%r13)
    jne fail
    mov $231,%eax
    xor %edi,%edi
    syscall
worker:
    movq %r12,%xmm15
park:
    cmpl $0,8(%r13)
    jne running
    mov $202,%eax
    lea 8(%r13),%rdi
    mov $128,%esi
    xor %edx,%edx
    xor %r10d,%r10d
    xor %r8d,%r8d
    xor %r9d,%r9d
    syscall
    jmp park
running:
    /* Every CPU faults the SAME initially untouched page. A peer that wins
       materialization must not make the losing fault look like SIGSEGV. */
    lock incl 4096(%r13)
    mov $1,%eax
    cpuid
    shr $24,%ebx
    cmpl $-1,1088(%r13,%r12,4)
    je cpu_allowed
    cmp 1088(%r13,%r12,4),%ebx
    jne worker_fail
cpu_allowed:
    mov $9,%eax
    xor %edi,%edi
    mov $12288,%esi
    mov $3,%edx
    mov $0x22,%r10d
    mov $-1,%r8
    xor %r9d,%r9d
    syscall
    cmp $-4095,%rax
    jae worker_fail
    mov %rax,%r14
    mov %rax,1600(%r13,%r12,8)
    mov %r12,(%rax)
    lock incl 16(%r13)
    mov $202,%eax
    lea 16(%r13),%rdi
    mov $129,%esi
    mov $0x7fffffff,%edx
    syscall
wait_allocations:
    mov 16(%r13),%edx
    cmp (%r13),%edx
    je check_allocations
    mov $202,%eax
    lea 16(%r13),%rdi
    mov $128,%esi
    syscall
    jmp wait_allocations
check_allocations:
    xor %ebx,%ebx
check_peer:
    cmp %r12d,%ebx
    je next_peer
    cmp 1600(%r13,%rbx,8),%r14
    je worker_fail
next_peer:
    inc %ebx
    cmp (%r13),%ebx
    jb check_peer
    mov $11,%eax
    mov %r14,%rdi
    mov $12288,%esi
    syscall
    test %rax,%rax
    jnz worker_fail
    mov $32,%r15d
mapping_cycle:
    mov $9,%eax
    xor %edi,%edi
    mov $12288,%esi
    mov $3,%edx
    mov $0x22,%r10d
    mov $-1,%r8
    xor %r9d,%r9d
    syscall
    cmp $-4095,%rax
    jae worker_fail
    mov %rax,%r14
    mov $0xc300001973b8,%rax /* mov $0x1973,eax; ret */
    mov %rax,(%r14)
    mov $10,%eax
    mov %r14,%rdi
    mov $4096,%esi
    mov $5,%edx /* RW -> RX, never RWX */
    syscall
    test %rax,%rax
    jnz worker_fail
    call *%r14
    cmp $0x1973,%eax
    jne worker_fail
    mov %r12,4096(%r14)
    mov $24,%eax
    syscall
    movq %xmm15,%rax
    cmp %r12,%rax
    jne worker_fail
    cmp %r12,%fs:0
    jne worker_fail
    mov $11,%eax
    mov %r14,%rdi
    mov $12288,%esi
    syscall
    test %rax,%rax
    jnz worker_fail
    dec %r15d
    jnz mapping_cycle
    lock incl 40(%r13)
    mov $202,%eax
    lea 40(%r13),%rdi
    mov $129,%esi
    mov $0x7fffffff,%edx
    syscall
wait_compute:
    cmpl $0,44(%r13)
    jne compute
    mov $202,%eax
    lea 44(%r13),%rdi
    mov $128,%esi
    xor %edx,%edx
    xor %r10d,%r10d
    xor %r8d,%r8d
    xor %r9d,%r9d
    syscall
    jmp wait_compute
compute:
    mov 24(%r13),%ecx
    cmp 28(%r13),%r12d
    jae compute_count
    inc %ecx
compute_count:
    mov $0x1973,%eax
compute_loop:
    mov %rax,%rdx
    shl $13,%rdx
    xor %rdx,%rax
    mov %rax,%rdx
    shr $7,%rdx
    xor %rdx,%rax
    mov %rax,%rdx
    shl $17,%rdx
    xor %rdx,%rax
    dec %ecx
    jnz compute_loop
    mov %rax,2624(%r13,%r12,8)
    lfence
    rdtsc
    shl $32,%rdx
    or %rdx,%rax
    /* Initial allocation addresses are dead after the compute barrier. */
    mov %rax,1600(%r13,%r12,8)
worker_done:
    lock incl 20(%r13)
    mov $202,%eax
    lea 20(%r13),%rdi
    mov $129,%esi
    mov $0x7fffffff,%edx
    syscall
    mov $60,%eax
    xor %edi,%edi
    syscall
worker_fail:
    lock incl 32(%r13)
    jmp worker_done
fail:
    mov $231,%eax
    mov $92,%edi
    syscall
.section .note.GNU-stack,"",@progbits
