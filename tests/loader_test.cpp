#include "loader.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>


#define MEMORY_SIZE 0x10000
#define LOAD_ADDR 0x0100
#define ENTRY_ADDR 0x0100


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

static void writeBinary( const char *filename, const uint8_t *data, size_t size ) {
	FILE *output = fopen(filename, "wb");
	if ( output == NULL ) printFailure("Create raw binary", "Unable to create temporary binary.");
	if ( size > 0 && fwrite(data, 1, size, output) != size ) {
		fclose(output);
		printFailure("Create raw binary", "Unable to write temporary binary.");
	}
	if ( fclose(output) != 0 ) {
		printFailure("Create raw binary", "Unable to close temporary binary.");
	}
}

static void expectLoadFailure(
	const char *name,
	const char *filename,
	uint8_t *memory,
	uint32_t memorySize,
	uint32_t loadAddr,
	uint32_t entryAddr,
	const char *expectedMessage
) {
	RawBinaryResult result;
	LoaderError error;
	testCnt ++;
	if ( loadRawBinary(filename, memory, memorySize, loadAddr, entryAddr, &result, &error) ) {
		printFailure(name, "Loader accepted invalid input.");
	}
	if ( strstr(error.message, expectedMessage) == NULL ) {
		printf( "Loader error: %s\n", error.message );
		printFailure(name, "Loader returned the wrong error message.");
	}
	printPass(name);
}


int main( void ) {
	char binaryPath[] = "/tmp/sirve-loader-test-XXXXXX";
	int binaryFd = mkstemp(binaryPath);
	if ( binaryFd < 0 ) {
		printf( "Unable to create temporary binary file\n" );
		return 1;
	}
	close(binaryFd);

	const uint8_t binaryData[] = {
		0x93, 0x00, 0x70, 0x00, // addi x1, x0, 7
		0x73, 0x00, 0x10, 0x00  // ebreak
	};
	writeBinary(binaryPath, binaryData, sizeof(binaryData));

	uint8_t memoryData[MEMORY_SIZE] = {0};
	RawBinaryResult result;
	LoaderError error;

	printf( "---------------------------------------------------------------------\n" );
	printf( "[STEP 1] Loading a raw RV32I binary.\n" );
	printf( "---------------------------------------------------------------------\n" );

	expectTrue(
		"Load raw binary",
		loadRawBinary(binaryPath, memoryData, MEMORY_SIZE, LOAD_ADDR, ENTRY_ADDR,
		              &result, &error),
		error.message
	);
	expectValue("Track load address", result.loadAddr, LOAD_ADDR);
	expectValue("Track entry address", result.entryAddr, ENTRY_ADDR);
	expectValue("Track loaded byte count", result.loadedByteCnt, sizeof(binaryData));
	expectValue("Track loaded end", result.loadedEnd, LOAD_ADDR + sizeof(binaryData));
	expectValue("Load first instruction little-endian",
	            ((uint32_t)memoryData[LOAD_ADDR]) |
	            ((uint32_t)memoryData[LOAD_ADDR + 1] << 8) |
	            ((uint32_t)memoryData[LOAD_ADDR + 2] << 16) |
	            ((uint32_t)memoryData[LOAD_ADDR + 3] << 24),
	            0x00700093);

	Memory memory;
	initializeMemory(&memory, memoryData, MEMORY_SIZE, MEMORY_SIZE);
	expectTrue(
		"Set raw-binary executable range",
		setMemoryExecutableRange(&memory, result.loadAddr, result.loadedEnd),
		"Memory rejected a valid executable range."
	);

	uint32_t raw = 0;
	expectValue("Fetch first raw instruction status",
	            memoryFetch32(&memory, LOAD_ADDR, &raw), MEMORY_STATUS_OK);
	expectValue("Fetch first raw instruction", raw, 0x00700093);
	expectValue("Fetch second raw instruction status",
	            memoryFetch32(&memory, LOAD_ADDR + 4, &raw), MEMORY_STATUS_OK);
	expectValue("Fetch second raw instruction", raw, 0x00100073);
	expectValue("Reject fetch below raw image",
	            memoryFetch32(&memory, 0, &raw), MEMORY_STATUS_OUT_OF_BOUNDS);
	expectValue("Reject fetch beyond raw image",
	            memoryFetch32(&memory, LOAD_ADDR + 8, &raw), MEMORY_STATUS_OUT_OF_BOUNDS);

	printf( "---------------------------------------------------------------------\n" );
	printf( "[STEP 2] Rejecting invalid raw-binary inputs.\n" );
	printf( "---------------------------------------------------------------------\n" );

	expectLoadFailure("Reject missing raw binary", "/tmp/sirve-loader-file-does-not-exist",
	                  memoryData, MEMORY_SIZE, LOAD_ADDR, ENTRY_ADDR,
	                  "Unable to open raw binary");
	expectLoadFailure("Reject misaligned load address", binaryPath,
	                  memoryData, MEMORY_SIZE, LOAD_ADDR + 1, ENTRY_ADDR,
	                  "load address is not 4-byte aligned");
	expectLoadFailure("Reject misaligned entry address", binaryPath,
	                  memoryData, MEMORY_SIZE, LOAD_ADDR, ENTRY_ADDR + 2,
	                  "entry address is not 4-byte aligned");
	expectLoadFailure("Reject entry outside image", binaryPath,
	                  memoryData, MEMORY_SIZE, LOAD_ADDR, LOAD_ADDR + sizeof(binaryData),
	                  "entry address is outside");
	expectLoadFailure("Reject image outside memory", binaryPath,
	                  memoryData, LOAD_ADDR + 4, LOAD_ADDR, ENTRY_ADDR,
	                  "exceeds emulated memory");

	writeBinary(binaryPath, NULL, 0);
	expectLoadFailure("Reject empty raw binary", binaryPath,
	                  memoryData, MEMORY_SIZE, LOAD_ADDR, ENTRY_ADDR,
	                  "Raw binary is empty");

	unlink(binaryPath);
	printf( "---------------------------------------------------------------------\n" );
	printf( "[SUMMARY] %d/%d loader tests passed.\n", passCnt, testCnt );
	printf( "---------------------------------------------------------------------\n" );
	return 0;
}
