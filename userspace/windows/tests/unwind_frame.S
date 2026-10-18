.data
.global unwind_saved_rbx
unwind_saved_rbx: .quad 0
.text
.global unwind_fixture
.seh_proc unwind_fixture
unwind_fixture:
    mov %rbx,unwind_saved_rbx(%rip)
    push %rbx
    .seh_pushreg %rbx
    .global unwind_after_push
unwind_after_push:
    sub $64,%rsp
    .seh_stackalloc 64
    .seh_endprologue
    mov %rcx,32(%rsp)
    mov %rdx,40(%rsp)
    mov $0x39175,%ebx
    mov %rdx,%rcx
    call *__imp_RtlCaptureContext(%rip)
    mov 40(%rsp),%rcx
    call *32(%rsp)
    add $64,%rsp
    pop %rbx
    ret
.seh_endproc
