	.section .text.start,"ax",@progbits
	.globl _start
	.type _start, @function
_start:
	lui x1, 0x2
	addi x2, x0, 5
	addi x3, x0, -3
	add x4, x2, x3
	sub x5, x2, x3
	sll x6, x2, x2
	srl x7, x6, x2
	sra x8, x3, x2
	slt x9, x3, x2
	sltu x10, x3, x2
	xor x11, x2, x3
	or x12, x2, x3
	and x13, x2, x3
	slli x14, x2, 3
	srli x15, x14, 2
	srai x16, x3, 1
	slti x17, x3, 0
	sltiu x18, x3, 1
	xori x19, x2, 0x55
	ori x20, x2, 0x40
	andi x21, x3, 0xff
	sw x5, 0(x1)
	sh x2, 4(x1)
	sb x3, 6(x1)
	lw x22, 0(x1)
	lh x23, 4(x1)
	lhu x24, 4(x1)
	lb x25, 6(x1)
	lbu x26, 6(x1)
	beq x22, x5, 1f
	addi x27, x0, 1
1:
	bne x22, x5, 2f
	addi x27, x0, 0x22
2:
	blt x3, x2, 3f
	addi x28, x0, 1
3:
	bge x2, x3, 4f
	addi x28, x0, 2
4:
	bltu x2, x3, 5f
	addi x28, x0, 3
5:
	bgeu x3, x2, 6f
	addi x28, x0, 4
6:
	auipc x28, 0
	jal x29, 7f
	addi x30, x0, 0x55
7:
	addi x30, x0, 0x66
	auipc x31, 0
	addi x31, x31, 16
	jalr x0, 0(x31)
	addi x30, x0, 0x77
	addi x30, x30, 1
	fence
	ebreak
	.size _start, . - _start
