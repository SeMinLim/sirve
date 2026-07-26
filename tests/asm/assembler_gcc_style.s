.text
main:
.L0:
	li sp 0x10000
	lla a4 data
	bnez a0 .L1
	beqz a1 .L1
	bgt a0 a1 .L1
	ble a0 a1 .L1
	jr ra
.L1:
	lbu a5 0(a4)
	sh a5 2(a4)
	fence
	ecall
	ebreak

.data
data:
	.zero 16
