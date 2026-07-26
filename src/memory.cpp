#include "memory.h"

#include <stdio.h>
#include <string.h>

#include "cache.h"


static uint32_t readWordLE( const uint8_t *data, uint32_t addr ) {
	uint32_t value = 0;
	for ( uint32_t i = 0; i < 4; i ++ ) {
		value |= ((uint32_t)data[addr + i]) << (i * 8);
	}
	return value;
}

static bool accessInBounds( const Memory *memory, uint32_t addr, uint32_t bytes ) {
	if ( memory == NULL || memory->data == NULL || bytes == 0 ) return false;
	if ( addr >= memory->size ) return false;
	return bytes <= memory->size - addr;
}

static MemoryStatus refillCacheLine( Memory *memory, uint32_t addr ) {
	uint32_t lineBytes = CACHE_LINE_WORD * 4;
	uint32_t lineAddr = addr & ~(lineBytes - 1u);
	if ( lineAddr >= memory->size || lineBytes > memory->size - lineAddr ) {
		return MEMORY_STATUS_OUT_OF_BOUNDS;
	}

	cacheFlush(addr, memory->data);
	for ( uint32_t i = 0; i < CACHE_LINE_WORD; i ++ ) {
		uint32_t wordAddr = lineAddr + (i * 4);
		cacheUpdate(wordAddr, readWordLE(memory->data, wordAddr));
	}
	memory->flushWordCnt += CACHE_LINE_WORD;
	return MEMORY_STATUS_OK;
}

static MemoryStatus memoryRead(
	Memory *memory,
	uint32_t addr,
	uint32_t bytes,
	uint32_t *data
) {
	if ( data == NULL || !accessInBounds(memory, addr, bytes) ) {
		return MEMORY_STATUS_OUT_OF_BOUNDS;
	}
	if ( bytes > 1 && (addr & (bytes - 1u)) != 0 ) {
		return MEMORY_STATUS_MISALIGNED;
	}

	memory->readReqCnt ++;
	if ( cachePeek(addr, bytes) < 0 ) {
		MemoryStatus status = refillCacheLine(memory, addr);
		if ( status != MEMORY_STATUS_OK ) return status;
	} else {
		memory->readHitCnt ++;
	}

	*data = cacheRead(addr, bytes);
	return MEMORY_STATUS_OK;
}

static MemoryStatus memoryWrite(
	Memory *memory,
	uint32_t addr,
	uint32_t bytes,
	uint32_t data
) {
	if ( memory == NULL ) return MEMORY_STATUS_OUT_OF_BOUNDS;

	if ( addr == memory->size ) {
		printf( "[System output]: 0x%x\n", data );
		fflush( stdout );
		return MEMORY_STATUS_OK;
	}
	if ( !accessInBounds(memory, addr, bytes) ) return MEMORY_STATUS_OUT_OF_BOUNDS;
	if ( bytes > 1 && (addr & (bytes - 1u)) != 0 ) {
		return MEMORY_STATUS_MISALIGNED;
	}

	memory->writeReqCnt ++;
	if ( cachePeek(addr, bytes) < 0 ) {
		MemoryStatus status = refillCacheLine(memory, addr);
		if ( status != MEMORY_STATUS_OK ) return status;
	} else {
		memory->writeHitCnt ++;
	}

	cacheWrite(addr, data, bytes);
	return MEMORY_STATUS_OK;
}


void initializeMemory(
	Memory *memory,
	uint8_t *data,
	uint32_t size,
	uint32_t executableLimit
) {
	if ( memory == NULL ) return;

	memory->data = data;
	memory->size = size;
	memory->executableLimit = executableLimit;
	memory->readReqCnt = 0;
	memory->writeReqCnt = 0;
	memory->readHitCnt = 0;
	memory->writeHitCnt = 0;
	memory->flushWordCnt = 0;
	cacheReset();
}

MemoryStatus memoryFetch32( const Memory *memory, uint32_t addr, uint32_t *data ) {
	if ( data == NULL || memory == NULL || memory->data == NULL ) {
		return MEMORY_STATUS_OUT_OF_BOUNDS;
	}
	if ( (addr & 0x3u) != 0 ) return MEMORY_STATUS_MISALIGNED;
	if ( addr >= memory->executableLimit || 4 > memory->executableLimit - addr ) {
		return MEMORY_STATUS_OUT_OF_BOUNDS;
	}

	*data = readWordLE(memory->data, addr);
	return MEMORY_STATUS_OK;
}

MemoryStatus memoryRead8( Memory *memory, uint32_t addr, uint32_t *data ) {
	return memoryRead(memory, addr, 1, data);
}

MemoryStatus memoryRead16( Memory *memory, uint32_t addr, uint32_t *data ) {
	return memoryRead(memory, addr, 2, data);
}

MemoryStatus memoryRead32( Memory *memory, uint32_t addr, uint32_t *data ) {
	return memoryRead(memory, addr, 4, data);
}

MemoryStatus memoryWrite8( Memory *memory, uint32_t addr, uint32_t data ) {
	return memoryWrite(memory, addr, 1, data);
}

MemoryStatus memoryWrite16( Memory *memory, uint32_t addr, uint32_t data ) {
	return memoryWrite(memory, addr, 2, data);
}

MemoryStatus memoryWrite32( Memory *memory, uint32_t addr, uint32_t data ) {
	return memoryWrite(memory, addr, 4, data);
}

bool memoryInspect8( const Memory *memory, uint32_t addr, uint32_t *data ) {
	if ( data == NULL || !accessInBounds(memory, addr, 1) ) return false;

	uint32_t cacheValue = 0;
	if ( cacheReadPresent(addr, 1, &cacheValue) ) {
		*data = cacheValue & 0xffu;
	} else {
		*data = memory->data[addr];
	}
	return true;
}
