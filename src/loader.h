#ifndef SIRVE_LOADER_H
#define SIRVE_LOADER_H

#include <stdint.h>


#define LOADER_ERROR_LEN 192


typedef struct {
	char message[LOADER_ERROR_LEN];
} LoaderError;


typedef struct {
	uint32_t loadAddr;
	uint32_t entryAddr;
	uint32_t loadedByteCnt;
	uint32_t loadedEnd;
} RawBinaryResult;


typedef struct {
	uint32_t entryAddr;
	uint32_t imageStart;
	uint32_t imageEnd;
	uint32_t executableStart;
	uint32_t executableLimit;
	uint32_t segmentCnt;
	uint32_t loadedByteCnt;
	uint32_t zeroByteCnt;
} Elf32Result;


bool loadRawBinary(
	const char *filename,
	uint8_t *memory,
	uint32_t memorySize,
	uint32_t loadAddr,
	uint32_t entryAddr,
	RawBinaryResult *result,
	LoaderError *error
);

bool loadElf32(
	const char *filename,
	uint8_t *memory,
	uint32_t memorySize,
	Elf32Result *result,
	LoaderError *error
);

#endif
