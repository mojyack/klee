bits 64
section .text

global syscall_printk
syscall_printk:
    mov eax, 0x00
    mov r10, rcx
    syscall
    ret
