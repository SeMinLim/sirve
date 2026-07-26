#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static uint32_t cacheData[CACHE_SETS][CACHE_WAYS][CACHE_LINE_WORD] = {0};
static uint32_t cacheTags[CACHE_SETS][CACHE_WAYS] = {0};
static uint8_t cacheFlags[CACHE_SETS][CACHE_WAYS] = {0};


static uint32_t cacheCalcIdx( uint32_t addr ) {
	return (addr >> (CACHE_LINE_WORD_SZ + 2)) & (CACHE_SETS - 1u);
}

static uint32_t cacheCalcTag( uint32_t addr ) {
	return addr >> (CACHE_SETS_SZ + CACHE_LINE_WORD_SZ + 2);
}

static uint32_t cacheCalcWordIdx( uint32_t addr ) {
	return (addr >> 2) & (CACHE_LINE_WORD - 1u);
}

static uint32_t cacheCalcByteIdx( uint32_t addr ) {
	return addr & 0x3u;
}

static uint32_t cacheReassembleAddr( uint32_t idx, uint32_t tag ) {
	return (idx << (CACHE_LINE_WORD_SZ + 2)) |
	       (tag << (CACHE_SETS_SZ + CACHE_LINE_WORD_SZ + 2));
}

static bool isFlagValid( uint8_t flags ) {
	return (flags & 1u) != 0;
}

static uint8_t setFlagValid( uint8_t flags ) {
	return flags | 1u;
}

static uint8_t setFlagInvalid( uint8_t flags ) {
	return flags & (uint8_t)~1u;
}


void cacheReset( void ) {
	memset(cacheData, 0, sizeof(cacheData));
	memset(cacheTags, 0, sizeof(cacheTags));
	memset(cacheFlags, 0, sizeof(cacheFlags));
}

int cachePeek( uint32_t addr, uint32_t bytes ) {
	if ( bytes != 1 && bytes != 2 && bytes != 4 ) {
		printf( "ERROR: invalid cache request size\n" );
		exit(1);
	}

	uint32_t lineBytes = CACHE_LINE_WORD * 4;
	uint32_t lineOffset = addr & (lineBytes - 1u);
	if ( bytes > lineBytes - lineOffset ) {
		printf( "ERROR: request spans line boundary\n" );
		exit(1);
	}

	uint32_t idx = cacheCalcIdx(addr);
	uint32_t tag = cacheCalcTag(addr);
	for ( uint32_t i = 0; i < CACHE_WAYS; i ++ ) {
		if ( cacheTags[idx][i] == tag && isFlagValid(cacheFlags[idx][i]) ) return (int)i;
	}
	return -1;
}

void cacheWrite( uint32_t addr, uint32_t data, uint32_t bytes ) {
	uint32_t idx = cacheCalcIdx(addr);
	uint32_t lineByteIdx = (cacheCalcWordIdx(addr) * 4) + cacheCalcByteIdx(addr);
	int way = cachePeek(addr, bytes);
	if ( way < 0 ) return;

	for ( uint32_t i = 0; i < bytes; i ++ ) {
		uint32_t byteIdx = lineByteIdx + i;
		uint32_t wordIdx = byteIdx / 4;
		uint32_t wordByteIdx = byteIdx % 4;
		uint32_t shift = wordByteIdx * 8;
		uint32_t mask = 0xffu << shift;
		uint32_t byteValue = ((data >> (i * 8)) & 0xffu) << shift;
		cacheData[idx][way][wordIdx] = (cacheData[idx][way][wordIdx] & ~mask) | byteValue;
	}
}

uint32_t cacheRead( uint32_t addr, uint32_t bytes ) {
	uint32_t idx = cacheCalcIdx(addr);
	uint32_t lineByteIdx = (cacheCalcWordIdx(addr) * 4) + cacheCalcByteIdx(addr);
	int way = cachePeek(addr, bytes);
	if ( way < 0 ) return 0xffffffffu;

	uint32_t result = 0;
	for ( uint32_t i = 0; i < bytes; i ++ ) {
		uint32_t byteIdx = lineByteIdx + i;
		uint32_t wordIdx = byteIdx / 4;
		uint32_t wordByteIdx = byteIdx % 4;
		uint32_t byteValue = (cacheData[idx][way][wordIdx] >> (wordByteIdx * 8)) & 0xffu;
		result |= byteValue << (i * 8);
	}
	return result;
}

void cacheUpdate( uint32_t addr, uint32_t data ) {
	uint32_t idx = cacheCalcIdx(addr);
	uint32_t tag = cacheCalcTag(addr);
	uint32_t wordIdx = cacheCalcWordIdx(addr);
	int way = cachePeek(addr, 4);
	if ( way < 0 ) {
		for ( uint32_t i = 0; i < CACHE_WAYS; i ++ ) {
			if ( !isFlagValid(cacheFlags[idx][i]) ) {
				way = (int)i;
				break;
			}
		}
	}
	if ( way < 0 ) {
		printf( "ERROR: no cache way available\n" );
		exit(1);
	}

	cacheData[idx][way][wordIdx] = data;
	cacheTags[idx][way] = tag;
	cacheFlags[idx][way] = setFlagValid(cacheFlags[idx][way]);
}

void cacheFlush( uint32_t addr, uint8_t *memory ) {
	uint32_t idx = cacheCalcIdx(addr);
	for ( uint32_t way = 0; way < CACHE_WAYS; way ++ ) {
		if ( !isFlagValid(cacheFlags[idx][way]) ) continue;

		uint32_t tag = cacheTags[idx][way];
		uint32_t lineAddr = cacheReassembleAddr(idx, tag);
		for ( uint32_t i = 0; i < CACHE_LINE_WORD; i ++ ) {
			uint32_t data = cacheData[idx][way][i];
			uint32_t wordAddr = lineAddr + (i * 4);
			for ( uint32_t byteIdx = 0; byteIdx < 4; byteIdx ++ ) {
				memory[wordAddr + byteIdx] = (uint8_t)((data >> (byteIdx * 8)) & 0xffu);
			}
		}
		cacheFlags[idx][way] = setFlagInvalid(cacheFlags[idx][way]);
	}
}

bool cacheReadPresent( uint32_t addr, uint32_t bytes, uint32_t *data ) {
	if ( data == NULL ) return false;
	if ( cachePeek(addr, bytes) < 0 ) return false;
	*data = cacheRead(addr, bytes);
	return true;
}
