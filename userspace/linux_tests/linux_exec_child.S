.global _start

# Child payload for linux_pipe_exec_smoke. It intentionally uses only stdout and
# exit so the parent proves pipe/dup2/execve/wait4 around it.

.set SYS_WRITE, 1
.set SYS_EXIT, 60

.section .text
_start:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea payload(%rip), %rsi
    mov $payload_len, %rdx
    syscall
    cmp $payload_len, %rax
    jne fail

    mov $SYS_EXIT, %rax
    mov $43, %rdi
    syscall

fail:
    mov $SYS_EXIT, %rax
    mov $119, %rdi
    syscall

.section .rodata
payload:
    .ascii "VEXEXECOK"
payload_len = . - payload
