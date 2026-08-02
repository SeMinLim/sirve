	.section .text.start
	.globl _start
	.type _start, @function
_start:
	.option push
	.option norelax
	la sp, __stackTop
	.option pop
	jal ra, main
	ebreak
1:
	j 1b
	.size _start, . - _start
