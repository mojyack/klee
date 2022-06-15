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
