#include "assembler.h"
#include "rv32i.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>


#define MEMORY_SIZE 0x10000
#define TEXT_OFFSET 0x0000
#define DATA_OFFSET 0x8000


static int testCnt = 0;
static int passCnt = 0;


static uint32_t readWordLE( const uint8_t *memory, uint32_t address ) {
	uint32_t value = 0;
	for ( uint32_t i = 0; i < 4; i ++ ) value |= ((uint32_t)memory[address + i]) << (i * 8);
	return value;
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

static void expectWord(
	const char *name,
	const uint8_t *memory,
	uint32_t address,
	uint32_t expected
) {
	testCnt ++;
	uint32_t actual = readWordLE(memory, address);
	if ( actual != expected ) {
		printf( "Expected: 0x%08x\n", expected );
		printf( "Actual  : 0x%08x\n", actual );
		printFailure(name, "Machine instruction encoding mismatch." );
	}
	printPass(name);
}

static void expectValue( const char *name, uint32_t actual, uint32_t expected ) {
	testCnt ++;
	if ( actual != expected ) {
		printf( "Expected: %u (0x%08x)\n", expected, expected );
		printf( "Actual  : %u (0x%08x)\n", actual, actual );
		printFailure(name, "Assembler result mismatch." );
	}
	printPass(name);
}

static void assembleOrFail(
	const char *name,
	const char *filename,
	uint8_t *memory,
	AssemblerResult *result
) {
	AssemblerError error;
	testCnt ++;
	if ( !assembleRV32I(filename, memory, MEMORY_SIZE, TEXT_OFFSET, DATA_OFFSET,
	                    result, &error) ) {
		printf( "Assembler error at line %d: %s\n", error.line, error.message );
		printFailure(name, "Assembler rejected valid source." );
	}
	printPass(name);
}

static void testCoreEncoding( void ) {
	uint8_t memory[MEMORY_SIZE];
	AssemblerResult result;
	assembleOrFail("Assemble canonical RV32I source", "tests/asm/assembler_core.s", memory, &result);

	expectWord("Encode ADD", memory, 0x0000, 0x003100b3);
	expectWord("Encode ADDI", memory, 0x0004, 0xfff28213);
	expectWord("Encode SW", memory, 0x0008, 0x0063a623);
	expectWord("Encode BEQ label offset", memory, 0x000c, 0x00208663);
	expectWord("Encode JAL label offset", memory, 0x0010, 0x00c000ef);
	expectWord("Encode LUI", memory, 0x0014, 0x12345437);
	expectWord("Encode SLLI", memory, 0x0018, 0x01f51493);
	expectWord("Encode HCF as EBREAK", memory, 0x001c, 0x00100073);

	expectValue("Set entry address", result.entryAddr, 0x0000);
	expectValue("Count encoded instructions", result.instructionCnt, 8);
	expectValue("Track text end", result.textEnd, 0x0020);
	expectValue("Track data end", result.dataEnd, 0x8008);

	testCnt ++;
	if ( memory[0x8000] != 0x78 || memory[0x8001] != 0x56 ||
	     memory[0x8002] != 0x34 || memory[0x8003] != 0x12 ||
	     memory[0x8004] != 0xff || memory[0x8005] != 0xcd ||
	     memory[0x8006] != 0xab || memory[0x8007] != 0x00 ) {
		printFailure("Write little-endian data directives", "Data bytes were encoded incorrectly." );
	}
	printPass("Write little-endian data directives");

	for ( uint32_t address = 0; address < 0x20; address += 4 ) {
		DecodedInstr instr;
		testCnt ++;
		if ( !decodeRV32I(readWordLE(memory, address), &instr) ) {
			printFailure("Decode generated instruction", "Generated word was not valid RV32I." );
		}
		printPass("Decode generated instruction");
	}
}

static void testPseudoEncoding( void ) {
	uint8_t memory[MEMORY_SIZE];
	AssemblerResult result;
	assembleOrFail("Assemble pseudo-instructions", "tests/asm/assembler_pseudo.s", memory, &result);

	expectWord("Expand NOP", memory, 0x0000, 0x00000013);
	expectWord("Expand short LI", memory, 0x0004, 0x00700093);
	expectWord("Expand long LI upper", memory, 0x0008, 0x12345137);
	expectWord("Expand long LI lower", memory, 0x000c, 0x67810113);
	expectWord("Expand LA upper", memory, 0x0010, 0x00008197);
	expectWord("Expand LA lower", memory, 0x0014, 0xff018193);
	expectWord("Expand CALL", memory, 0x0018, 0x008000ef);
	expectWord("Expand J", memory, 0x001c, 0x00c0006f);
	expectWord("Expand MV", memory, 0x0020, 0x00018213);
	expectWord("Expand RET", memory, 0x0024, 0x00008067);
	expectWord("Expand HCF", memory, 0x0028, 0x00100073);
	expectValue("Count expanded instructions", result.instructionCnt, 11);
}

static void testGCCStyleSource( void ) {
	uint8_t memory[MEMORY_SIZE];
	AssemblerResult result;
	assembleOrFail("Assemble GCC-style labels and pseudo-instructions",
	               "tests/asm/assembler_gcc_style.s", memory, &result);

	expectValue("Count GCC-style expanded instructions", result.instructionCnt, 14);
	for ( uint32_t address = 0; address < result.textEnd; address += 4 ) {
		DecodedInstr instr;
		testCnt ++;
		if ( !decodeRV32I(readWordLE(memory, address), &instr) ) {
			printFailure("Decode GCC-style generated instruction",
			             "Generated word was not valid RV32I." );
		}
		printPass("Decode GCC-style generated instruction");
	}
}

static void testInvalidSource( void ) {
	uint8_t memory[MEMORY_SIZE];
	AssemblerResult result;
	AssemblerError error;

	testCnt ++;
	if ( assembleRV32I("tests/asm/assembler_invalid.s", memory, MEMORY_SIZE,
	                   TEXT_OFFSET, DATA_OFFSET, &result, &error) ) {
		printFailure("Reject undefined label", "Assembler accepted an undefined label." );
	}
	if ( strstr(error.message, "Undefined label") == NULL ) {
		printFailure("Reject undefined label", "Assembler returned the wrong error message." );
	}
	printPass("Reject undefined label");
}

static void testWorkingExamples( void ) {
	const char *examples[] = {
		"examples/reduction.s",
		"examples/sort.s",
		"examples/graph.s",
		"examples/sudoku.s"
	};

	for ( uint32_t i = 0; i < sizeof(examples) / sizeof(examples[0]); i ++ ) {
		uint8_t memory[MEMORY_SIZE];
		AssemblerResult result;
		AssemblerError error;
		testCnt ++;
		if ( !assembleRV32I(examples[i], memory, MEMORY_SIZE, TEXT_OFFSET, DATA_OFFSET,
		                    &result, &error) ) {
			printf( "Example: %s\n", examples[i] );
			printf( "Assembler error at line %d: %s\n", error.line, error.message );
			printFailure("Assemble working example", "Assembler rejected an existing example." );
		}
		printPass("Assemble working example");
	}
}


int main( void ) {
	printf( "---------------------------------------------------------------------\n" );
	printf( "[STEP 1] Checking canonical RV32I instruction encoding.\n" );
	printf( "---------------------------------------------------------------------\n" );
	testCoreEncoding();

	printf( "---------------------------------------------------------------------\n" );
	printf( "[STEP 2] Checking pseudo-instruction expansion.\n" );
	printf( "---------------------------------------------------------------------\n" );
	testPseudoEncoding();
	testGCCStyleSource();

	printf( "---------------------------------------------------------------------\n" );
	printf( "[STEP 3] Checking errors and existing examples.\n" );
	printf( "---------------------------------------------------------------------\n" );
	testInvalidSource();
	testWorkingExamples();

	printf( "---------------------------------------------------------------------\n" );
	printf( "[SUMMARY] %d/%d assembler tests passed.\n", passCnt, testCnt );
	printf( "---------------------------------------------------------------------\n" );
	return 0;
}
