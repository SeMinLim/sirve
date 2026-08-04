OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS(5);
}

SECTIONS
{
	. = 0x1000;

	.text : ALIGN(4)
	{
		KEEP(*(.text.start))
		*(.text .text.*)
	} :text

	/DISCARD/ :
	{
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}
}
