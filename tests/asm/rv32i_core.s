# Register and immediate arithmetic boundaries
addi x1 x0 0
addi x2 x0 1
addi x3 x0 -1
lui x4 0x80000
addi x5 x0 31
addi x6 x0 32

# Register arithmetic and logical instructions
add x7 x2 x3
sub x8 x0 x2
slt x9 x3 x2
sltu x10 x3 x2
xor x11 x3 x2
or x12 x4 x2
and x13 x3 x4
sll x14 x2 x5
srl x15 x4 x5
sra x16 x4 x5
sll x17 x2 x6

# Immediate arithmetic and logical instructions
addi x18 x0 -2048
slti x19 x3 0
sltiu x20 x3 1
xori x21 x3 0xff
ori x22 x0 0x7ff
andi x23 x3 -2048
slli x24 x2 31
srli x25 x4 31
srai x26 x4 31

# Upper-immediate instructions
lui x27 0xfffff
auipc x28 1

# Aligned memory instructions
lui x29 0x1
sw x27 0(x29)
lw x30 0(x29)
sb x3 4(x29)
lb x31 4(x29)
lbu x1 4(x29)
sh x3 6(x29)
lh x2 6(x29)
lhu x3 6(x29)

hcf
