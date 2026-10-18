.global _start

# File-backed mmap smoke:
# - create/write a file through Linux syscalls
# - mmap it MAP_PRIVATE|PROT_READ using the fd
# - verify bytes are actually backed by the file content

.set SYS_READ, 0
.set SYS_WRITE, 1
.set SYS_OPEN, 2
.set SYS_CLOSE, 3
.set SYS_FSTAT, 5
.set SYS_LSEEK, 8
.set SYS_MMAP, 9
.set SYS_MUNMAP, 11
.set SYS_PREAD64, 17
.set SYS_PWRITE64, 18
.set SYS_FTRUNCATE, 77
.set SYS_UNLINK, 87
.set SYS_EXIT_GROUP, 231

.set O_RDWR_CREAT_TRUNC, 0x242
.set PROT_READ, 1
.set PROT_READ_WRITE, 3
.set MAP_PRIVATE, 2
.set MAP_SHARED, 1

.section .text
_start:
    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea start_message(%rip), %rsi
    mov $start_message_len, %rdx
    syscall

    mov $SYS_OPEN, %rax
    lea path(%rip), %rdi
    mov $O_RDWR_CREAT_TRUNC, %rsi
    mov $0644, %rdx
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12

    mov $SYS_WRITE, %rax
    mov %r12, %rdi
    lea payload(%rip), %rsi
    mov $payload_len, %rdx
    syscall
    cmp $payload_len, %rax
    jne close_fail

    # ftruncate must change the file size, not just move the file offset.
    # Verify it with fstat, then touch the last byte of a shared mapping.
    mov $SYS_FTRUNCATE, %rax
    mov %r12, %rdi
    mov $8192, %rsi
    syscall
    test %rax, %rax
    js close_fail
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea stage_ftruncate(%rip), %rsi
    mov $stage_ftruncate_len, %rdx
    syscall

    mov $SYS_FSTAT, %rax
    mov %r12, %rdi
    lea stat_buffer(%rip), %rsi
    syscall
    test %rax, %rax
    js close_fail
    cmpq $8192, stat_buffer+48(%rip)
    jne close_fail
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea stage_fstat(%rip), %rsi
    mov $stage_fstat_len, %rdx
    syscall

    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $8192, %rsi
    mov $PROT_READ_WRITE, %rdx
    mov $MAP_SHARED, %r10
    mov %r12, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae close_fail
    mov %rax, %r15
    movb $0x5a, 8191(%r15)
    cmpb $0x5a, 8191(%r15)
    jne shared_unmap_fail
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea stage_shared(%rip), %rsi
    mov $stage_shared_len, %rdx
    syscall

    movb $'A', failure_stage(%rip)
    mov $SYS_MUNMAP, %rax
    mov %r15, %rdi
    mov $8192, %rsi
    syscall
    test %rax, %rax
    js close_fail

    # Verify the original bytes while they still exist; the shrink test below
    # intentionally discards bytes after offset 4.
    movb $'B', failure_stage(%rip)
    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $4096, %rsi
    mov $PROT_READ, %rdx
    mov $MAP_PRIVATE, %r10
    mov %r12, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae close_fail
    mov %rax, %r14
    movb $'C', failure_stage(%rip)
    mov (%r14), %rbx
    mov payload(%rip), %rcx
    cmp %rcx, %rbx
    jne private_unmap_fail
    movb $'D', failure_stage(%rip)
    mov $SYS_MUNMAP, %rax
    mov %r14, %rdi
    mov $4096, %rsi
    syscall
    test %rax, %rax
    js close_fail

    # Shrinking and re-extending must not reveal discarded bytes.
    movb $'E', failure_stage(%rip)
    mov $SYS_FTRUNCATE, %rax
    mov %r12, %rdi
    mov $4, %rsi
    syscall
    test %rax, %rax
    js close_fail
    movb $'F', failure_stage(%rip)
    mov $SYS_FTRUNCATE, %rax
    mov %r12, %rdi
    mov $8192, %rsi
    syscall
    test %rax, %rax
    js close_fail
    movb $'G', failure_stage(%rip)
    mov $SYS_PREAD64, %rax
    mov %r12, %rdi
    lea zero_check(%rip), %rsi
    mov $1, %rdx
    mov $4, %r10
    syscall
    cmp $1, %rax
    jne close_fail
    cmpb $0, zero_check(%rip)
    jne close_fail
    movb $'H', failure_stage(%rip)

    # Positional I/O must leave the shared descriptor cursor untouched.
    movb $'a', failure_stage(%rip)
    mov $SYS_LSEEK, %rax
    mov %r12, %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne close_fail

    movb $'b', failure_stage(%rip)
    mov $SYS_PWRITE64, %rax
    mov %r12, %rdi
    lea positional_patch(%rip), %rsi
    mov $1, %rdx
    mov $1, %r10
    syscall
    cmp $1, %rax
    jne close_fail

    movb $'c', failure_stage(%rip)
    mov $SYS_LSEEK, %rax
    mov %r12, %rdi
    xor %rsi, %rsi
    mov $1, %rdx
    syscall
    test %rax, %rax
    jne close_fail

    movb $'d', failure_stage(%rip)
    mov $SYS_PREAD64, %rax
    mov %r12, %rdi
    lea zero_check(%rip), %rsi
    mov $1, %rdx
    mov $1, %r10
    syscall
    cmp $1, %rax
    jne close_fail
    cmpb $'X', zero_check(%rip)
    jne close_fail

    movb $'e', failure_stage(%rip)
    mov $SYS_LSEEK, %rax
    mov %r12, %rdi
    xor %rsi, %rsi
    mov $1, %rdx
    syscall
    test %rax, %rax
    jne close_fail

    movb $'f', failure_stage(%rip)
    mov $SYS_PWRITE64, %rax
    mov %r12, %rdi
    lea payload+1(%rip), %rsi
    mov $1, %rdx
    mov $1, %r10
    syscall
    cmp $1, %rax
    jne close_fail

    movb $'g', failure_stage(%rip)
    mov $SYS_LSEEK, %rax
    mov %r12, %rdi
    xor %rsi, %rsi
    mov $1, %rdx
    syscall
    test %rax, %rax
    jne close_fail

    # A positional write beyond EOF must create a zero-filled sparse hole.
    movb $'h', failure_stage(%rip)
    mov $SYS_FTRUNCATE, %rax
    mov %r12, %rdi
    mov $4, %rsi
    syscall
    test %rax, %rax
    js close_fail

    movb $'i', failure_stage(%rip)
    mov $SYS_PWRITE64, %rax
    mov %r12, %rdi
    lea hole_marker(%rip), %rsi
    mov $1, %rdx
    mov $16, %r10
    syscall
    cmp $1, %rax
    jne close_fail

    mov $12, %ecx
    lea hole_check(%rip), %rdi
    mov $0xa5, %al
    rep stosb
    movb $'j', failure_stage(%rip)
    mov $SYS_PREAD64, %rax
    mov %r12, %rdi
    lea hole_check(%rip), %rsi
    mov $12, %rdx
    mov $4, %r10
    syscall
    cmp $12, %rax
    jne close_fail
    movb $'k', failure_stage(%rip)
    mov $12, %ecx
    lea hole_check(%rip), %rsi
.Lhole_zero_check:
    cmpb $0, (%rsi)
    jne close_fail
    inc %rsi
    loop .Lhole_zero_check

    movb $'l', failure_stage(%rip)
    mov $SYS_PREAD64, %rax
    mov %r12, %rdi
    lea zero_check(%rip), %rsi
    mov $1, %rdx
    mov $16, %r10
    syscall
    cmp $1, %rax
    jne close_fail
    cmpb $'Y', zero_check(%rip)
    jne close_fail

    movb $'m', failure_stage(%rip)
    mov $SYS_LSEEK, %rax
    mov %r12, %rdi
    xor %rsi, %rsi
    mov $1, %rdx
    syscall
    test %rax, %rax
    jne close_fail

    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea stage_positional(%rip), %rsi
    mov $stage_positional_len, %rdx
    syscall

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea ftruncate_message(%rip), %rsi
    mov $ftruncate_message_len, %rdx
    syscall

    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall

    # A 128 MiB file-backed reservation must remain sparse.  Touch only the
    # first and last page; the kernel trace records that physical pages are
    # committed at those faults rather than during mmap().
    movb $'I', failure_stage(%rip)
    mov $SYS_OPEN, %rax
    lea large_path(%rip), %rdi
    mov $O_RDWR_CREAT_TRUNC, %rsi
    mov $0600, %rdx
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r13

    # Give the sparse file a real 128 MiB logical length. Mapping a short
    # libc file for 128 MiB and touching its last page correctly raises SIGBUS.
    movb $'J', failure_stage(%rip)
    mov $SYS_FTRUNCATE, %rax
    mov %r13, %rdi
    mov $0x08000000, %rsi
    syscall
    test %rax, %rax
    js large_close_fail

    mov $SYS_FSTAT, %rax
    mov %r13, %rdi
    lea stat_buffer(%rip), %rsi
    syscall
    test %rax, %rax
    js large_close_fail
    cmpq $0x08000000, stat_buffer+48(%rip)
    jne large_close_fail

    mov $SYS_MMAP, %rax
    xor %rdi, %rdi
    mov $0x08000000, %rsi
    mov $PROT_READ, %rdx
    mov $MAP_PRIVATE, %r10
    mov %r13, %r8
    xor %r9, %r9
    syscall
    cmp $-4095, %rax
    jae large_close_fail
    mov %rax, %r14
    mov (%r14), %rbx
    mov 0x07fff000(%r14), %rbx

    mov $SYS_MUNMAP, %rax
    mov %r14, %rdi
    mov $0x08000000, %rsi
    syscall
    test %rax, %rax
    js large_close_fail
    mov $SYS_CLOSE, %rax
    mov %r13, %rdi
    syscall
    mov $SYS_UNLINK, %rax
    lea large_path(%rip), %rdi
    syscall
    test %rax, %rax
    js fail

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea lazy_large_message(%rip), %rsi
    mov $lazy_large_message_len, %rdx
    syscall

    mov $SYS_WRITE, %rax
    mov $1, %rdi
    lea done_message(%rip), %rsi
    mov $done_message_len, %rdx
    syscall

    mov $SYS_EXIT_GROUP, %rax
    xor %rdi, %rdi
    syscall

close_fail:
    mov %rax, failure_return(%rip)
    mov $SYS_CLOSE, %rax
    mov %r12, %rdi
    syscall
    jmp fail

shared_unmap_fail:
    mov $SYS_MUNMAP, %rax
    mov %r15, %rdi
    mov $8192, %rsi
    syscall
    jmp close_fail

private_unmap_fail:
    mov $SYS_MUNMAP, %rax
    mov %r14, %rdi
    mov $4096, %rsi
    syscall
    jmp close_fail

large_close_fail:
    mov %rax, failure_return(%rip)
    mov $SYS_CLOSE, %rax
    mov %r13, %rdi
    syscall
    mov $SYS_UNLINK, %rax
    lea large_path(%rip), %rdi
    syscall
    jmp fail

fail:
    mov failure_return(%rip), %rax
    lea failure_return_hex(%rip), %rdi
    mov $16, %ecx
.Lfailure_hex:
    rol $4, %rax
    mov %al, %dl
    and $15, %dl
    add $'0', %dl
    cmp $'9', %dl
    jbe .Lfailure_digit
    add $39, %dl
.Lfailure_digit:
    mov %dl, (%rdi)
    inc %rdi
    loop .Lfailure_hex
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea failure_detail(%rip), %rsi
    mov $failure_detail_len, %rdx
    syscall
    mov $SYS_WRITE, %rax
    mov $2, %rdi
    lea fail_message(%rip), %rsi
    mov $fail_message_len, %rdx
    syscall

    mov $SYS_EXIT_GROUP, %rax
    mov $117, %rdi
    syscall

.section .rodata
start_message:
    .ascii "Vex ELF file-mmap smoke: start\n"
start_message_len = . - start_message
done_message:
    .ascii "Vex ELF file-mmap smoke: done\n"
done_message_len = . - done_message
lazy_large_message:
    .ascii "Vex ELF file-mmap smoke: lazy-large done\n"
lazy_large_message_len = . - lazy_large_message
fail_message:
    .ascii "Vex ELF file-mmap smoke: failed\n"
fail_message_len = . - fail_message
path:
    .asciz "/home/root/file_mmap_smoke.txt"
large_path:
    .asciz "/home/root/file_mmap_large_sparse.bin"
payload:
    .ascii "VEXMAPOK"
payload_len = . - payload
ftruncate_message:
    .ascii "Vex ELF file-mmap smoke: ftruncate-shared ok\n"
ftruncate_message_len = . - ftruncate_message
stage_ftruncate:
    .ascii "file-mmap stage: ftruncate ok\n"
stage_ftruncate_len = . - stage_ftruncate
stage_fstat:
    .ascii "file-mmap stage: fstat size ok\n"
stage_fstat_len = . - stage_fstat
stage_shared:
    .ascii "file-mmap stage: shared write ok\n"
stage_shared_len = . - stage_shared
stage_zero:
    .ascii "file-mmap stage: truncate zero-fill ok\n"
stage_zero_len = . - stage_zero
stage_positional:
    .ascii "file-mmap stage: positional io and sparse-hole zero-fill ok\n"
stage_positional_len = . - stage_positional
positional_patch:
    .ascii "X"
hole_marker:
    .ascii "Y"

.section .bss
.align 16
stat_buffer:
    .zero 144
zero_check:
    .zero 1
hole_check:
    .zero 12

.section .data
failure_detail:
    .ascii "file-mmap failure stage: "
failure_stage:
    .byte '?'
    .ascii " return=0x"
failure_return_hex:
    .ascii "0000000000000000"
    .ascii "\n"
failure_detail_len = . - failure_detail
failure_return:
    .quad 0
