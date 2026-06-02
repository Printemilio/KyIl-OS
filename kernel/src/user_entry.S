.section .text
.global user_enter
.type user_enter, @function

# void user_enter(uint64_t entry, uint64_t user_rsp)
# rdi = user RIP, rsi = user RSP.
user_enter:
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
    iretq
