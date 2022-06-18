bits 64
section .text

global io_set32
io_set32:
    mov dx, di    ; dx = address
    mov eax, esi  ; eax = data
    out dx, eax
    ret

global io_read32
io_read32:
    mov dx, di    ; dx = addr
    in eax, dx
    ret

global read_cs
read_cs:
    xor eax, eax
    mov ax, cs
    ret

global load_idt
load_idt:
    push rbp
    mov rbp, rsp
    sub rsp, 10
    mov [rsp], di  ; limit
    mov [rsp + 2], rsi  ; offset
    lidt [rsp]
    mov rsp, rbp
    pop rbp
    ret

global load_gdt
load_gdt:
    push rbp
    mov rbp, rsp
    sub rsp, 10
    mov [rsp], di  ; limit
    mov [rsp + 2], rsi  ; offset
    lgdt [rsp]
    mov rsp, rbp
    pop rbp
    ret

global set_csss
set_csss:
    push rbp
    mov rbp, rsp
    mov ss, si
    mov rax, .next
    push rdi    ; CS
    push rax    ; RIP
    o64 retf
.next:
    mov rsp, rbp
    pop rbp
    ret

global set_dsall
set_dsall:
    mov ds, di
    mov es, di
    mov fs, di
    mov gs, di
    ret

global set_cr3
set_cr3:
    mov cr3, rdi
    ret

extern kernel_main_stack
extern kernel_main

global kernel_entry
kernel_entry:
    mov rsp, kernel_main_stack + 1024 * 1024
    call kernel_main
.fin:
    hlt
    jmp .fin
