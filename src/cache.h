#ifndef SIRVE_CACHE_H
#define SIRVE_CACHE_H

#include <stdint.h>


// Change these values to configure the cache.
// The parameters are logarithmic, so CACHE_WAYS_SZ=0 creates one way.
#define CACHE_SETS_SZ 8
#define CACHE_WAYS_SZ 0
#define CACHE_LINE_WORD_SZ 0

#define CACHE_SETS (1u << CACHE_SETS_SZ)
#define CACHE_WAYS (1u << CACHE_WAYS_SZ)
#define CACHE_LINE_WORD (1u << CACHE_LINE_WORD_SZ)


void cacheReset( void );
int cachePeek( uint32_t addr, uint32_t bytes );
void cacheWrite( uint32_t addr, uint32_t data, uint32_t bytes );
uint32_t cacheRead( uint32_t addr, uint32_t bytes );
void cacheUpdate( uint32_t addr, uint32_t data );
void cacheFlush( uint32_t addr, uint8_t *memory );
bool cacheReadPresent( uint32_t addr, uint32_t bytes, uint32_t *data );

#endif
