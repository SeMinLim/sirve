#ifndef SIRVE_ASSEMBLER_H
#define SIRVE_ASSEMBLER_H

#include <stddef.h>
#include <stdint.h>


#define ASSEMBLER_ERROR_LEN 192
#define ASSEMBLER_SOURCE_LEN 128


typedef struct {
	int line;
	char message[ASSEMBLER_ERROR_LEN];
} AssemblerError;


typedef struct {
	int line;
	char text[ASSEMBLER_SOURCE_LEN];
} AssemblerSourceEntry;


typedef struct {
	uint32_t entryAddr;
	uint32_t textEnd;
	uint32_t dataEnd;
	uint32_t instructionCnt;
	uint32_t writtenByteCnt;
} AssemblerResult;


bool assembleRV32I(
	const char *filename,
	uint8_t *memory,
	uint32_t memorySize,
	uint32_t textOffset,
	uint32_t dataOffset,
	AssemblerResult *result,
	AssemblerError *error,
	AssemblerSourceEntry *sourceMap = NULL,
	uint32_t sourceMapCnt = 0
);

#endif
