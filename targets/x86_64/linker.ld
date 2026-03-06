ENTRY(kernel_main)

SECTIONS
{
    /* We want to at least be at the 2MB mark as this is the minimum */
    /* alignment for the kernel in Limine. */
    . = 0xffffffff80200000;

    .text : ALIGN(4K)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4K)
    {
        *(.data .data.*)
    }

    /* Requests section for Limine protocol */
    .requests : ALIGN(4K)
    {
        KEEP(*(.requests))
    }

    .bss : ALIGN(4K)
    {
        *(.bss .bss.*)
        *(COMMON)
        kernel_end = .;
    }

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.note .note.*)
    }
}
