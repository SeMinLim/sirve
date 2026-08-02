OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}

SECTIONS
{
	. = 0x1000;

	.text : ALIGN(4)
	{
		KEEP(*(.text.start))
		*(.text .text.*)
		*(.rodata .rodata.*)
	} :text

	. = ALIGN(0x1000);

	.data : ALIGN(4)
	{
		__dataStart = .;
		*(.data .data.*)
		*(.sdata .sdata.*)
		__dataEnd = .;
	} :data

	.bss (NOLOAD) : ALIGN(4)
	{
		__bssStart = .;
		*(.bss .bss.*)
		*(.sbss .sbss.*)
		*(COMMON)
		__bssEnd = .;
	} :data

	. = ALIGN(16);
	__heapStart = .;
	__stackTop = 0x10000;

	ASSERT(__heapStart < __stackTop, "Freestanding program exceeds emulated memory")

	/DISCARD/ :
	{
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}
}
