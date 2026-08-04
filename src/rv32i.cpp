#include "rv32i.h"

#include <stdio.h>
#include <string.h>


static int32_t bitCastSigned32( uint32_t value ) {
	int32_t signedValue = 0;
	memcpy(&signedValue, &value, sizeof(signedValue));
	return signedValue;
}

static int32_t signExtendImmediate( uint32_t value, uint32_t bits ) {
	uint32_t mask = (1u << bits) - 1u;
	uint32_t signBit = 1u << (bits - 1);
	uint32_t extended = value & mask;

	if ( extended & signBit ) extended |= ~mask;
	return bitCastSigned32(extended);
}

static uint32_t arithmeticShiftRight( uint32_t value, uint32_t shiftAmount ) {
	shiftAmount &= 0x1f;
	if ( shiftAmount == 0 ) return value;

	uint32_t shifted = value >> shiftAmount;
	if ( value & 0x80000000u ) shifted |= ~0u << (32 - shiftAmount);
	return shifted;
}

static void initializeDecodedInstr( uint32_t raw, DecodedInstr *instr ) {
	memset(instr, 0, sizeof(*instr));
	instr->raw = raw;
	instr->op = RV32I_INVALID;
	instr->opcode = raw & 0x7f;
	instr->rd = (raw >> 7) & 0x1f;
	instr->funct3 = (raw >> 12) & 0x07;
	instr->rs1 = (raw >> 15) & 0x1f;
	instr->rs2 = (raw >> 20) & 0x1f;
	instr->funct7 = (raw >> 25) & 0x7f;
}

static void initializeStepResult( RV32IStepResult *result, uint32_t pc ) {
	if ( result == NULL ) return;
	memset(result, 0, sizeof(*result));
	result->status = RV32I_STEP_OK;
	result->pc = pc;
	result->nextPc = pc;
	result->instr.op = RV32I_INVALID;
}

static void writeRegister(
	ProcessorState *state,
	uint32_t reg,
	uint32_t value,
	RV32IStepResult *result
) {
	if ( reg == 0 ) return;

	state->reg[reg] = value;
	if ( result != NULL ) {
		result->regWrite = true;
		result->regIdx = reg;
		result->regValue = value;
	}
}

static void recordMemoryRead( RV32IStepResult *result, uint32_t addr ) {
	if ( result == NULL ) return;
	result->memRead = true;
	result->memReadAddr = addr;
}

static void recordMemoryWrite(
	RV32IStepResult *result,
	uint32_t addr,
	uint32_t size,
	uint32_t value
) {
	if ( result == NULL ) return;
	result->memWrite = true;
	result->memWriteAddr = addr;
	result->memWriteSize = size;
	if ( size == 1 ) result->memWriteValue = value & 0xffu;
	else if ( size == 2 ) result->memWriteValue = value & 0xffffu;
	else result->memWriteValue = value;
}

static RV32IStepStatus validateInstructionTarget(
	const Memory *memory,
	uint32_t target,
	RV32IStepResult *result
) {
	uint32_t unused = 0;
	MemoryStatus status = memoryFetch32(memory, target, &unused);
	if ( status == MEMORY_STATUS_MISALIGNED ) {
		if ( result != NULL ) result->faultAddr = target;
		return RV32I_STEP_INSTRUCTION_MISALIGNED;
	}
	if ( status == MEMORY_STATUS_OUT_OF_BOUNDS ) {
		if ( result != NULL ) result->faultAddr = target;
		return RV32I_STEP_INSTRUCTION_ACCESS_FAULT;
	}
	return RV32I_STEP_OK;
}

static RV32IStepStatus mapLoadStatus(
	MemoryStatus status,
	uint32_t addr,
	RV32IStepResult *result
) {
	if ( status == MEMORY_STATUS_OK ) return RV32I_STEP_OK;
	if ( result != NULL ) result->faultAddr = addr;
	if ( status == MEMORY_STATUS_MISALIGNED ) return RV32I_STEP_LOAD_MISALIGNED;
	return RV32I_STEP_LOAD_ACCESS_FAULT;
}

static RV32IStepStatus mapStoreStatus(
	MemoryStatus status,
	uint32_t addr,
	RV32IStepResult *result
) {
	if ( status == MEMORY_STATUS_OK ) return RV32I_STEP_OK;
	if ( result != NULL ) result->faultAddr = addr;
	if ( status == MEMORY_STATUS_MISALIGNED ) return RV32I_STEP_STORE_MISALIGNED;
	return RV32I_STEP_STORE_ACCESS_FAULT;
}


int32_t decodeImmediateI( uint32_t raw ) {
	return signExtendImmediate(raw >> 20, 12);
}

int32_t decodeImmediateS( uint32_t raw ) {
	uint32_t immediate = ((raw >> 25) << 5) |
	                     ((raw >> 7) & 0x1f);
	return signExtendImmediate(immediate, 12);
}

int32_t decodeImmediateB( uint32_t raw ) {
	uint32_t immediate = ((raw >> 31) << 12) |
	                     (((raw >> 7) & 0x01) << 11) |
	                     (((raw >> 25) & 0x3f) << 5) |
	                     (((raw >> 8) & 0x0f) << 1);
	return signExtendImmediate(immediate, 13);
}

int32_t decodeImmediateU( uint32_t raw ) {
	return bitCastSigned32(raw & 0xfffff000u);
}

int32_t decodeImmediateJ( uint32_t raw ) {
	uint32_t immediate = ((raw >> 31) << 20) |
	                     (((raw >> 12) & 0xff) << 12) |
	                     (((raw >> 20) & 0x01) << 11) |
	                     (((raw >> 21) & 0x3ff) << 1);
	return signExtendImmediate(immediate, 21);
}

bool decodeRV32I( uint32_t raw, DecodedInstr *instr ) {
	if ( instr == NULL ) return false;
	initializeDecodedInstr(raw, instr);

	switch ( instr->opcode ) {
		case 0x37:
			instr->op = RV32I_LUI;
			instr->imm = decodeImmediateU(raw);
			return true;
		case 0x17:
			instr->op = RV32I_AUIPC;
			instr->imm = decodeImmediateU(raw);
			return true;
		case 0x6f:
			instr->op = RV32I_JAL;
			instr->imm = decodeImmediateJ(raw);
			return true;
		case 0x67:
			if ( instr->funct3 != 0 ) return false;
			instr->op = RV32I_JALR;
			instr->imm = decodeImmediateI(raw);
			return true;
		case 0x63:
			switch ( instr->funct3 ) {
				case 0x0: instr->op = RV32I_BEQ; break;
				case 0x1: instr->op = RV32I_BNE; break;
				case 0x4: instr->op = RV32I_BLT; break;
				case 0x5: instr->op = RV32I_BGE; break;
				case 0x6: instr->op = RV32I_BLTU; break;
				case 0x7: instr->op = RV32I_BGEU; break;
				default: return false;
			}
			instr->imm = decodeImmediateB(raw);
			return true;
		case 0x03:
			switch ( instr->funct3 ) {
				case 0x0: instr->op = RV32I_LB; break;
				case 0x1: instr->op = RV32I_LH; break;
				case 0x2: instr->op = RV32I_LW; break;
				case 0x4: instr->op = RV32I_LBU; break;
				case 0x5: instr->op = RV32I_LHU; break;
				default: return false;
			}
			instr->imm = decodeImmediateI(raw);
			return true;
		case 0x23:
			switch ( instr->funct3 ) {
				case 0x0: instr->op = RV32I_SB; break;
				case 0x1: instr->op = RV32I_SH; break;
				case 0x2: instr->op = RV32I_SW; break;
				default: return false;
			}
			instr->imm = decodeImmediateS(raw);
			return true;
		case 0x13:
			switch ( instr->funct3 ) {
				case 0x0:
					instr->op = RV32I_ADDI;
					instr->imm = decodeImmediateI(raw);
					return true;
				case 0x2:
					instr->op = RV32I_SLTI;
					instr->imm = decodeImmediateI(raw);
					return true;
				case 0x3:
					instr->op = RV32I_SLTIU;
					instr->imm = decodeImmediateI(raw);
					return true;
				case 0x4:
					instr->op = RV32I_XORI;
					instr->imm = decodeImmediateI(raw);
					return true;
				case 0x6:
					instr->op = RV32I_ORI;
					instr->imm = decodeImmediateI(raw);
					return true;
				case 0x7:
					instr->op = RV32I_ANDI;
					instr->imm = decodeImmediateI(raw);
					return true;
				case 0x1:
					if ( instr->funct7 != 0x00 ) return false;
					instr->op = RV32I_SLLI;
					instr->imm = (raw >> 20) & 0x1f;
					return true;
				case 0x5:
					if ( instr->funct7 == 0x00 ) instr->op = RV32I_SRLI;
					else if ( instr->funct7 == 0x20 ) instr->op = RV32I_SRAI;
					else return false;
					instr->imm = (raw >> 20) & 0x1f;
					return true;
				default:
					return false;
			}
		case 0x33:
			switch ( instr->funct3 ) {
				case 0x0:
					if ( instr->funct7 == 0x00 ) instr->op = RV32I_ADD;
					else if ( instr->funct7 == 0x20 ) instr->op = RV32I_SUB;
					else return false;
					return true;
				case 0x1:
					if ( instr->funct7 != 0x00 ) return false;
					instr->op = RV32I_SLL;
					return true;
				case 0x2:
					if ( instr->funct7 != 0x00 ) return false;
					instr->op = RV32I_SLT;
					return true;
				case 0x3:
					if ( instr->funct7 != 0x00 ) return false;
					instr->op = RV32I_SLTU;
					return true;
				case 0x4:
					if ( instr->funct7 != 0x00 ) return false;
					instr->op = RV32I_XOR;
					return true;
				case 0x5:
					if ( instr->funct7 == 0x00 ) instr->op = RV32I_SRL;
					else if ( instr->funct7 == 0x20 ) instr->op = RV32I_SRA;
					else return false;
					return true;
				case 0x6:
					if ( instr->funct7 != 0x00 ) return false;
					instr->op = RV32I_OR;
					return true;
				case 0x7:
					if ( instr->funct7 != 0x00 ) return false;
					instr->op = RV32I_AND;
					return true;
				default:
					return false;
			}
		case 0x0f:
			if ( instr->funct3 != 0 || instr->rd != 0 || instr->rs1 != 0 ) return false;
			instr->op = RV32I_FENCE;
			instr->imm = (raw >> 20) & 0xfff;
			return true;
		case 0x73:
			if ( instr->funct3 != 0 || instr->rd != 0 || instr->rs1 != 0 ) return false;
			if ( (raw >> 20) == 0 ) instr->op = RV32I_ECALL;
			else if ( (raw >> 20) == 1 ) instr->op = RV32I_EBREAK;
			else return false;
			instr->imm = (int32_t)(raw >> 20);
			return true;
		default:
			return false;
	}
}

void initializeProcessorState( ProcessorState *state, uint32_t entryAddr ) {
	if ( state == NULL ) return;
	memset(state, 0, sizeof(*state));
	state->pc = entryAddr;
}

RV32IStepStatus stepRV32I(
	ProcessorState *state,
	Memory *memory,
	RV32IStepResult *result
) {
	if ( state == NULL || memory == NULL ) return RV32I_STEP_INSTRUCTION_ACCESS_FAULT;
	initializeStepResult(result, state->pc);

	uint32_t raw = 0;
	MemoryStatus fetchStatus = memoryFetch32(memory, state->pc, &raw);
	if ( fetchStatus == MEMORY_STATUS_MISALIGNED ) {
		if ( result != NULL ) {
			result->status = RV32I_STEP_INSTRUCTION_MISALIGNED;
			result->faultAddr = state->pc;
		}
		return RV32I_STEP_INSTRUCTION_MISALIGNED;
	}
	if ( fetchStatus != MEMORY_STATUS_OK ) {
		if ( result != NULL ) {
			result->status = RV32I_STEP_INSTRUCTION_ACCESS_FAULT;
			result->faultAddr = state->pc;
		}
		return RV32I_STEP_INSTRUCTION_ACCESS_FAULT;
	}

	DecodedInstr instr;
	if ( !decodeRV32I(raw, &instr) ) {
		state->instCnt ++;
		if ( result != NULL ) {
			result->status = RV32I_STEP_ILLEGAL_INSTRUCTION;
			result->raw = raw;
			result->instr = instr;
		}
		return RV32I_STEP_ILLEGAL_INSTRUCTION;
	}

	if ( result != NULL ) {
		result->raw = raw;
		result->instr = instr;
	}
	state->instCnt ++;

	uint32_t pc = state->pc;
	uint32_t nextPc = pc + 4;
	uint32_t rs1Value = state->reg[instr.rs1];
	uint32_t rs2Value = state->reg[instr.rs2];
	uint32_t effectiveAddr = 0;
	uint32_t memoryValue = 0;
	MemoryStatus memoryStatus = MEMORY_STATUS_OK;
	RV32IStepStatus stepStatus = RV32I_STEP_OK;
	bool branchTaken = false;

	switch ( instr.op ) {
		case RV32I_LUI:
			writeRegister(state, instr.rd, (uint32_t)instr.imm, result);
			break;
		case RV32I_AUIPC:
			writeRegister(state, instr.rd, pc + (uint32_t)instr.imm, result);
			break;
		case RV32I_JAL:
			nextPc = pc + (uint32_t)instr.imm;
			stepStatus = validateInstructionTarget(memory, nextPc, result);
			if ( stepStatus != RV32I_STEP_OK ) break;
			writeRegister(state, instr.rd, pc + 4, result);
			break;
		case RV32I_JALR:
			nextPc = (rs1Value + (uint32_t)instr.imm) & ~1u;
			stepStatus = validateInstructionTarget(memory, nextPc, result);
			if ( stepStatus != RV32I_STEP_OK ) break;
			writeRegister(state, instr.rd, pc + 4, result);
			break;
		case RV32I_BEQ: branchTaken = rs1Value == rs2Value; break;
		case RV32I_BNE: branchTaken = rs1Value != rs2Value; break;
		case RV32I_BLT: branchTaken = bitCastSigned32(rs1Value) < bitCastSigned32(rs2Value); break;
		case RV32I_BGE: branchTaken = bitCastSigned32(rs1Value) >= bitCastSigned32(rs2Value); break;
		case RV32I_BLTU: branchTaken = rs1Value < rs2Value; break;
		case RV32I_BGEU: branchTaken = rs1Value >= rs2Value; break;
		case RV32I_LB:
			effectiveAddr = rs1Value + (uint32_t)instr.imm;
			memoryStatus = memoryRead8(memory, effectiveAddr, &memoryValue);
			stepStatus = mapLoadStatus(memoryStatus, effectiveAddr, result);
			if ( stepStatus == RV32I_STEP_OK ) {
				recordMemoryRead(result, effectiveAddr);
				writeRegister(state, instr.rd,
				              (uint32_t)signExtendImmediate(memoryValue, 8), result);
			}
			break;
		case RV32I_LH:
			effectiveAddr = rs1Value + (uint32_t)instr.imm;
			memoryStatus = memoryRead16(memory, effectiveAddr, &memoryValue);
			stepStatus = mapLoadStatus(memoryStatus, effectiveAddr, result);
			if ( stepStatus == RV32I_STEP_OK ) {
				recordMemoryRead(result, effectiveAddr);
				writeRegister(state, instr.rd,
				              (uint32_t)signExtendImmediate(memoryValue, 16), result);
			}
			break;
		case RV32I_LW:
			effectiveAddr = rs1Value + (uint32_t)instr.imm;
			memoryStatus = memoryRead32(memory, effectiveAddr, &memoryValue);
			stepStatus = mapLoadStatus(memoryStatus, effectiveAddr, result);
			if ( stepStatus == RV32I_STEP_OK ) {
				recordMemoryRead(result, effectiveAddr);
				writeRegister(state, instr.rd, memoryValue, result);
			}
			break;
		case RV32I_LBU:
			effectiveAddr = rs1Value + (uint32_t)instr.imm;
			memoryStatus = memoryRead8(memory, effectiveAddr, &memoryValue);
			stepStatus = mapLoadStatus(memoryStatus, effectiveAddr, result);
			if ( stepStatus == RV32I_STEP_OK ) {
				recordMemoryRead(result, effectiveAddr);
				writeRegister(state, instr.rd, memoryValue & 0xffu, result);
			}
			break;
		case RV32I_LHU:
			effectiveAddr = rs1Value + (uint32_t)instr.imm;
			memoryStatus = memoryRead16(memory, effectiveAddr, &memoryValue);
			stepStatus = mapLoadStatus(memoryStatus, effectiveAddr, result);
			if ( stepStatus == RV32I_STEP_OK ) {
				recordMemoryRead(result, effectiveAddr);
				writeRegister(state, instr.rd, memoryValue & 0xffffu, result);
			}
			break;
		case RV32I_SB:
			effectiveAddr = rs1Value + (uint32_t)instr.imm;
			memoryStatus = memoryWrite8(memory, effectiveAddr, rs2Value);
			stepStatus = mapStoreStatus(memoryStatus, effectiveAddr, result);
			if ( stepStatus == RV32I_STEP_OK ) {
				recordMemoryWrite(result, effectiveAddr, 1, rs2Value);
			}
			break;
		case RV32I_SH:
			effectiveAddr = rs1Value + (uint32_t)instr.imm;
			memoryStatus = memoryWrite16(memory, effectiveAddr, rs2Value);
			stepStatus = mapStoreStatus(memoryStatus, effectiveAddr, result);
			if ( stepStatus == RV32I_STEP_OK ) {
				recordMemoryWrite(result, effectiveAddr, 2, rs2Value);
			}
			break;
		case RV32I_SW:
			effectiveAddr = rs1Value + (uint32_t)instr.imm;
			memoryStatus = memoryWrite32(memory, effectiveAddr, rs2Value);
			stepStatus = mapStoreStatus(memoryStatus, effectiveAddr, result);
			if ( stepStatus == RV32I_STEP_OK ) {
				recordMemoryWrite(result, effectiveAddr, 4, rs2Value);
			}
			break;
		case RV32I_ADDI:
			writeRegister(state, instr.rd, rs1Value + (uint32_t)instr.imm, result);
			break;
		case RV32I_SLTI:
			writeRegister(state, instr.rd,
			              bitCastSigned32(rs1Value) < instr.imm ? 1u : 0u, result);
			break;
		case RV32I_SLTIU:
			writeRegister(state, instr.rd,
			              rs1Value < (uint32_t)instr.imm ? 1u : 0u, result);
			break;
		case RV32I_XORI:
			writeRegister(state, instr.rd, rs1Value ^ (uint32_t)instr.imm, result);
			break;
		case RV32I_ORI:
			writeRegister(state, instr.rd, rs1Value | (uint32_t)instr.imm, result);
			break;
		case RV32I_ANDI:
			writeRegister(state, instr.rd, rs1Value & (uint32_t)instr.imm, result);
			break;
		case RV32I_SLLI:
			writeRegister(state, instr.rd,
			              rs1Value << ((uint32_t)instr.imm & 0x1f), result);
			break;
		case RV32I_SRLI:
			writeRegister(state, instr.rd,
			              rs1Value >> ((uint32_t)instr.imm & 0x1f), result);
			break;
		case RV32I_SRAI:
			writeRegister(state, instr.rd,
			              arithmeticShiftRight(rs1Value, (uint32_t)instr.imm), result);
			break;
		case RV32I_ADD:
			writeRegister(state, instr.rd, rs1Value + rs2Value, result);
			break;
		case RV32I_SUB:
			writeRegister(state, instr.rd, rs1Value - rs2Value, result);
			break;
		case RV32I_SLL:
			writeRegister(state, instr.rd, rs1Value << (rs2Value & 0x1f), result);
			break;
		case RV32I_SLT:
			writeRegister(state, instr.rd,
			              bitCastSigned32(rs1Value) < bitCastSigned32(rs2Value) ? 1u : 0u,
			              result);
			break;
		case RV32I_SLTU:
			writeRegister(state, instr.rd, rs1Value < rs2Value ? 1u : 0u, result);
			break;
		case RV32I_XOR:
			writeRegister(state, instr.rd, rs1Value ^ rs2Value, result);
			break;
		case RV32I_SRL:
			writeRegister(state, instr.rd, rs1Value >> (rs2Value & 0x1f), result);
			break;
		case RV32I_SRA:
			writeRegister(state, instr.rd, arithmeticShiftRight(rs1Value, rs2Value), result);
			break;
		case RV32I_OR:
			writeRegister(state, instr.rd, rs1Value | rs2Value, result);
			break;
		case RV32I_AND:
			writeRegister(state, instr.rd, rs1Value & rs2Value, result);
			break;
		case RV32I_FENCE:
			break;
		case RV32I_ECALL:
			stepStatus = RV32I_STEP_ECALL;
			break;
		case RV32I_EBREAK:
			stepStatus = RV32I_STEP_EBREAK;
			break;
		case RV32I_INVALID:
		default:
			stepStatus = RV32I_STEP_ILLEGAL_INSTRUCTION;
			break;
	}

	if ( stepStatus == RV32I_STEP_OK && branchTaken ) {
		nextPc = pc + (uint32_t)instr.imm;
		stepStatus = validateInstructionTarget(memory, nextPc, result);
	}

	state->reg[0] = 0;
	if ( stepStatus == RV32I_STEP_OK ) state->pc = nextPc;
	if ( result != NULL ) {
		result->status = stepStatus;
		result->nextPc = stepStatus == RV32I_STEP_OK ? nextPc : pc;
	}
	return stepStatus;
}

const char *rv32iInstrName( RV32IInstrType op ) {
	switch ( op ) {
		case RV32I_LUI: return "lui";
		case RV32I_AUIPC: return "auipc";
		case RV32I_JAL: return "jal";
		case RV32I_JALR: return "jalr";
		case RV32I_BEQ: return "beq";
		case RV32I_BNE: return "bne";
		case RV32I_BLT: return "blt";
		case RV32I_BGE: return "bge";
		case RV32I_BLTU: return "bltu";
		case RV32I_BGEU: return "bgeu";
		case RV32I_LB: return "lb";
		case RV32I_LH: return "lh";
		case RV32I_LW: return "lw";
		case RV32I_LBU: return "lbu";
		case RV32I_LHU: return "lhu";
		case RV32I_SB: return "sb";
		case RV32I_SH: return "sh";
		case RV32I_SW: return "sw";
		case RV32I_ADDI: return "addi";
		case RV32I_SLTI: return "slti";
		case RV32I_SLTIU: return "sltiu";
		case RV32I_XORI: return "xori";
		case RV32I_ORI: return "ori";
		case RV32I_ANDI: return "andi";
		case RV32I_SLLI: return "slli";
		case RV32I_SRLI: return "srli";
		case RV32I_SRAI: return "srai";
		case RV32I_ADD: return "add";
		case RV32I_SUB: return "sub";
		case RV32I_SLL: return "sll";
		case RV32I_SLT: return "slt";
		case RV32I_SLTU: return "sltu";
		case RV32I_XOR: return "xor";
		case RV32I_SRL: return "srl";
		case RV32I_SRA: return "sra";
		case RV32I_OR: return "or";
		case RV32I_AND: return "and";
		case RV32I_FENCE: return "fence";
		case RV32I_ECALL: return "ecall";
		case RV32I_EBREAK: return "ebreak";
		case RV32I_INVALID:
		default: return "invalid";
	}
}

bool disassembleRV32I( uint32_t raw, char *buffer, size_t bufferSize ) {
	if ( buffer == NULL || bufferSize == 0 ) return false;

	DecodedInstr instr;
	if ( !decodeRV32I(raw, &instr) ) {
		snprintf(buffer, bufferSize, ".word 0x%08x", raw);
		return false;
	}

	const char *name = rv32iInstrName(instr.op);
	switch ( instr.op ) {
		case RV32I_LUI:
		case RV32I_AUIPC:
			snprintf(buffer, bufferSize, "%s x%u, 0x%x", name, instr.rd, raw >> 12);
			break;
		case RV32I_JAL:
			snprintf(buffer, bufferSize, "%s x%u, %d", name, instr.rd, instr.imm);
			break;
		case RV32I_JALR:
			snprintf(buffer, bufferSize, "%s x%u, %d(x%u)",
			         name, instr.rd, instr.imm, instr.rs1);
			break;
		case RV32I_BEQ:
		case RV32I_BNE:
		case RV32I_BLT:
		case RV32I_BGE:
		case RV32I_BLTU:
		case RV32I_BGEU:
			snprintf(buffer, bufferSize, "%s x%u, x%u, %d",
			         name, instr.rs1, instr.rs2, instr.imm);
			break;
		case RV32I_LB:
		case RV32I_LH:
		case RV32I_LW:
		case RV32I_LBU:
		case RV32I_LHU:
			snprintf(buffer, bufferSize, "%s x%u, %d(x%u)",
			         name, instr.rd, instr.imm, instr.rs1);
			break;
		case RV32I_SB:
		case RV32I_SH:
		case RV32I_SW:
			snprintf(buffer, bufferSize, "%s x%u, %d(x%u)",
			         name, instr.rs2, instr.imm, instr.rs1);
			break;
		case RV32I_ADDI:
		case RV32I_SLTI:
		case RV32I_SLTIU:
		case RV32I_XORI:
		case RV32I_ORI:
		case RV32I_ANDI:
		case RV32I_SLLI:
		case RV32I_SRLI:
		case RV32I_SRAI:
			snprintf(buffer, bufferSize, "%s x%u, x%u, %d",
			         name, instr.rd, instr.rs1, instr.imm);
			break;
		case RV32I_ADD:
		case RV32I_SUB:
		case RV32I_SLL:
		case RV32I_SLT:
		case RV32I_SLTU:
		case RV32I_XOR:
		case RV32I_SRL:
		case RV32I_SRA:
		case RV32I_OR:
		case RV32I_AND:
			snprintf(buffer, bufferSize, "%s x%u, x%u, x%u",
			         name, instr.rd, instr.rs1, instr.rs2);
			break;
		case RV32I_FENCE:
		case RV32I_ECALL:
		case RV32I_EBREAK:
			snprintf(buffer, bufferSize, "%s", name);
			break;
		case RV32I_INVALID:
		default:
			snprintf(buffer, bufferSize, ".word 0x%08x", raw);
			return false;
	}
	return true;
}
