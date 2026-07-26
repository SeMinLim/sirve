#include "rv32i.h"

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

static void initializeDecodedInstr( uint32_t raw, DecodedInstr *instr ) {
	instr->raw = raw;
	instr->op = RV32I_INVALID;
	instr->opcode = raw & 0x7f;
	instr->rd = (raw >> 7) & 0x1f;
	instr->funct3 = (raw >> 12) & 0x07;
	instr->rs1 = (raw >> 15) & 0x1f;
	instr->rs2 = (raw >> 20) & 0x1f;
	instr->funct7 = (raw >> 25) & 0x7f;
	instr->imm = 0;
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
			if ( instr->funct3 != 0x0 ) return false;
			instr->op = RV32I_FENCE;
			instr->imm = (raw >> 20) & 0xfff;
			return true;
		case 0x73:
			if ( instr->funct3 != 0x0 || instr->rd != 0 || instr->rs1 != 0 ) return false;
			if ( (raw >> 20) == 0 ) instr->op = RV32I_ECALL;
			else if ( (raw >> 20) == 1 ) instr->op = RV32I_EBREAK;
			else return false;
			instr->imm = (int32_t)(raw >> 20);
			return true;
		default:
			return false;
	}
}
