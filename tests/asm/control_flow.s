addi x1 x0 1
addi x2 x0 2
addi x3 x0 -1
addi x10 x0 0

beq x1 x1 beq_true
addi x10 x10 1
beq_true:
bne x1 x2 bne_true
addi x10 x10 2
bne_true:
blt x3 x1 blt_true
addi x10 x10 4
blt_true:
bge x2 x1 bge_true
addi x10 x10 8
bge_true:
bltu x1 x3 bltu_true
addi x10 x10 16
bltu_true:
bgeu x3 x2 bgeu_true
addi x10 x10 32
bgeu_true:

beq x1 x2 branch_fail
bne x1 x1 branch_fail
blt x2 x1 branch_fail
bge x1 x2 branch_fail
bltu x3 x1 branch_fail
bgeu x1 x3 branch_fail

jal x11 function
after_call:
addi x12 x0 85
jal x0 end

branch_fail:
addi x10 x10 64
jal x0 end

function:
addi x13 x0 102
jalr x0 0(x11)

end:
hcf
