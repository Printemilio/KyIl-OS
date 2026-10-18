.text
# The wrappers capture the PE caller before entering the SysV C engine. No
# unwind of the native ELF runtime is misrepresented as a Windows PE frame.
.macro capture_caller
    sub $0x528,%rsp
    mov %rcx,0x4f0(%rsp)
    mov %rdx,0x4f8(%rsp)
    mov %r8,0x500(%rsp)
    mov %r9,0x508(%rsp)
    lea 32(%rsp),%rcx
    call wr_capture_context
    mov 0x528(%rsp),%rax
    mov %rax,0x118(%rsp)
    lea 0x530(%rsp),%rax
    mov %rax,0xb8(%rsp)
    mov 0x4f0(%rsp),%rax
    mov %rax,0xa0(%rsp)
    mov 0x4f8(%rsp),%rax
    mov %rax,0xa8(%rsp)
    mov 0x500(%rsp),%rax
    mov %rax,0xd8(%rsp)
    mov 0x508(%rsp),%rax
    mov %rax,0xe0(%rsp)
    mov 0x4f0(%rsp),%rdi
    mov 0x4f8(%rsp),%rsi
    mov 0x500(%rsp),%rdx
    mov 0x508(%rsp),%rcx
.endm
.global wr_raise_entry
.type wr_raise_entry,@function
wr_raise_entry:
    capture_caller
    lea 32(%rsp),%r8
    call wr_raise_api
    ud2
.global wr_unwind_entry
.type wr_unwind_entry,@function
wr_unwind_entry:
    capture_caller
    xor %r8d,%r8d
    xor %r9d,%r9d
    lea 32(%rsp),%rax
    mov %rax,(%rsp)
    call wr_unwind_api
    ud2
.global wr_unwind_ex_entry
.type wr_unwind_ex_entry,@function
wr_unwind_ex_entry:
    capture_caller
    mov 0x550(%rsp),%r8
    mov 0x558(%rsp),%r9
    lea 32(%rsp),%rax
    mov %rax,(%rsp)
    call wr_unwind_api
    ud2
.global wr_restore_context_raw
.type wr_restore_context_raw,@function
wr_restore_context_raw:
    # SysV RDI points to the validated private context. RIP was staged at
    # target RSP-8 before leaving a native fault event, when applicable.
    mov %rdi,%r11
    testl $8,0x30(%r11)
    jz 1f
    fxrstor64 0x100(%r11)
    ldmxcsr 0x34(%r11)
1:  mov 0x44(%r11),%eax
    push %rax
    popfq
    mov 0x80(%r11),%rcx
    mov 0x88(%r11),%rdx
    mov 0x90(%r11),%rbx
    mov 0xa0(%r11),%rbp
    mov 0xa8(%r11),%rsi
    mov 0xb0(%r11),%rdi
    mov 0xb8(%r11),%r8
    mov 0xc0(%r11),%r9
    mov 0xc8(%r11),%r10
    mov 0xd8(%r11),%r12
    mov 0xe0(%r11),%r13
    mov 0xe8(%r11),%r14
    mov 0xf0(%r11),%r15
    mov 0x98(%r11),%rsp
    sub $8,%rsp
    mov 0x78(%r11),%rax
    mov 0xd0(%r11),%r11
    ret
.section .note.GNU-stack,"",@progbits
