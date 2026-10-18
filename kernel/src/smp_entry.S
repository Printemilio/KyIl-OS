.section .text
.global smp_ap_entry
.type smp_ap_entry,@function
smp_ap_entry:
    cli
    cld
    mov 24(%rdi),%rdi       # limine_mp_info.extra_argument -> CPU
    mov $0xc0000080,%ecx
    rdmsr
    or $0x800,%eax           # NXE before walking our NX stack mappings
    wrmsr
    mov 8(%rdi),%rax
    mov 0(%rdi),%rdx
    mov 16(%rdi),%rdi
    mov %rax,%cr3
    mov %rdx,%rsp
    xor %rbp,%rbp
    call smp_ap_main
    ud2
.size smp_ap_entry,.-smp_ap_entry
.section .note.GNU-stack,"",@progbits
