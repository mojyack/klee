bits 64
section .text

; void switch_context(const ThreadContext* next, ThreadContext* current, std::atomic_uint8_t* current_lock)
global switch_context
switch_context:
    ; save context
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

    mov [rsi + 0x20], cs
    mov [rsi + 0x28], ss
    mov [rsi + 0x30], fs
    mov [rsi + 0x38], gs

    fxsave [rsi + 0xc0]

    ; unlock process manager
    xor eax, eax
    xchg al, byte [rdx]
    ; fall through to restore_context

; void restore_context(const ThreadContext* next)
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
    mov fs, [rdi + 0x30]
    mov gs, [rdi + 0x38]

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

    iretq
