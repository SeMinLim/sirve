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


bool loadRawBinary(
	const char *filename,
	uint8_t *memory,
	uint32_t memorySize,
	uint32_t loadAddr,
	uint32_t entryAddr,
	RawBinaryResult *result,
	LoaderError *error
);

#endif
