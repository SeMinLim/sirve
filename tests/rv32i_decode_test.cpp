#include "rv32i.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


typedef struct {
	const char *name;
	uint32_t raw;
	RV32IInstrType op;
} DecodeTest;


static int testCnt = 0;
static int passCnt = 0;


static void printFailure( const char *name, uint32_t raw, const char *message ) {
	printf( "[TEST %02d] FAIL: %s\n", testCnt, name );
	printf( "Raw instruction: 0x%08x\n", raw );
	printf( "%s\n", message );
	exit(1);
}

static void printPass( const char *name ) {
	passCnt ++;
	printf( "[TEST %02d] PASS: %s\n", testCnt, name );
}

static void expectDecode( const DecodeTest *test ) {
	DecodedInstr instr;
	testCnt ++;
	if ( !decodeRV32I(test->raw, &instr) ) {
		printFailure(test->name, test->raw, "Decoder rejected a valid RV32I instruction." );
	}
	if ( instr.op != test->op ) {
		printFailure(test->name, test->raw, "Decoder returned the wrong instruction type." );
	}
	printPass(test->name);
}

static void expectInvalid( const char *name, uint32_t raw ) {
	DecodedInstr instr;
	testCnt ++;
	if ( decodeRV32I(raw, &instr) || instr.op != RV32I_INVALID ) {
		printFailure(name, raw, "Decoder accepted an invalid or unsupported instruction." );
	}
	printPass(name);
}

static void expectValue( const char *name, int32_t actual, int32_t expected ) {
	testCnt ++;
	if ( actual != expected ) {
		printf( "Expected: %d (0x%08x)\n", expected, (uint32_t)expected );
		printf( "Actual  : %d (0x%08x)\n", actual, (uint32_t)actual );
		printFailure(name, 0, "Immediate decoding mismatch." );
	}
	printPass(name);
}


int main( void ) {
	const DecodeTest decodeTests[] = {
		{ "Decode LUI",    0x123450b7, RV32I_LUI },
		{ "Decode AUIPC",  0x23456117, RV32I_AUIPC },
		{ "Decode JAL",    0x008000ef, RV32I_JAL },
		{ "Decode JALR",   0x00c100e7, RV32I_JALR },
		{ "Decode BEQ",    0x00208463, RV32I_BEQ },
		{ "Decode BNE",    0x00209463, RV32I_BNE },
		{ "Decode BLT",    0x0020c463, RV32I_BLT },
		{ "Decode BGE",    0x0020d463, RV32I_BGE },
		{ "Decode BLTU",   0x0020e463, RV32I_BLTU },
		{ "Decode BGEU",   0x0020f463, RV32I_BGEU },
		{ "Decode LB",     0x00c10083, RV32I_LB },
		{ "Decode LH",     0x00c11083, RV32I_LH },
		{ "Decode LW",     0x00c12083, RV32I_LW },
		{ "Decode LBU",    0x00c14083, RV32I_LBU },
		{ "Decode LHU",    0x00c15083, RV32I_LHU },
		{ "Decode SB",     0x00310623, RV32I_SB },
		{ "Decode SH",     0x00311623, RV32I_SH },
		{ "Decode SW",     0x00312623, RV32I_SW },
		{ "Decode ADDI",   0xfff10093, RV32I_ADDI },
		{ "Decode SLTI",   0xfff12093, RV32I_SLTI },
		{ "Decode SLTIU",  0xfff13093, RV32I_SLTIU },
		{ "Decode XORI",   0xfff14093, RV32I_XORI },
		{ "Decode ORI",    0xfff16093, RV32I_ORI },
		{ "Decode ANDI",   0xfff17093, RV32I_ANDI },
		{ "Decode SLLI",   0x01f11093, RV32I_SLLI },
		{ "Decode SRLI",   0x01f15093, RV32I_SRLI },
		{ "Decode SRAI",   0x41f15093, RV32I_SRAI },
		{ "Decode ADD",    0x003100b3, RV32I_ADD },
		{ "Decode SUB",    0x403100b3, RV32I_SUB },
		{ "Decode SLL",    0x003110b3, RV32I_SLL },
		{ "Decode SLT",    0x003120b3, RV32I_SLT },
		{ "Decode SLTU",   0x003130b3, RV32I_SLTU },
		{ "Decode XOR",    0x003140b3, RV32I_XOR },
		{ "Decode SRL",    0x003150b3, RV32I_SRL },
		{ "Decode SRA",    0x403150b3, RV32I_SRA },
		{ "Decode OR",     0x003160b3, RV32I_OR },
		{ "Decode AND",    0x003170b3, RV32I_AND },
		{ "Decode FENCE",  0x0ff0000f, RV32I_FENCE },
		{ "Decode ECALL",  0x00000073, RV32I_ECALL },
		{ "Decode EBREAK", 0x00100073, RV32I_EBREAK }
	};

	printf( "---------------------------------------------------------------------\n" );
	printf( "[STEP 1] Decoding all RV32I instructions.\n" );
	printf( "---------------------------------------------------------------------\n" );
	for ( uint32_t i = 0; i < sizeof(decodeTests) / sizeof(decodeTests[0]); i ++ ) {
		expectDecode(&decodeTests[i]);
	}

	printf( "---------------------------------------------------------------------\n" );
	printf( "[STEP 2] Checking immediate boundaries.\n" );
	printf( "---------------------------------------------------------------------\n" );
	expectValue("I-immediate minimum", decodeImmediateI(0x80010093), -2048);
	expectValue("I-immediate maximum", decodeImmediateI(0x7ff10093), 2047);
	expectValue("S-immediate minimum", decodeImmediateS(0x80312023), -2048);
	expectValue("S-immediate maximum", decodeImmediateS(0x7e312fa3), 2047);
	expectValue("B-immediate minimum", decodeImmediateB(0x80208063), -4096);
	expectValue("B-immediate maximum", decodeImmediateB(0x7e208fe3), 4094);
	expectValue("U-immediate positive", decodeImmediateU(0x123450b7), 0x12345000);
	expectValue("U-immediate sign bit", decodeImmediateU(0x800000b7), INT32_MIN);
	expectValue("J-immediate minimum", decodeImmediateJ(0x800000ef), -1048576);
	expectValue("J-immediate maximum", decodeImmediateJ(0x7ffff0ef), 1048574);

	printf( "---------------------------------------------------------------------\n" );
	printf( "[STEP 3] Rejecting invalid encodings.\n" );
	printf( "---------------------------------------------------------------------\n" );
	expectInvalid("Reject unknown opcode", 0xffffffff);
	expectInvalid("Reject invalid JALR funct3", 0x00001067);
	expectInvalid("Reject invalid branch funct3", 0x00002063);
	expectInvalid("Reject invalid load funct3", 0x00003003);
	expectInvalid("Reject invalid store funct3", 0x00003023);
	expectInvalid("Reject invalid SLLI funct7", 0x02011093);
	expectInvalid("Reject RV32M encoding", 0x023100b3);
	expectInvalid("Reject FENCE.I encoding", 0x0000100f);
	expectInvalid("Reject Zicsr encoding", 0x300110f3);

	printf( "---------------------------------------------------------------------\n" );
	printf( "[SUMMARY] %d/%d decoder tests passed.\n", passCnt, testCnt );
	printf( "---------------------------------------------------------------------\n" );
	return 0;
}
