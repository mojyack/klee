bits 64
section .text

%macro define_syscall 2
global syscall_%1
syscall_%1:
    mov rax, %2
    mov r10, rcx
    syscall
    ret
%endmacro

define_syscall printk, 0
define_syscall exit,   1
