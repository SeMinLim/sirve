.text
start:
	add x1 x2 x3
	addi x4 x5 -1
	sw x6 12(x7)
	beq x1 x2 branch_target
	jal x1 jump_target
	lui x8 0x12345
branch_target:
	slli x9 x10 31
jump_target:
	hcf

.data
value:
	.word 0x12345678
	.byte -1
	.half 0xabcd
	.zero 1
