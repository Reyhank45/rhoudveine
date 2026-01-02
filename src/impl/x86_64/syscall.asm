; syscall.asm - SYSCALL instruction entry point
; This is called when userspace executes the SYSCALL instruction

section .text
global syscall_entry
extern syscall_handler

; SYSCALL calling convention:
; RCX = return address (saved by CPU)
; R11 = saved RFLAGS (saved by CPU)
; RAX = syscall number
; RDI = arg1
; RSI = arg2
; RDX = arg3
; R10 = arg4 (RCX is clobbered by SYSCALL, so arg4 uses R10)
; R8  = arg5
; R9  = arg6

syscall_entry:
    ; Save user stack pointer
    mov r12, rsp
    
    ; Switch to kernel stack (use a simple approach for now)
    ; In a real implementation, we'd use the TSS or per-CPU data
    ; For now, we'll use a static kernel stack
    mov rsp, kernel_syscall_stack_top
    
    ; Save callee-saved registers
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    
    ; Save RCX (return address) and R11 (flags)
    push rcx
    push r11
    
    ; Set up arguments for syscall_handler
    ; syscall_handler(num, arg1, arg2, arg3, arg4, arg5)
    ; RAX already has syscall number, move to RDI
    ; Current: RAX=num, RDI=arg1, RSI=arg2, RDX=arg3, R10=arg4, R8=arg5
    ; Need:    RDI=num, RSI=arg1, RDX=arg2, RCX=arg3, R8=arg4, R9=arg5
    
    mov r9, r8      ; arg5 -> r9
    mov r8, r10     ; arg4 -> r8
    mov rcx, rdx    ; arg3 -> rcx
    mov rdx, rsi    ; arg2 -> rdx
    mov rsi, rdi    ; arg1 -> rsi
    mov rdi, rax    ; num -> rdi
    
    ; Call the C handler
    call syscall_handler
    ; Return value is in RAX
    
    ; Restore saved registers
    pop r11
    pop rcx
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    
    ; Restore user stack
    mov rsp, r12
    
    ; Return to userspace
    ; SYSRET expects:
    ; RCX = return address
    ; R11 = saved RFLAGS
    o64 sysret

section .bss
    resb 4096
kernel_syscall_stack_top:
