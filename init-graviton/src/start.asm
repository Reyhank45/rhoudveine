section .text
global _start
_start:
    ; Visual Beacon 'Z' (0x1F5A) to 0xB8000
    mov word [0xB8000], 0x1F5A
    
    ; Loop forever
    jmp $
