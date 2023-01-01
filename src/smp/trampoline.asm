bits 16

; db=1 dw=2 dd=4 dq=8

global trampoline
trampoline:
real_start:
    cli
    cld

    ; ds <- cs
    xor ebx, ebx
    mov bx, cs
    mov ds, bx
    shl ebx, 4
    ; now ebx is image base

    ; relocate pointers
    add [var_gdt_desc - trampoline + 2], ebx ; gdt base address
    add [var_f32ptr - trampoline], ebx       ; protected mode base address
    add [var_f64ptr - trampoline], ebx       ; long mode base address

    ; init marker
    ; or dword [var_shared - trampoline], 1
   
    ; load tables
    lidt [var_idt_desc - trampoline]
    lgdt [var_gdt_desc - trampoline]

    ; enable protected mode
    mov eax, cr0
    bts eax, 0
    mov cr0, eax

    o32 jmp far [var_f32ptr - trampoline]

bits 32
prot_start:
    ; initialize data segment registers
    mov ax, 0x10 ; 15:3{=2}
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; stack at end of this page
    mov esp, ebx
    add esp, 0x1000

    ; enter long mode
    ; - enable pae and sse
    mov eax, cr4
    bts eax, 5 ; pae
    bts eax, 9 ; sse
    mov cr4, eax
    ; - load cr3
    mov eax, [ebx + var_cr3 - trampoline]
    mov cr3, eax
    ; - enable long mode
    mov ecx, 0xC0000080 ; IA32_EFER
    rdmsr
    bts eax, 8 ; long mode enable
    wrmsr
    ; - enable paging
    mov eax, cr0
    bts eax, 31
    mov cr0, eax

    o32 jmp far [ebx + var_f64ptr - trampoline]

bits 64
long_start:
    ; initialize data segment registers
    mov ax, 0x20 ; 15:3{=4}
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; enabel sse
    mov rax, cr0
    and ax, 0xFFFB  ; clear coprocessor emulation CR0.EM
    or ax, 0x0002   ; set coprocessor monitoring CR0.MP
    mov cr0, rax
    mov rax, cr4
    or ax, 0x0600   ; set CR4.OSFXSR and CR4.OSXMMEXCPT
    mov cr4, rax

    ; reset stack pointer
    mov rsp, [ebx + var_kernel_stack - trampoline]

    ; set argument of kernel entry
    mov rdi, [ebx + var_boot_parameter - trampoline]

    ; not jmp, call
    ; c compiler assumes that there is a return address on the stack
    ; without pushing the return address, an alignment violation may occur
    o64 call [ebx + var_kernel_entry - trampoline]

align 4
var_f32ptr:
    dd prot_start - trampoline ; offset
    dd 0x08                    ; cs (15:3{=1} is index in gdt)
var_f64ptr:
    dd long_start - trampoline ; offset
    dd 0x18                    ; cs (15:3{=3} is index in gdt)
var_gdt:
    dq 0x0000000000000000 ; +0x00{=0} null
    dq 0x00CF9A000000FFFF ; +0x08{=1} protected mode cs BaseHigh:00 Flags:C LimitHigh:F Access:9A BaseMiddle:00 BaseLow:0000 LimitLow:FFFF
    dq 0x00CF92000000FFFF ; +0x10{=2} protected mode ss
    dq 0x00AF9A000000FFFF ; +0x18{=3} long mode cs
    dq 0x00AF92000000FFFF ; +0x20{=4} long mode ss
var_gdt_desc:
    dw var_gdt_desc - var_gdt - 1
    dd var_gdt - trampoline
var_idt_desc:
    dw 0
    dd 0
; variables below are initialized by bsp
global var_cr3
var_cr3: ; cr3 for this core
    dd 0
global var_kernel_entry
var_kernel_entry: ; entry point after long mode
    dq 0
global var_kernel_stack
var_kernel_stack: ; rsp
    dq 0
global var_boot_parameter
var_boot_parameter: ; APBootParameter*
    dq 0

global trampoline_end
trampoline_end:
