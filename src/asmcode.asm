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
    retfq
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

global write_msr
write_msr:
    mov rdx, rsi
    shr rdx, 32
    mov eax, esi
    mov ecx, edi
    wrmsr
    ret

global jump_to_app
jump_to_app:  ; void jump_to_app(uint64_t id, int64_t data, uint16_t ss(rdx), uint64_t rip(rcx), uint64_t rsp(r8), uint64_t* system_stack_ptr(r9));
    mov [r9], rsp ; save system stack pointer

    push rdx  ; SS
    push r8   ; RSP
    add rdx, 8
    push rdx  ; CS
    push rcx  ; RIP
    retfq

extern syscall_table
extern get_stack_ptr
global syscall_entry
syscall_entry:
    push rbp
    push rcx  ; original RIP
    push r11  ; original RFLAGS

    mov rcx, r10 ; restore 4th argument (rcx is used by SYSCALL)
    mov rbp, rsp ; rebase stack

    ; use system stack
    and rsp, 0xFFFFFFFFFFFFFFF0
    push rax            ; save rax
    push rdx            ; save rdx
    call get_stack_ptr  ; rax = system_stack_address
    mov rdx, [rsp + 0]  ; restore saved rdx to rdx
    mov [rax - 16], rdx ; send rdx value to system stack
    mov rdx, [rsp + 8]  ; restore saved rax to rdx
    mov [rax - 8], rdx  ; send rax value to system stack

    lea rsp, [rax - 16] ; set stack
    pop rdx             ; receive copied rdx
    pop rax             ; receive copied rax

    and rsp, 0xFFFFFFFFFFFFFFF0
    call [syscall_table + 8 * eax]

    mov rsp, rbp 

    pop r11
    pop rcx
    pop rbp

    o64 sysret

global load_tr
load_tr:
    ltr di
    ret

extern int_handler_lapic_timer
global int_handler_lapic_timer_entry
int_handler_lapic_timer_entry:
    push rbp
    mov rbp, rsp

    ; construct TaskContext on the stack
    sub rsp, 512
    fxsave [rsp]
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push qword [rbp]         ; RBP
    push qword [rbp + 0x20]  ; RSP
    push rsi
    push rdi
    push rdx
    push rcx
    push rbx
    push rax

    mov ax, fs
    mov bx, gs
    mov rcx, cr3

    push rbx                 ; GS
    push rax                 ; FS
    push qword [rbp + 0x28]  ; SS
    push qword [rbp + 0x10]  ; CS
    push rbp                 ; reserved1
    push qword [rbp + 0x18]  ; RFLAGS
    push qword [rbp + 0x08]  ; RIP
    push rcx                 ; CR3

    mov rdi, rsp
    call int_handler_lapic_timer

    add rsp, 8*8  ; ignore CR3 to GS
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rdi
    pop rsi
    add rsp, 16   ; ignore RSP and RBP
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    fxrstor [rsp]

    mov rsp, rbp
    pop rbp
    iretq

extern kernel_main_stack
extern kernel_main

global kernel_entry
kernel_entry:
    mov rsp, kernel_main_stack + 1024 * 1024
    call kernel_main
.fin:
    hlt
    jmp .fin
