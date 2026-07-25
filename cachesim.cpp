#include "cachesim.h"
#include <stdlib.h>

uint32_t g_cache[CACHE_SETS][CACHE_WAYS][CACHE_LINE_WORD] = {0};
uint32_t g_tags[CACHE_SETS][CACHE_WAYS] = {0};
uint8_t g_flags[CACHE_SETS][CACHE_WAYS] = {0};

uint32_t cache_calc_idx(uint32_t addr) {
	return (addr >> (CACHE_LINE_WORD_SZ + 2)) & ((1u << CACHE_SETS_SZ) - 1u);
}

uint32_t cache_calc_tag(uint32_t addr) {
	return addr >> (CACHE_SETS_SZ + CACHE_LINE_WORD_SZ + 2);
}

uint32_t cache_calc_word_idx(uint32_t addr) {
	return (addr >> 2) & ((1u << CACHE_LINE_WORD_SZ) - 1u);
}

uint32_t cache_calc_byte_idx(uint32_t addr) {
	return addr & 0x3u;
}

uint32_t cache_reassemble_addr(uint32_t idx, uint32_t tag) {
	return (idx << (CACHE_LINE_WORD_SZ + 2)) |
	       (tag << (CACHE_SETS_SZ + CACHE_LINE_WORD_SZ + 2));
}

bool is_flag_valid(uint8_t flags) {
	return (flags & 1u) != 0;
}

uint8_t set_flag_valid(uint8_t flags) {
	return flags | 1u;
}

uint8_t set_flag_invalid(uint8_t flags) {
	return flags & (uint8_t)~1u;
}

int cache_peek(uint32_t addr, int bytes) {
	if ( bytes != 1 && bytes != 2 && bytes != 4 ) {
		printf( "ERROR: invalid cache request size\n" );
		exit(1);
	}

	uint32_t lineBytes = CACHE_LINE_WORD * 4;
	uint32_t lineOffset = addr & (lineBytes - 1u);
	if ( (uint32_t)bytes > lineBytes - lineOffset ) {
		printf( "ERROR: request spans line boundary\n" );
		exit(1);
	}

	uint32_t idx = cache_calc_idx(addr);
	uint32_t tag = cache_calc_tag(addr);
	for ( int i = 0; i < CACHE_WAYS; i ++ ) {
		if ( g_tags[idx][i] == tag && is_flag_valid(g_flags[idx][i]) ) return i;
	}

	return -1;
}

void cache_write(uint32_t addr, uint32_t data, int bytes) {
	uint32_t idx = cache_calc_idx(addr);
	uint32_t lineByteIdx = (cache_calc_word_idx(addr) * 4) + cache_calc_byte_idx(addr);
	int way = cache_peek(addr, bytes);
	if ( way < 0 ) return;

	for ( int i = 0; i < bytes; i ++ ) {
		uint32_t byteIdx = lineByteIdx + i;
		uint32_t wordIdx = byteIdx / 4;
		uint32_t wordByteIdx = byteIdx % 4;
		uint32_t shift = wordByteIdx * 8;
		uint32_t mask = 0xffu << shift;
		uint32_t byteValue = ((data >> (i * 8)) & 0xffu) << shift;
		g_cache[idx][way][wordIdx] = (g_cache[idx][way][wordIdx] & ~mask) | byteValue;
	}
}

uint32_t cache_read(uint32_t addr, int bytes) {
	uint32_t idx = cache_calc_idx(addr);
	uint32_t lineByteIdx = (cache_calc_word_idx(addr) * 4) + cache_calc_byte_idx(addr);
	int way = cache_peek(addr, bytes);
	if ( way < 0 ) return 0xffffffff;

	uint32_t ret = 0;
	for ( int i = 0; i < bytes; i ++ ) {
		uint32_t byteIdx = lineByteIdx + i;
		uint32_t wordIdx = byteIdx / 4;
		uint32_t wordByteIdx = byteIdx % 4;
		uint32_t byteValue = (g_cache[idx][way][wordIdx] >> (wordByteIdx * 8)) & 0xffu;
		ret |= byteValue << (i * 8);
	}

	return ret;
}

void cache_update(uint32_t addr, uint32_t data) {
	uint32_t idx = cache_calc_idx(addr);
	uint32_t tag = cache_calc_tag(addr);
	uint32_t wid = cache_calc_word_idx(addr);
	int way = cache_peek(addr, 4);
	if ( way < 0 ) {
		for ( int i = 0; i < CACHE_WAYS; i ++ ) {
			if ( !is_flag_valid(g_flags[idx][i]) ) {
				way = i;
				break;
			}
		}
	}
	if ( way < 0 ) {
		printf( "ERROR: no cache way available\n" );
		exit(1);
	}

	g_cache[idx][way][wid] = data;
	g_tags[idx][way] = tag;
	g_flags[idx][way] = set_flag_valid(g_flags[idx][way]);
}

void cache_flush(uint32_t addr, uint8_t* mem) {
	uint32_t idx = cache_calc_idx(addr);
	int way = 0;
	if ( !is_flag_valid(g_flags[idx][way]) ) return;

	uint32_t tag = g_tags[idx][way];
	uint32_t lineAddr = cache_reassemble_addr(idx, tag);
	for ( int i = 0; i < CACHE_LINE_WORD; i ++ ) {
		uint32_t data = g_cache[idx][way][i];
		uint32_t wordAddr = lineAddr + (i * 4);
		for ( int byteIdx = 0; byteIdx < 4; byteIdx ++ ) {
			mem[wordAddr + byteIdx] = (uint8_t)((data >> (byteIdx * 8)) & 0xffu);
		}
	}
	g_flags[idx][way] = set_flag_invalid(g_flags[idx][way]);
}
