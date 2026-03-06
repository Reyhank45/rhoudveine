global isr_default_handler
global isr_irq0
global isr_irq1
global reload_segments
global reload_segments

section .text
bits 64

isr_default_handler:
    cli
    hlt
    
; IRQ handler stub for IRQ0 (timer)
isr_irq0:
    cli
    push rbp
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    mov rdi, 0
    extern irq_handler
    call irq_handler
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    pop rbp
    ; send EOI to PIC
    mov al, 0x20
    out 0x20, al
    sti
    iretq

; IRQ handler stub for IRQ1 (keyboard)
isr_irq1:
    cli
    push rbp
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    mov rdi, 1
    extern irq_handler
    call irq_handler
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    pop rbp
    ; send EOI to PIC (both if slave)
    mov al, 0x20
    out 0xA0, al
    mov al, 0x20
    out 0x20, al
    sti
    iretq

; reload_segments(void)
; Helper to reload CS and data segments after LGDT
reload_segments:
    ; 1. Reload CS via a far return
    push 0x08               ; New CS (code segment selector)
    lea rax, [rel .reload_cs] ; Address to jump back to
    push rax
    retfq                   ; Far return to 64-bit far pointer
.reload_cs:
    ; 2. Reload data segments
    mov ax, 0x10            ; New data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
