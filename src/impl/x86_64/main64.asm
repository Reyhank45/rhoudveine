global long_mode_start
extern kernel_main
extern stack_top

section .text
bits 64
long_mode_start:
    ; load null into all data segment registers
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; 2. RESET AND ALIGN THE STACK (CRITICAL FIX)
    ; We reset the stack pointer to the top of our stack memory.
    mov rsp, stack_top
    
    ; We force the last 4 bits to be 0 (multiple of 16).
    ; This ensures strict 16-byte alignment required by the x64 ABI.
    and rsp, -16
	call kernel_main
    hlt
