.text
start:
	nop
	li x1 7
	li x2 0x12345678
	la x3 data_value
	call target
	j done
target:
	mv x4 x3
	ret
done:
	hcf

.data
data_value:
	.word 0
