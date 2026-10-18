.global _start
.text
/* Independent Linux ABI probes for the system contracts used by browsers.
 * This executable is launched without NET_CONNECT. No test page or renderer. */
_start:
    mov $228,%eax
    xor %edi,%edi
    lea stamp(%rip),%rsi
    syscall
    test %rax,%rax
    jne fail
    cmpq $946684800,stamp(%rip)
    jb fail
    cmpq $1000000000,stamp+8(%rip)
    jae fail
    mov $201,%eax
    xor %edi,%edi
    syscall
    sub stamp(%rip),%rax
    cmp $2,%rax
    ja fail
    mov $228,%eax
    mov $99,%edi
    lea stamp(%rip),%rsi
    syscall
    cmp $-22,%rax
    jne fail
    /* AT_FDCWD is a signed 32-bit argument even when zero-extended. */
    mov $332,%eax
    mov $-100,%edi
    lea path(%rip),%rsi
    xor %edx,%edx
    mov $0x7ff,%r10d
    lea statx_buffer(%rip),%r8
    syscall
    test %rax,%rax
    jne fail
    cmpq $0,statx_buffer+40(%rip)
    je fail
    mov $332,%eax
    mov $-100,%edi
    mov $1,%esi
    xor %edx,%edx
    mov $0x7ff,%r10d
    lea statx_buffer(%rip),%r8
    syscall
    cmp $-14,%rax
    jne fail
    /* A file-backed pathname has never been touched by ring 3. stat must
     * materialize it safely, even after closing the original descriptor. */
    mov $2,%eax
    lea lazy_path_file(%rip),%rdi
    xor %esi,%esi
    syscall
    test %rax,%rax
    js fail
    mov %rax,%r13
    mov %rax,%r8
    mov $9,%eax
    xor %edi,%edi
    mov $4096,%esi
    mov $1,%edx
    mov $2,%r10d
    xor %r9d,%r9d
    syscall
    test %rax,%rax
    js fail
    mov %rax,%r12
    mov $3,%eax
    mov %r13,%rdi
    syscall
    mov $262,%eax
    mov $-100,%edi
    mov %r12,%rsi
    lea stat_buffer(%rip),%rdx
    xor %r10d,%r10d
    syscall
    test %rax,%rax
    jne fail
    cmpq $0,stat_buffer+48(%rip)
    je fail
    mov $262,%eax
    mov $-100,%edi
    mov $1,%esi
    lea stat_buffer(%rip),%rdx
    xor %r10d,%r10d
    syscall
    cmp $-14,%rax
    jne fail
    mov $269,%eax
    mov $-100,%edi
    mov %r12,%rsi
    xor %edx,%edx
    syscall
    test %rax,%rax
    jne fail
    mov $269,%eax
    mov $-100,%edi
    mov $1,%esi
    xor %edx,%edx
    syscall
    cmp $-14,%rax
    jne fail
    mov $439,%eax
    mov $-100,%edi
    mov %r12,%rsi
    xor %edx,%edx
    mov $0x100,%r10d
    syscall
    cmp $-38,%rax
    jne fail
    mov $11,%eax
    mov %r12,%rdi
    mov $4096,%esi
    syscall
    /* Network access is an explicit capability, not an incidental root UID. */
    mov $41,%eax
    mov $2,%edi
    mov $2,%esi
    xor %edx,%edx
    syscall
    cmp $-1,%rax
    jne fail
    mov $1,%eax
    mov $1,%edi
    lea success(%rip),%rsi
    mov $success_end-success,%edx
    syscall
    xor %edi,%edi
    jmp exit
fail:
    mov $1,%eax
    mov $1,%edi
    lea failure(%rip),%rsi
    mov $failure_end-failure,%edx
    syscall
    mov $1,%edi
exit:
    mov $231,%eax
    syscall
.section .rodata
lazy_path_file: .asciz "/usr/browser/stat-path.txt"
path: .asciz "/usr/browser/a.html"
success: .ascii "browser-system: UTC/statx/invalid-pointer/NET_CONNECT isolation PASS\n"
success_end:
failure: .ascii "browser-system: FAIL\n"
failure_end:
.bss
.align 16
stamp: .skip 16
statx_buffer: .skip 256
stat_buffer: .skip 144
.section .note.GNU-stack,"",@progbits
