#include "loader.h"
#include "memory.h"
#include "rv32i.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>


#define MEMORY_SIZE 0x10000
#define ELF_FILE_SIZE 0x204
#define TEXT_ADDR 0x0100
#define TEXT_FILE_OFFSET 0x0100
#define TEXT_FILE_SIZE 20
#define DATA_ADDR 0x8000
#define DATA_FILE_OFFSET 0x0200
#define DATA_FILE_SIZE 4
#define DATA_MEMORY_SIZE 8


static int testCnt = 0;
static int passCnt = 0;


static void printFailure( const char *name, const char *message ) {
	printf( "[TEST %02d] FAIL: %s\n", testCnt, name );
	printf( "%s\n", message );
	exit(1);
}

static void printPass( const char *name ) {
	passCnt ++;
	printf( "[TEST %02d] PASS: %s\n", testCnt, name );
}

static void expectTrue( const char *name, bool condition, const char *message ) {
	testCnt ++;
	if ( !condition ) printFailure(name, message);
	printPass(name);
}

static void expectValue( const char *name, uint32_t actual, uint32_t expected ) {
	testCnt ++;
	if ( actual != expected ) {
		printf( "Expected: 0x%08x\n", expected );
		printf( "Actual  : 0x%08x\n", actual );
		printFailure(name, "Value mismatch.");
	}
	printPass(name);
}

static void write16LE( uint8_t *data, uint32_t offset, uint16_t value ) {
	data[offset] = (uint8_t)(value & 0xffu);
	data[offset + 1] = (uint8_t)((value >> 8) & 0xffu);
}

static void write32LE( uint8_t *data, uint32_t offset, uint32_t value ) {
	for ( uint32_t i = 0; i < 4; i ++ ) {
		data[offset + i] = (uint8_t)((value >> (i * 8)) & 0xffu);
	}
}

static uint32_t read32LE( const uint8_t *data, uint32_t offset ) {
	uint32_t value = 0;
	for ( uint32_t i = 0; i < 4; i ++ ) {
		value |= ((uint32_t)data[offset + i]) << (i * 8);
	}
	return value;
}

static void writeFile( const char *filename, const uint8_t *data, size_t size ) {
	FILE *output = fopen(filename, "wb");
	if ( output == NULL ) printFailure("Create ELF32 fixture", "Unable to create ELF32 fixture.");
	if ( size > 0 && fwrite(data, 1, size, output) != size ) {
		fclose(output);
		printFailure("Create ELF32 fixture", "Unable to write ELF32 fixture.");
	}
	if ( fclose(output) != 0 ) {
		printFailure("Create ELF32 fixture", "Unable to close ELF32 fixture.");
	}
}

static void buildValidElf( uint8_t elfData[ELF_FILE_SIZE] ) {
	memset(elfData, 0, ELF_FILE_SIZE);

	elfData[0] = 0x7f;
	elfData[1] = 'E';
	elfData[2] = 'L';
	elfData[3] = 'F';
	elfData[4] = 1;
	elfData[5] = 1;
	elfData[6] = 1;

	write16LE(elfData, 16, 2);
	write16LE(elfData, 18, 243);
	write32LE(elfData, 20, 1);
	write32LE(elfData, 24, TEXT_ADDR);
	write32LE(elfData, 28, 52);
	write32LE(elfData, 32, 0);
	write32LE(elfData, 36, 0);
	write16LE(elfData, 40, 52);
	write16LE(elfData, 42, 32);
	write16LE(elfData, 44, 2);
	write16LE(elfData, 46, 40);
	write16LE(elfData, 48, 0);
	write16LE(elfData, 50, 0);

	uint32_t textHeader = 52;
	write32LE(elfData, textHeader, 1);
	write32LE(elfData, textHeader + 4, TEXT_FILE_OFFSET);
	write32LE(elfData, textHeader + 8, TEXT_ADDR);
	write32LE(elfData, textHeader + 12, TEXT_ADDR);
	write32LE(elfData, textHeader + 16, TEXT_FILE_SIZE);
	write32LE(elfData, textHeader + 20, TEXT_FILE_SIZE);
	write32LE(elfData, textHeader + 24, 5);
	write32LE(elfData, textHeader + 28, 0x100);

	uint32_t dataHeader = textHeader + 32;
	write32LE(elfData, dataHeader, 1);
	write32LE(elfData, dataHeader + 4, DATA_FILE_OFFSET);
	write32LE(elfData, dataHeader + 8, DATA_ADDR);
	write32LE(elfData, dataHeader + 12, DATA_ADDR);
	write32LE(elfData, dataHeader + 16, DATA_FILE_SIZE);
	write32LE(elfData, dataHeader + 20, DATA_MEMORY_SIZE);
	write32LE(elfData, dataHeader + 24, 6);
	write32LE(elfData, dataHeader + 28, 0x100);

	write32LE(elfData, TEXT_FILE_OFFSET + 0, 0x00700093);  // addi x1, x0, 7
	write32LE(elfData, TEXT_FILE_OFFSET + 4, 0x000082b7);  // lui x5, 0x8
	write32LE(elfData, TEXT_FILE_OFFSET + 8, 0x0002a183);  // lw x3, 0(x5)
	write32LE(elfData, TEXT_FILE_OFFSET + 12, 0x0042a203); // lw x4, 4(x5)
	write32LE(elfData, TEXT_FILE_OFFSET + 16, 0x00100073); // ebreak
	write32LE(elfData, DATA_FILE_OFFSET, 0x12345678);
}

static void expectElfFileFailure(
	const char *name,
	const char *filename,
	const char *expectedMessage
) {
	uint8_t memory[MEMORY_SIZE];
	memset(memory, 0x5a, sizeof(memory));
	Elf32Result result;
	LoaderError error;

	testCnt ++;
	if ( loadElf32(filename, memory, MEMORY_SIZE, &result, &error) ) {
		printFailure(name, "Loader accepted an invalid ELF32 executable.");
	}
	if ( strstr(error.message, expectedMessage) == NULL ) {
		printf( "Loader error: %s\n", error.message );
		printFailure(name, "Loader returned the wrong error message.");
	}
	for ( uint32_t i = 0; i < MEMORY_SIZE; i ++ ) {
		if ( memory[i] != 0x5a ) {
			printFailure(name, "Loader modified memory before validation completed.");
		}
	}
	printPass(name);
}

static void expectElfFailure(
	const char *name,
	const char *filename,
	const uint8_t elfData[ELF_FILE_SIZE],
	const char *expectedMessage
) {
	writeFile(filename, elfData, ELF_FILE_SIZE);
	expectElfFileFailure(name, filename, expectedMessage);
}

static void testValidElf( const uint8_t elfData[ELF_FILE_SIZE] ) {
	const char *elfPath = "obj/test-rv32i.elf";
	writeFile(elfPath, elfData, ELF_FILE_SIZE);

	uint8_t memoryData[MEMORY_SIZE];
	memset(memoryData, 0xa5, sizeof(memoryData));
	Elf32Result result;
	LoaderError error;

	expectTrue(
		"Load ELF32 executable",
		loadElf32(elfPath, memoryData, MEMORY_SIZE, &result, &error),
		error.message
	);
	expectValue("Track ELF32 entry address", result.entryAddr, TEXT_ADDR);
	expectValue("Track ELF32 image start", result.imageStart, TEXT_ADDR);
	expectValue("Track ELF32 image end", result.imageEnd, DATA_ADDR + DATA_MEMORY_SIZE);
	expectValue("Track executable start", result.executableStart, TEXT_ADDR);
	expectValue("Track executable limit", result.executableLimit, TEXT_ADDR + TEXT_FILE_SIZE);
	expectValue("Count loadable segments", result.segmentCnt, 2);
	expectValue("Count loaded ELF32 bytes", result.loadedByteCnt,
	            TEXT_FILE_SIZE + DATA_FILE_SIZE);
	expectValue("Count zero-filled bytes", result.zeroByteCnt,
	            DATA_MEMORY_SIZE - DATA_FILE_SIZE);
	expectValue("Load first ELF32 instruction", read32LE(memoryData, TEXT_ADDR), 0x00700093);
	expectValue("Load ELF32 data", read32LE(memoryData, DATA_ADDR), 0x12345678);
	expectValue("Zero-initialize ELF32 BSS", read32LE(memoryData, DATA_ADDR + 4), 0);

	Memory memory;
	initializeMemory(&memory, memoryData, MEMORY_SIZE, result.executableLimit);
	expectTrue(
		"Set ELF32 executable range",
		setMemoryExecutableRange(&memory, result.executableStart, result.executableLimit),
		"Memory rejected the ELF32 executable range."
	);

	ProcessorState state;
	initializeProcessorState(&state, result.entryAddr);
	RV32IStepResult stepResult;
	for ( int i = 0; i < 4; i ++ ) {
		expectValue("Execute ELF32 instruction",
		            stepRV32I(&state, &memory, &stepResult), RV32I_STEP_OK);
	}
	expectValue("Stop ELF32 execution on EBREAK",
	            stepRV32I(&state, &memory, &stepResult), RV32I_STEP_EBREAK);
	expectValue("Execute ELF32 ADDI", state.reg[1], 7);
	expectValue("Load ELF32 initialized data", state.reg[3], 0x12345678);
	expectValue("Load ELF32 zero-filled data", state.reg[4], 0);
	expectValue("Count ELF32 instructions", (uint32_t)state.instCnt, 5);
}

static void testExecutableTail(
	const char *tempPath,
	const uint8_t validElf[ELF_FILE_SIZE]
) {
	uint8_t elfData[ELF_FILE_SIZE];
	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 52 + 16, TEXT_FILE_SIZE + 1);
	write32LE(elfData, 52 + 20, TEXT_FILE_SIZE + 1);
	elfData[TEXT_FILE_OFFSET + TEXT_FILE_SIZE] = 0x5a;
	writeFile(tempPath, elfData, sizeof(elfData));

	uint8_t memory[MEMORY_SIZE] = {0};
	Elf32Result result;
	LoaderError error;
	expectTrue(
		"Load ELF32 executable with byte-aligned segment tail",
		loadElf32(tempPath, memory, MEMORY_SIZE, &result, &error),
		error.message
	);
	expectValue("Track byte-aligned executable limit",
	            result.executableLimit, TEXT_ADDR + TEXT_FILE_SIZE + 1);
	expectValue("Load byte-aligned executable tail",
	            memory[TEXT_ADDR + TEXT_FILE_SIZE], 0x5a);
}

static void testInvalidElf(
	const char *tempPath,
	const uint8_t validElf[ELF_FILE_SIZE]
) {
	uint8_t elfData[ELF_FILE_SIZE];

	expectElfFileFailure("Reject missing ELF32 executable",
	                     "/tmp/sirve-elf-loader-file-does-not-exist",
	                     "Unable to open ELF32 executable");

	writeFile(tempPath, NULL, 0);
	expectElfFileFailure("Reject empty ELF32 executable", tempPath,
	                     "ELF32 executable is empty");

	writeFile(tempPath, validElf, 16);
	expectElfFileFailure("Reject truncated ELF32 header", tempPath,
	                     "ELF32 header is truncated");

	memcpy(elfData, validElf, sizeof(elfData));
	elfData[0] = 0;
	expectElfFailure("Reject invalid ELF magic", tempPath, elfData, "Invalid ELF magic");

	memcpy(elfData, validElf, sizeof(elfData));
	elfData[4] = 2;
	expectElfFailure("Reject ELF64 executable", tempPath, elfData, "not 32-bit");

	memcpy(elfData, validElf, sizeof(elfData));
	elfData[5] = 2;
	expectElfFailure("Reject big-endian ELF32 executable", tempPath, elfData, "not little-endian");

	memcpy(elfData, validElf, sizeof(elfData));
	write16LE(elfData, 16, 1);
	expectElfFailure("Reject relocatable ELF32 file", tempPath, elfData, "not an executable");

	memcpy(elfData, validElf, sizeof(elfData));
	write16LE(elfData, 18, 3);
	expectElfFailure("Reject non-RISC-V ELF32 executable", tempPath, elfData, "not for RISC-V");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 36, 1);
	expectElfFailure("Reject compressed-ISA ELF flags", tempPath, elfData,
	                 "Unsupported RISC-V ELF flags");

	memcpy(elfData, validElf, sizeof(elfData));
	write16LE(elfData, 44, 0);
	expectElfFailure("Reject missing ELF32 program headers", tempPath, elfData,
	                 "no program headers");

	memcpy(elfData, validElf, sizeof(elfData));
	write16LE(elfData, 42, 31);
	expectElfFailure("Reject unexpected ELF32 program-header size", tempPath, elfData,
	                 "Unexpected ELF32 program-header size");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 28, ELF_FILE_SIZE - 16);
	expectElfFailure("Reject truncated ELF32 program-header table", tempPath, elfData,
	                 "program-header table is truncated");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 52 + 16, TEXT_FILE_SIZE + 4);
	expectElfFailure("Reject ELF32 file size larger than memory size", tempPath, elfData,
	                 "file size exceeds memory size");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 52 + 32 + 4, ELF_FILE_SIZE);
	expectElfFailure("Reject ELF32 segment outside input file", tempPath, elfData,
	                 "segment exceeds the input file");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 52 + 32 + 8, MEMORY_SIZE - 4);
	write32LE(elfData, 52 + 32 + 12, MEMORY_SIZE - 4);
	expectElfFailure("Reject ELF32 segment outside emulated memory", tempPath, elfData,
	                 "segment exceeds emulated memory");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 52 + 12, TEXT_ADDR + 4);
	expectElfFailure("Reject differing ELF32 load addresses", tempPath, elfData,
	                 "virtual and physical load addresses differ");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 52 + 28, 3);
	expectElfFailure("Reject invalid ELF32 segment alignment", tempPath, elfData,
	                 "segment alignment is invalid");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 52 + 32 + 8, TEXT_ADDR + 0x10);
	write32LE(elfData, 52 + 32 + 12, TEXT_ADDR + 0x10);
	write32LE(elfData, 52 + 32 + 28, 1);
	expectElfFailure("Reject overlapping ELF32 load segments", tempPath, elfData,
	                 "loadable segments overlap");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 52, 0x70000003);
	write32LE(elfData, 52 + 32, 0x70000003);
	expectElfFailure("Reject ELF32 without loadable segments", tempPath, elfData,
	                 "no loadable segments");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 52 + 24, 4);
	expectElfFailure("Reject ELF32 without executable segment", tempPath, elfData,
	                 "no executable load segment");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 24, DATA_ADDR);
	expectElfFailure("Reject ELF32 entry outside executable data", tempPath, elfData,
	                 "entry address is outside executable file data");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 24, TEXT_ADDR + 2);
	expectElfFailure("Reject misaligned ELF32 entry address", tempPath, elfData,
	                 "entry address is not 4-byte aligned");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 52, 2);
	expectElfFailure("Reject dynamic ELF32 executable", tempPath, elfData,
	                 "Dynamic ELF32 executables are not supported");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 52, 7);
	expectElfFailure("Reject ELF32 thread-local storage", tempPath, elfData,
	                 "thread-local storage is not supported");

	memcpy(elfData, validElf, sizeof(elfData));
	write32LE(elfData, 52 + 32 + 24, 7);
	expectElfFailure("Reject noncontiguous executable ELF32 segments", tempPath, elfData,
	                 "Executable ELF32 segments are not contiguous");
}


int main( void ) {
	uint8_t validElf[ELF_FILE_SIZE];
	buildValidElf(validElf);

	char tempPath[] = "/tmp/sirve-elf-loader-test-XXXXXX";
	int tempFd = mkstemp(tempPath);
	if ( tempFd < 0 ) {
		printf( "Unable to create temporary ELF32 file\n" );
		return 1;
	}
	close(tempFd);

	printf( "---------------------------------------------------------------------\n" );
	printf( "[STEP 1] Loading and executing a valid RV32I ELF32 executable.\n" );
	printf( "---------------------------------------------------------------------\n" );
	testValidElf(validElf);
	testExecutableTail(tempPath, validElf);

	printf( "---------------------------------------------------------------------\n" );
	printf( "[STEP 2] Rejecting invalid and unsupported ELF32 files.\n" );
	printf( "---------------------------------------------------------------------\n" );
	testInvalidElf(tempPath, validElf);

	unlink(tempPath);
	printf( "---------------------------------------------------------------------\n" );
	printf( "[SUMMARY] %d/%d ELF32 loader tests passed.\n", passCnt, testCnt );
	printf( "---------------------------------------------------------------------\n" );
	return 0;
}
