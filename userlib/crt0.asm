; crt0.asm - C runtime startup for userspace programs
; This is the entry point that the kernel jumps to

section .text
global _start
extern main

_start:
    ; Clear frame pointer for stack traces
    xor rbp, rbp
    
    ; Arguments (argc, argv) are passed in RDI, RSI by kernel
    ; Just call main directly
    call main
    
    ; Exit with return value from main
    mov rdi, rax    ; Return value -> exit status
    mov rax, 0      ; SYS_EXIT = 0
    syscall
    
    ; Should never reach here
    hlt
