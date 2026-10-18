/* Independent SysV ABI adversary: the validator deliberately clobbers Windows
 * nonvolatile SIMD registers, which is legal for a SysV callee. No PE shim. */
.text
.global _start
_start:
    mov $0x1234,%rax
    mov $42,%rcx
    mov $7,%rdi
    mov $9,%rsi
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    movdqa expected(%rip),%xmm\n
    .endr
    call wr_cfg_check
    cmp $0x1234,%rax
    jne failed
    cmp $42,%rcx
    jne failed
    cmp $7,%rdi
    jne failed
    cmp $9,%rsi
    jne failed
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    pcmpeqb expected(%rip),%xmm\n
    pmovmskb %xmm\n,%eax
    cmp $65535,%eax
    jne failed
    .endr
    mov $1,%eax
    mov $1,%edi
    lea passed(%rip),%rsi
    mov $passed_len,%edx
    syscall
    xor %edi,%edi
    mov $60,%eax
    syscall
.global wr_cfg_allowed
wr_cfg_allowed:
    cmp $0x1234,%rdi
    jne failed
    mov $111,%ecx
    mov $222,%edi
    mov $333,%esi
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    pxor %xmm\n,%xmm\n
    .endr
    mov $1,%eax
    ret
.global wr_cfg_failure
wr_cfg_failure:
failed:
    mov $1,%edi
    mov $60,%eax
    syscall
.section .rodata
.balign 16
expected: .quad 0x1234567890abcdef,0xfedcba9876543210
passed: .ascii "windows-guard-abi: target / arguments / RDI-RSI / XMM0-15 PASS\n"
.set passed_len,.-passed
.section .note.GNU-stack,"",@progbits
