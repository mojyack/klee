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

global get_cr3
get_cr3:
    mov rax, cr3
    ret

global set_cr3
set_cr3:
    mov cr3, rdi
    ret

global write_msr
write_msr:
    mov rdx, rsi
    shr rdx, 32
    mov eax, esi
    mov ecx, edi
    wrmsr
    ret

global switch_context
switch_context:
    mov [rsi + 0x40], rax
    mov [rsi + 0x48], rbx
    mov [rsi + 0x50], rcx
    mov [rsi + 0x58], rdx
    mov [rsi + 0x60], rdi
    mov [rsi + 0x68], rsi

    lea rax, [rsp + 8]
    mov [rsi + 0x70], rax  ; RSP
    mov [rsi + 0x78], rbp

    mov [rsi + 0x80], r8
    mov [rsi + 0x88], r9
    mov [rsi + 0x90], r10
    mov [rsi + 0x98], r11
    mov [rsi + 0xa0], r12
    mov [rsi + 0xa8], r13
    mov [rsi + 0xb0], r14
    mov [rsi + 0xb8], r15

    mov rax, cr3
    mov [rsi + 0x00], rax  ; CR3
    mov rax, [rsp]
    mov [rsi + 0x08], rax  ; RIP
    pushfq
    pop qword [rsi + 0x10] ; RFLAGS

    mov ax, cs
    mov [rsi + 0x20], rax
    mov bx, ss
    mov [rsi + 0x28], rbx
    mov cx, fs
    mov [rsi + 0x30], rcx
    mov dx, gs
    mov [rsi + 0x38], rdx

    fxsave [rsi + 0xc0]
    ; fall through to restore_context

global restore_context
restore_context:
    ; stackframe for iret
    push qword [rdi + 0x28] ; SS
    push qword [rdi + 0x70] ; RSP
    push qword [rdi + 0x10] ; RFLAGS
    push qword [rdi + 0x20] ; CS
    push qword [rdi + 0x08] ; RIP

    ; restore context
    fxrstor [rdi + 0xc0]

    mov rax, [rdi + 0x00]
    mov cr3, rax
    mov rax, [rdi + 0x30]
    mov fs, ax
    mov rax, [rdi + 0x38]
    mov gs, ax

    mov rax, [rdi + 0x40]
    mov rbx, [rdi + 0x48]
    mov rcx, [rdi + 0x50]
    mov rdx, [rdi + 0x58]
    mov rsi, [rdi + 0x68]
    mov rbp, [rdi + 0x78]
    mov r8,  [rdi + 0x80]
    mov r9,  [rdi + 0x88]
    mov r10, [rdi + 0x90]
    mov r11, [rdi + 0x98]
    mov r12, [rdi + 0xa0]
    mov r13, [rdi + 0xa8]
    mov r14, [rdi + 0xb0]
    mov r15, [rdi + 0xb8]

    mov rdi, [rdi + 0x60]

    sti
    o64 iret

extern self_task_system_stack
global jump_to_app
jump_to_app:  ; void jump_to_app(uint64_t id, int64_t data, uint16_t ss(rdx), uint64_t rip(rcx), uint64_t rsp(r8), uint64_t* system_stack_ptr(r9));
    mov [r9], rsp ; save system stack pointer

    ; self_task_system_stack is automatically updated by the task_manager on context switch,
    ; but set here so that the system call can be called before it is ever context switched
    mov [self_task_system_stack], rsp

    push rdx  ; SS
    push r8   ; RSP
    add rdx, 8
    push rdx  ; CS
    push rcx  ; RIP
    o64 retf

extern syscall_table
global syscall_entry
syscall_entry:
    push rbp
    push rcx  ; original RIP
    push r11  ; original RFLAGS

    mov rcx, r10 ; restore 4th argument (rcx is used by SYSCALL)
    mov rbp, rsp ; rebase stack

    mov rsp, [self_task_system_stack] ; use kernel stack
    and rsp, 0xFFFFFFFFFFFFFFF0 ; rsp must be 16 byte aligned

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
