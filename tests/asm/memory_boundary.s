lui x1 0x10
addi x1 x1 -4
lui x2 0x12345
addi x2 x2 0x678
sw x2 0(x1)
lw x3 0(x1)
addi x1 x1 3
sb x2 0(x1)
lbu x4 0(x1)
addi x1 x1 -1
sh x2 0(x1)
lhu x5 0(x1)
hcf
