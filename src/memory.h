#ifndef SIRVE_MEMORY_H
#define SIRVE_MEMORY_H

#include <stdint.h>


typedef enum {
	MEMORY_STATUS_OK = 0,
	MEMORY_STATUS_MISALIGNED,
	MEMORY_STATUS_OUT_OF_BOUNDS
} MemoryStatus;


typedef struct {
	uint8_t *data;
	uint32_t size;
	uint32_t executableLimit;
	uint64_t readReqCnt;
	uint64_t writeReqCnt;
	uint64_t readHitCnt;
	uint64_t writeHitCnt;
	uint64_t flushWordCnt;
} Memory;


void initializeMemory(
	Memory *memory,
	uint8_t *data,
	uint32_t size,
	uint32_t executableLimit
);

MemoryStatus memoryFetch32( const Memory *memory, uint32_t addr, uint32_t *data );
MemoryStatus memoryRead8( Memory *memory, uint32_t addr, uint32_t *data );
MemoryStatus memoryRead16( Memory *memory, uint32_t addr, uint32_t *data );
MemoryStatus memoryRead32( Memory *memory, uint32_t addr, uint32_t *data );
MemoryStatus memoryWrite8( Memory *memory, uint32_t addr, uint32_t data );
MemoryStatus memoryWrite16( Memory *memory, uint32_t addr, uint32_t data );
MemoryStatus memoryWrite32( Memory *memory, uint32_t addr, uint32_t data );
bool memoryInspect8( const Memory *memory, uint32_t addr, uint32_t *data );

#endif
