.section .text
.global user_enter
.type user_enter, @function
.extern scheduler_user_iret_fcw_probe

# void user_enter(uint64_t entry, uint64_t user_rsp)
# rdi = user RIP, rsi = user RSP.
user_enter:
    cli                         # GS/IRET transition must not admit a device IRQ
    /* Linux leaves general registers unspecified at process entry.  VexOS
       starts a fresh image with deterministic zeroed registers, while keeping
       the entry point and stack in scratch registers for iretq. */
    mov %rdi, %r12
    mov %rsi, %r13
    xor %eax, %eax
    xor %ebp, %ebp
    xor %ebx, %ebx
    xor %ecx, %ecx
    xor %edx, %edx
    xor %esi, %esi
    xor %edi, %edi
    xor %r8d, %r8d
    xor %r9d, %r9d
    xor %r10d, %r10d
    xor %r11d, %r11d
    xor %r14d, %r14d
    xor %r15d, %r15d
    mov %r13, %rsi
    mov %r12, %rdi
    # The kernel keeps its per-CPU state in GS_BASE.  Make the user GS base
    # current before iretq; SYSCALL will swap back on the next entry.
    swapgs
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es

    pushq $0x23
    pushq %rsi
    pushfq
    popq %rax
    orq $0x200, %rax
    pushq %rax
    pushq $0x2b
    pushq %rdi
    xor %r12d, %r12d
    xor %r13d, %r13d
    /* Temporary passive probe: capture the x87 control word at the last
       kernel instruction before entering the user image. */
    fnstcw scheduler_user_iret_fcw_probe(%rip)
    iretq
