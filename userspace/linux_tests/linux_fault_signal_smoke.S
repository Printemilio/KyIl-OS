.section .text
.global _start
_start:
    # Linux x86-64 kernel sigaction: handler, flags, restorer, 8-byte mask.
    mov $13, %eax
    mov $11, %edi
    lea fault_action(%rip), %rsi
    xor %edx, %edx
    mov $8, %r10d
    syscall
    test %rax, %rax
    jne fail

    mov $13, %eax
    mov $7, %edi
    lea fault_action(%rip), %rsi
    xor %edx, %edx
    mov $8, %r10d
    syscall
    test %rax, %rax
    jne fail

    # Keep SIGUSR1 blocked across the handler; SA_MASK adds SIGUSR2 while it
    # runs, and the synchronous SIGSEGV itself is blocked by default.
    mov $14, %eax
    mov $2, %edi
    lea initial_mask(%rip), %rsi
    xor %edx, %edx
    mov $8, %r10d
    syscall
    test %rax, %rax
    jne fail

    movq $1, fault_stage(%rip)
    movabs $0x1122334455667788, %rax
    xor %r12, %r12
    movabs $0x8877665544332211, %rbx
    movabs $0x123456789abcdef0, %rcx
    movabs $0x0fedcba987654321, %r11
fault_null:
    movq (%r12), %rax
fault_null_resume:
    pushfq
    pop %r15
    test $512, %r15
    jz fail
    movabs $0x1122334455667788, %r15
    cmp %r15, %rax
    jne fail
    movabs $0x8877665544332211, %r15
    cmp %r15, %rbx
    jne fail
    movabs $0x123456789abcdef0, %r15
    cmp %r15, %rcx
    jne fail
    movabs $0x0fedcba987654321, %r15
    cmp %r15, %r11
    jne fail

    movq $2, fault_stage(%rip)
    lea protected_text(%rip), %r12
fault_readonly:
    movb $0x90, (%r12)
fault_readonly_resume:

    # Allocate writable, non-executable memory, then execute from it.  This
    # must be a protection SIGSEGV with the instruction-fetch address in si_addr.
    mov $9, %eax
    xor %edi, %edi
    mov $4096, %esi
    mov $3, %edx
    mov $0x22, %r10d
    mov $-1, %r8
    xor %r9d, %r9d
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    movb $0xc3, (%r12)
    mov $10, %eax
    mov %r12, %rdi
    mov $4096, %esi
    mov $3, %edx
    syscall
    test %rax, %rax
    jne fail
    movq $3, fault_stage(%rip)
fault_nx:
    call *%r12
fault_nx_resume:
    add $8, %rsp
    mov $1, %eax
    mov $1, %edi
    lea stage_nx_ok(%rip), %rsi
    mov $stage_nx_ok_len, %edx
    syscall

    # A page wholly beyond a one-byte file mapping is SIGBUS/BUS_ADRERR.
    mov $257, %eax
    mov $-100, %edi
    lea bus_path(%rip), %rsi
    mov $0x242, %edx
    mov $0600, %r10d
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r13
    mov $1, %eax
    mov %r13, %rdi
    lea one_byte(%rip), %rsi
    mov $1, %edx
    syscall
    cmp $1, %rax
    jne fail
    mov $1, %eax
    mov $1, %edi
    lea stage_file_ok(%rip), %rsi
    mov $stage_file_ok_len, %edx
    syscall
    mov $9, %eax
    xor %edi, %edi
    mov $8192, %esi
    mov $3, %edx
    mov $2, %r10d
    mov %r13, %r8
    xor %r9d, %r9d
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r14
    mov $3, %eax
    mov %r13, %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $1, %eax
    mov $1, %edi
    lea stage_mapping_ok(%rip), %rsi
    mov $stage_mapping_ok_len, %edx
    syscall
    mov %r14, %r12
    add $4096, %r12
    movq $4, fault_stage(%rip)
fault_bus:
    movb (%r12), %al
fault_bus_resume:
    mov $1, %eax
    mov $1, %edi
    lea stage_bus_ok(%rip), %rsi
    mov $stage_bus_ok_len, %edx
    syscall
    mov $11, %eax
    mov %r14, %rdi
    mov $8192, %esi
    syscall
    test %rax, %rax
    jne fail

    # rt_sigreturn must restore the pre-delivery thread mask.
    mov $14, %eax
    mov $2, %edi
    xor %esi, %esi
    lea current_mask(%rip), %rdx
    mov $8, %r10d
    syscall
    test %rax, %rax
    jne fail
    cmpq $0x200, current_mask(%rip)
    jne fail
    cmpq $4, handler_count(%rip)
    jne fail
    cmpq $0, handler_failed(%rip)
    jne fail

    mov $1, %eax
    mov $1, %edi
    lea passed(%rip), %rsi
    mov $passed_len, %edx
    syscall
    xor %edi, %edi
    jmp exit

fail:
    mov $1, %eax
    mov $1, %edi
    lea failed(%rip), %rsi
    mov $failed_len, %edx
    syscall
    mov $1, %edi
exit:
    mov $60, %eax
    syscall
    ud2

fault_handler:
    # Save pointers/values in callee-saved registers while querying the mask.
    mov %rdi, %r15
    mov %rsi, %r13
    mov %rdx, %r12
    mov fault_stage(%rip), %r14
    mov $14, %eax
    mov $2, %edi
    xor %esi, %esi
    lea handler_mask(%rip), %rdx
    mov $8, %r10d
    syscall
    test %rax, %rax
    jne handler_bad
    cmp $4, %r14d
    je .Lcheck_bus_mask
    cmpq $0xe00, handler_mask(%rip)
    jne handler_bad
    jmp .Lcheck_saved_mask
.Lcheck_bus_mask:
    cmpq $0xa40, handler_mask(%rip)
    jne handler_bad
.Lcheck_saved_mask:
    cmpq $0x200, 296(%r12)      # uc_sigmask before handler mask changes
    jne handler_bad
    cmpq $1, %r14
    je .Lcheck_segv
    cmpq $2, %r14
    je .Lcheck_ro
    cmpq $3, %r14
    je .Lcheck_nx
    cmpq $4, %r14
    je .Lcheck_bus
    jmp handler_bad

.Lcheck_segv:
    cmp $11, %r15d
    jne handler_bad
    cmpl $1, 8(%r13)            # SEGV_MAPERR
    jne handler_bad
    cmpq $0, 16(%r13)
    jne handler_bad
    lea fault_null(%rip), %rax
    jmp .Lcheck_rip
.Lcheck_ro:
    cmp $11, %r15d
    jne handler_bad
    cmpl $2, 8(%r13)            # SEGV_ACCERR
    jne handler_bad
    lea protected_text(%rip), %rax
    cmpq %rax, 16(%r13)
    jne handler_bad
    lea fault_readonly(%rip), %rax
    jmp .Lcheck_rip
.Lcheck_nx:
    cmp $11, %r15d
    jne handler_bad
    cmpl $2, 8(%r13)            # instruction-fetch protection fault
    jne handler_bad
    # The target is the saved R12 register (greg index 4 is R12).
    mov 72(%r12), %rax
    cmp %rax, 16(%r13)
    jne handler_bad
    # The CPU reports the attempted instruction-fetch target as RIP for this
    # protection fault.  The target is also si_addr; the handler advances RIP
    # to fault_nx_resume below and the test repairs CALL's pushed return slot.
    jmp .Lcheck_rip
.Lcheck_bus:
    cmp $7, %r15d
    jne handler_bad
    cmpl $2, 8(%r13)            # BUS_ADRERR
    jne handler_bad
    mov 72(%r12), %rax         # faulting address was in R12
    cmp %rax, 16(%r13)
    jne handler_bad
    lea fault_bus(%rip), %rax

.Lcheck_rip:
    cmp %rax, 168(%r12)         # REG_RIP
    jne handler_bad
    cmpq $1, fault_stage(%rip)
    jne .Lcheck_stage_rax
    movabs $0x1122334455667788, %rcx
    cmp %rcx, 144(%r12)         # REG_RAX from null-load fault
    jne handler_bad
    # Full-context restore must preserve RCX and R11 across rt_sigreturn.
    movabs $0x123456789abcdef0, %rcx
    cmp %rcx, 152(%r12)         # REG_RCX
    jne handler_bad
    movabs $0x0fedcba987654321, %rcx
    cmp %rcx, 64(%r12)          # REG_R11
    jne handler_bad
.Lcheck_stage_rax:
    lea fault_null_resume(%rip), %rcx
    cmpq $1, fault_stage(%rip)
    je .Lset_resume
    lea fault_readonly_resume(%rip), %rcx
    cmpq $2, fault_stage(%rip)
    je .Lset_resume
    lea fault_nx_resume(%rip), %rcx
    cmpq $3, fault_stage(%rip)
    je .Lset_resume
    lea fault_bus_resume(%rip), %rcx
.Lset_resume:
    mov %rcx, 168(%r12)
    # A modifiable Linux context cannot grant ring 3 an interrupt-disable
    # capability. The kernel must restore IF even when this bit is cleared.
    andq $-513, 176(%r12)
    incq handler_count(%rip)
    ret

handler_bad:
    movq $1, handler_failed(%rip)
    ret

linux_sigreturn:
    mov $15, %eax
    syscall
    ud2

protected_text:
    ret

.section .rodata
passed: .ascii "Vex ELF fault-signal smoke: done\n"
passed_len = . - passed
failed: .ascii "Vex ELF fault-signal smoke: failed\n"
failed_len = . - failed
stage_nx_ok: .ascii "fault-signal: NX handler returned\n"
stage_nx_ok_len = . - stage_nx_ok
stage_file_ok: .ascii "fault-signal: backing file prepared\n"
stage_file_ok_len = . - stage_file_ok
stage_mapping_ok: .ascii "fault-signal: file mapping created\n"
stage_mapping_ok_len = . - stage_mapping_ok
stage_bus_ok: .ascii "fault-signal: SIGBUS handler returned\n"
stage_bus_ok_len = . - stage_bus_ok
bus_path: .asciz "/tmp/vex_fault_signal_bus"
one_byte: .byte 0x5a

.section .data
.align 8
fault_action:
    .quad fault_handler
    .quad 0x04000004
    .quad linux_sigreturn
    .quad 0x800               # SA_MASK: block SIGUSR2 during handler
    .quad 0
initial_mask: .quad 0x200    # keep SIGUSR1 blocked before the fault
current_mask: .quad 0
handler_mask: .quad 0
fault_stage: .quad 0
handler_count: .quad 0
handler_failed: .quad 0
