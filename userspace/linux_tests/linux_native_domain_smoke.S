/* Untagged ELF => Linux personality. Exact explicit native wire alongside
   Linux WRITE=1; no native ELF note and no shared userspace dispatch helper. */
.global _start
.text
_start:
    movabs $0x5645584e00000001, %rax
    lea bulk(%rip), %rdi
    syscall
    test %rax,%rax
    jnz fail
    cmpq $0x41,result(%rip)
    jne fail
    cmpq $0,result+56(%rip)
    je fail
    mov $39,%eax
    syscall
    cmp %eax,result+8(%rip)
    jne fail
    movabs $0x5645584e000000c1,%rax
    mov $7,%edi
    mov $2,%esi
    syscall
    cmp $-1,%rax
    je fail
    mov %rax,counter(%rip)
    mov %rax,read_req(%rip)
    movabs $0x5645584e000000c3,%rax
    lea read_req(%rip),%rdi
    syscall
    test %rax,%rax
    jnz fail
    cmpq $7,value(%rip)
    jne fail
    movabs $0x5645584e000000c4,%rax
    mov counter(%rip),%rdi
    syscall
    test %rax,%rax
    jnz fail
    /* Historical Linux SHM bridge remains recognized (invalid cap is denied). */
    mov $0x4002,%eax
    xor %edi,%edi
    syscall
    cmp $-1,%rax
    jne fail
    movabs $0x5645584e00000001,%rax
    movabs $0xffff800000000000,%rdi
    syscall
    cmp $-1,%rax
    jne fail
    mov $1,%eax
    mov $1,%edi
    lea message(%rip),%rsi
    mov $message_len,%edx
    syscall
    cmp $message_len,%rax
    jne fail
    xor %edi,%edi
    jmp finish
fail:
    mov $1,%edi
finish:
    mov $231,%eax
    syscall
    ud2
.data
.balign 8
bulk: .quad 0x41,result
read_req: .quad 0,value,2,0
counter: .quad 0
value: .quad 0
message: .ascii "linux_native_domain: native BULK/counter + Linux WRITE and legacy SHM bridge ok\n"
.set message_len,.-message
.bss
.balign 8
result: .skip 256
.section .note.GNU-stack,"",@progbits
