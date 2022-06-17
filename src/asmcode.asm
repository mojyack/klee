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
