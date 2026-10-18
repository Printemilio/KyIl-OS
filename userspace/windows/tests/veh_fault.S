.text
.global trigger_fault
trigger_fault:
    xor %eax,%eax
.global fault_ip
fault_ip:
    mov (%rax),%rax
.global resume_ip
resume_ip:
    ret
