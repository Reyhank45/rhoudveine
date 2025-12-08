BITS 64
global _start

section .text
_start:
    ; rdi contains pointer to print function (kernel will set this before jumping)
    mov rax, rdi        ; save print function ptr in rax
    lea rdi, [rel hello]
    call rax            ; call print("hello world\n")

halt_loop:
    cli
    hlt
    jmp halt_loop

section .rodata
hello: db "hello world", 10, 0
