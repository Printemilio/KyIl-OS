.section .text
.global _start
_start:
    # RWX cannot be represented by either Vex JIT alias. Linux mmap must
    # report a real denial rather than return the writable/NX address.
    mov $9, %eax
    xor %edi, %edi
    mov $4096, %esi
    mov $7, %edx                 # PROT_READ | PROT_WRITE | PROT_EXEC
    mov $0x22, %r10d              # MAP_PRIVATE | MAP_ANONYMOUS
    mov $-1, %r8
    xor %r9d, %r9d
    syscall
    cmp $-13, %rax               # -EACCES
    jne fail

    # A single-address W^X transition is supported: write while RW/NX, then
    # switch the same mapping to RX/RO before execution.
    mov $9, %eax
    xor %edi, %edi
    mov $4096, %esi
    mov $3, %edx                 # PROT_READ | PROT_WRITE
    mov $0x22, %r10d
    mov $-1, %r8
    xor %r9d, %r9d
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12

    # mov eax, 42; ret
    movb $0xb8, 0(%r12)
    movb $42, 1(%r12)
    movb $0, 2(%r12)
    movb $0, 3(%r12)
    movb $0, 4(%r12)
    movb $0xc3, 5(%r12)

    # mprotect must reject W+X without changing the existing RW/NX mapping.
    mov $10, %eax
    mov %r12, %rdi
    mov $4096, %esi
    mov $7, %edx
    syscall
    cmp $-13, %rax
    jne fail
    movb $42, 1(%r12)            # still writable after the rejected request

    mov $10, %eax
    mov %r12, %rdi
    mov $4096, %esi
    mov $5, %edx                 # PROT_READ | PROT_EXEC
    syscall
    test %rax, %rax
    jne fail
    call *%r12
    cmp $42, %eax
    jne fail

    mov $11, %eax
    mov %r12, %rdi
    mov $4096, %esi
    syscall
    test %rax, %rax
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

.section .rodata
passed: .ascii "Vex ELF JIT W_X smoke: mmap denied, RW to RX passed\n"
.set passed_len, .-passed
failed: .ascii "Vex ELF JIT W_X smoke: failed\n"
.set failed_len, .-failed
