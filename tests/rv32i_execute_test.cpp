#include "memory.h"
#include "rv32i.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>


#define MEMORY_SIZE 0x10000
#define EXECUTABLE_LIMIT 0x8000


static int testCnt = 0;
static int passCnt = 0;


static void writeWordLE( uint8_t *memory, uint32_t address, uint32_t value ) {
	for ( uint32_t i = 0; i < 4; i ++ ) memory[address + i] = (uint8_t)(value >> (i * 8));
}

static void printFailure( const char *name, const char *message ) {
	printf( "[TEST %02d] FAIL: %s\n", testCnt, name );
	printf( "%s\n", message );
	exit(1);
}

static void printPass( const char *name ) {
	passCnt ++;
	printf( "[TEST %02d] PASS: %s\n", testCnt, name );
}

static void expectStatus(
	const char *name,
	RV32IStepStatus actual,
	RV32IStepStatus expected
) {
	testCnt ++;
	if ( actual != expected ) printFailure(name, "Unexpected step status." );
	printPass(name);
}

static void expectValue( const char *name, uint32_t actual, uint32_t expected ) {
	testCnt ++;
	if ( actual != expected ) {
		printf( "Expected: 0x%08x\n", expected );
		printf( "Actual  : 0x%08x\n", actual );
		printFailure(name, "Architectural state mismatch." );
	}
	printPass(name);
}

static void initializeTest(
	uint8_t memoryData[MEMORY_SIZE],
	Memory *memory,
	ProcessorState *state
) {
	memset(memoryData, 0, MEMORY_SIZE);
	initializeMemory(memory, memoryData, MEMORY_SIZE, EXECUTABLE_LIMIT);
	initializeProcessorState(state, 0);
}

static void testFenceAndBreak( void ) {
	uint8_t memoryData[MEMORY_SIZE];
	Memory memory;
	ProcessorState state;
	RV32IStepResult result;
	initializeTest(memoryData, &memory, &state);
	writeWordLE(memoryData, 0, 0x0ff0000f);
	writeWordLE(memoryData, 4, 0x00100073);

	expectStatus("Execute FENCE", stepRV32I(&state, &memory, &result), RV32I_STEP_OK);
	expectValue("Advance after FENCE", state.pc, 4);
	expectStatus("Stop on EBREAK", stepRV32I(&state, &memory, &result), RV32I_STEP_EBREAK);
	expectValue("Count fetched instructions", (uint32_t)state.instCnt, 2);
}

static void testJALROverlap( void ) {
	uint8_t memoryData[MEMORY_SIZE];
	Memory memory;
	ProcessorState state;
	RV32IStepResult result;
	initializeTest(memoryData, &memory, &state);
	writeWordLE(memoryData, 0, 0x01100093); // addi x1, x0, 17
	writeWordLE(memoryData, 4, 0x000080e7); // jalr x1, 0(x1)
	writeWordLE(memoryData, 16, 0x00700193); // addi x3, x0, 7

	expectStatus("Prepare JALR target", stepRV32I(&state, &memory, &result), RV32I_STEP_OK);
	expectStatus("Execute overlapping JALR", stepRV32I(&state, &memory, &result), RV32I_STEP_OK);
	expectValue("Use old rs1 value for JALR target", state.pc, 16);
	expectValue("Write JALR return address", state.reg[1], 8);
	expectStatus("Execute JALR target", stepRV32I(&state, &memory, &result), RV32I_STEP_OK);
	expectValue("Execute instruction at target", state.reg[3], 7);
}

static void testTraps( void ) {
	uint8_t memoryData[MEMORY_SIZE];
	Memory memory;
	ProcessorState state;
	RV32IStepResult result;

	initializeTest(memoryData, &memory, &state);
	writeWordLE(memoryData, 0, 0x00000073);
	expectStatus("Trap on ECALL", stepRV32I(&state, &memory, &result), RV32I_STEP_ECALL);

	initializeTest(memoryData, &memory, &state);
	writeWordLE(memoryData, 0, 0xffffffff);
	expectStatus("Trap on illegal instruction", stepRV32I(&state, &memory, &result),
	             RV32I_STEP_ILLEGAL_INSTRUCTION);

	initializeTest(memoryData, &memory, &state);
	state.pc = 2;
	expectStatus("Trap on misaligned fetch", stepRV32I(&state, &memory, &result),
	             RV32I_STEP_INSTRUCTION_MISALIGNED);

	initializeTest(memoryData, &memory, &state);
	state.pc = EXECUTABLE_LIMIT;
	expectStatus("Trap on out-of-bounds fetch", stepRV32I(&state, &memory, &result),
	             RV32I_STEP_INSTRUCTION_ACCESS_FAULT);
}

static void testRegisterAndMemorySemantics( void ) {
	uint8_t memoryData[MEMORY_SIZE];
	Memory memory;
	ProcessorState state;
	RV32IStepResult result;
	initializeTest(memoryData, &memory, &state);

	writeWordLE(memoryData, 0, 0x00500013);  // addi x0, x0, 5
	writeWordLE(memoryData, 4, 0x000100b7);  // lui x1, 0x10
	writeWordLE(memoryData, 8, 0xffc08093);  // addi x1, x1, -4
	writeWordLE(memoryData, 12, 0x12345137); // lui x2, 0x12345
	writeWordLE(memoryData, 16, 0x67810113); // addi x2, x2, 0x678
	writeWordLE(memoryData, 20, 0x0020a023); // sw x2, 0(x1)
	writeWordLE(memoryData, 24, 0x0000a183); // lw x3, 0(x1)

	for ( int i = 0; i < 7; i ++ ) {
		expectStatus("Execute register/memory instruction",
		             stepRV32I(&state, &memory, &result), RV32I_STEP_OK);
	}
	expectValue("Keep x0 immutable", state.reg[0], 0);
	expectValue("Load stored word", state.reg[3], 0x12345678);
}


int main( void ) {
	printf( "---------------------------------------------------------------------\n" );
	printf( "[STEP 1] Checking fetch, decode, and control flow.\n" );
	printf( "---------------------------------------------------------------------\n" );
	testFenceAndBreak();
	testJALROverlap();

	printf( "---------------------------------------------------------------------\n" );
	printf( "[STEP 2] Checking traps and architectural state.\n" );
	printf( "---------------------------------------------------------------------\n" );
	testTraps();
	testRegisterAndMemorySemantics();

	printf( "---------------------------------------------------------------------\n" );
	printf( "[SUMMARY] %d/%d execution tests passed.\n", passCnt, testCnt );
	printf( "---------------------------------------------------------------------\n" );
	return 0;
}
