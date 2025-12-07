section .multiboot_header
header_start:
	; magic number
	dd 0xe85250d6 ; multiboot2
	; architecture
	dd 0 ; protected mode i386
	; header length
	dd header_end - header_start
	; checksum
	dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))

	; Framebuffer Tag
    align 8
    dd 5    ; Type
    dd 20    ; Flags
    dd 0   ; Size
    dd 0 ; Width
    dd 0  ; Height
    dd 0   ; Depth

	; end tag
	dw 0
	dw 0
	dd 8
header_end:
