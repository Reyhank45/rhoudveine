global start
extern long_mode_start

section .text
bits 32
start:
    mov esp, stack_top

    push ebx             ; Save Multiboot Pointer
    call check_multiboot
    call check_cpuid
    call check_long_mode

    call setup_page_tables   ; <--- This is the updated function
    call enable_paging
	call enable_sse
	pop edi
    lgdt [gdt64.pointer]
    jmp gdt64.code_segment:long_mode_start

    hlt

check_multiboot:
    cmp eax, 0x36d76289      ; Magic number for Multiboot2
    jne .no_multiboot
    ret
.no_multiboot:
    mov al, "M"
    jmp error

check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_cpuid
    ret
.no_cpuid:
    mov al, "C"
    jmp error

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode
    
    ret
.no_long_mode:
    mov al, "L"
    jmp error

setup_page_tables:
    ; 1. ZERO OUT THE TABLES (CRITICAL!)
    ; We must ensure the tables are empty before writing to them.
    ; We have 1 L4 + 1 L3 + 4 L2 tables = 6 tables total.
    mov edi, page_table_l4
    xor eax, eax
    mov ecx, 4096 * 6
    rep stosb

    ; 2. Link L4 -> L3
    mov eax, page_table_l3
    or eax, 0b11 ; present, writable
    mov [page_table_l4], eax
    ; Link PML4[256] to the same L3 table for the higher-half direct map.
    mov [page_table_l4 + 256 * 8], eax
    
    ; 3. Link L3 -> L2 (We need 4 L2 tables to cover 4GB)
    
    ; Entry 0 (0-1GB)
    mov eax, page_table_l2
    or eax, 0b11
    mov [page_table_l3], eax

    ; Entry 1 (1-2GB)
    mov eax, page_table_l2 + 4096
    or eax, 0b11
    mov [page_table_l3 + 8], eax

    ; Entry 2 (2-3GB)
    mov eax, page_table_l2 + (4096 * 2)
    or eax, 0b11
    mov [page_table_l3 + 16], eax

    ; Entry 3 (3-4GB) - This usually covers the Framebuffer
    mov eax, page_table_l2 + (4096 * 3)
    or eax, 0b11
    mov [page_table_l3 + 24], eax

    ; 4. Identity Map the 4GB Loop
    ; 4 tables * 512 entries = 2048 entries total
    mov ecx, 0
.loop:
    mov eax, 0x200000 ; 2MiB
    mul ecx           ; Calculate physical address
    or eax, 0b10000011 ; present, writable, huge page
    
    ; Write to the correct L2 table slot.
    ; Since the L2 tables are contiguous in BSS, we treat them as one array.
    mov [page_table_l2 + ecx * 8], eax

    inc ecx
    cmp ecx, 2048     ; Check if 4GB (2048 * 2MB) is mapped
    jne .loop

    ret

enable_paging:
    ; pass page table location to cpu
    mov eax, page_table_l4
    mov cr3, eax

    ; enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; enable long mode
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; enable paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ret

enable_sse:
    ; 1. Check for SSE (optional, but good practice. All x64 CPUs have it)
    mov eax, 0x1
    cpuid
    test edx, 1 << 25
    jz .no_sse

    ; 2. Enable SSE in CR0
    mov eax, cr0
    and ax, 0xFFFB      ; Clear EM (Bit 2) - "Emulation"
    or ax, 0x2          ; Set MP (Bit 1) - "Monitor Coprocessor"
    mov cr0, eax

    ; 3. Enable SSE in CR4
    mov eax, cr4
    or ax, 3 << 9       ; Set OSFXSR (Bit 9) and OSXMMEXCPT (Bit 10)
    mov cr4, eax

    ret
.no_sse:
    mov al, "S"
    jmp error

error:
    ; Note: This writes to 0xb8000 (VGA Text).
    ; On pure UEFI systems, this might not show anything,
    ; but it won't crash the system if memory is mapped correctly.
    mov dword [0xb8000], 0x4f524f45
    mov dword [0xb8004], 0x4f3a4f52
    mov dword [0xb8008], 0x4f204f20
    mov byte  [0xb800a], al
    hlt

section .bss
align 4096
page_table_l4:
    resb 4096
page_table_l3:
    resb 4096
page_table_l2:
    resb 4096 * 4      ; <--- RESERVED 4 TABLES (16KB) INSTEAD OF 1
stack_bottom:
    resb 4096 * 4
stack_top:
    global stack_top;

section .rodata
gdt64:
    dq 0 ; zero entry
.code_segment: equ $ - gdt64
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53) ; code segment
.pointer:
    dw $ - gdt64 - 1 ; length
    dq gdt64 ; address