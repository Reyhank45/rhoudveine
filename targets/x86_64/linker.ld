ENTRY(start)

SECTIONS
{
	. = 1M;

	.boot :
	{
		*(.multiboot_header)
	}

 .text : ALIGN(4K)
	{
		*(.text)
	}

 .rodata : ALIGN(4K)
	{
		*(.rodata*)
	}
 .data : ALIGN(4K)
	{
		*(.data)
	}

 .bss : ALIGN(4K)
 {
	*(.bss)
	*(COMMON)
	kernel_end = .;
 }
}
