#ifndef SIRVE_RV32I_H
#define SIRVE_RV32I_H

#include <stddef.h>
#include <stdint.h>

#include "memory.h"


typedef enum {
	RV32I_INVALID = 0,
	RV32I_LUI,
	RV32I_AUIPC,
	RV32I_JAL,
	RV32I_JALR,
	RV32I_BEQ,
	RV32I_BNE,
	RV32I_BLT,
	RV32I_BGE,
	RV32I_BLTU,
	RV32I_BGEU,
	RV32I_LB,
	RV32I_LH,
	RV32I_LW,
	RV32I_LBU,
	RV32I_LHU,
	RV32I_SB,
	RV32I_SH,
	RV32I_SW,
	RV32I_ADDI,
	RV32I_SLTI,
	RV32I_SLTIU,
	RV32I_XORI,
	RV32I_ORI,
	RV32I_ANDI,
	RV32I_SLLI,
	RV32I_SRLI,
	RV32I_SRAI,
	RV32I_ADD,
	RV32I_SUB,
	RV32I_SLL,
	RV32I_SLT,
	RV32I_SLTU,
	RV32I_XOR,
	RV32I_SRL,
	RV32I_SRA,
	RV32I_OR,
	RV32I_AND,
	RV32I_FENCE,
	RV32I_ECALL,
	RV32I_EBREAK
} RV32IInstrType;


typedef struct {
	uint32_t raw;
	RV32IInstrType op;
	uint32_t opcode;
	uint32_t rd;
	uint32_t funct3;
	uint32_t rs1;
	uint32_t rs2;
	uint32_t funct7;
	int32_t imm;
} DecodedInstr;


typedef struct {
	uint32_t reg[32];
	uint32_t pc;
	uint64_t instCnt;
} ProcessorState;


typedef enum {
	RV32I_STEP_OK = 0,
	RV32I_STEP_EBREAK,
	RV32I_STEP_ECALL,
	RV32I_STEP_ILLEGAL_INSTRUCTION,
	RV32I_STEP_INSTRUCTION_MISALIGNED,
	RV32I_STEP_INSTRUCTION_ACCESS_FAULT,
	RV32I_STEP_LOAD_MISALIGNED,
	RV32I_STEP_LOAD_ACCESS_FAULT,
	RV32I_STEP_STORE_MISALIGNED,
	RV32I_STEP_STORE_ACCESS_FAULT
} RV32IStepStatus;


typedef struct {
	RV32IStepStatus status;
	uint32_t pc;
	uint32_t raw;
	uint32_t faultAddr;
	DecodedInstr instr;
} RV32IStepResult;


int32_t decodeImmediateI( uint32_t raw );
int32_t decodeImmediateS( uint32_t raw );
int32_t decodeImmediateB( uint32_t raw );
int32_t decodeImmediateU( uint32_t raw );
int32_t decodeImmediateJ( uint32_t raw );

bool decodeRV32I( uint32_t raw, DecodedInstr *instr );
void initializeProcessorState( ProcessorState *state, uint32_t entryAddr );
RV32IStepStatus stepRV32I(
	ProcessorState *state,
	Memory *memory,
	RV32IStepResult *result
);
const char *rv32iInstrName( RV32IInstrType op );
bool disassembleRV32I( uint32_t raw, char *buffer, size_t bufferSize );

#endif
