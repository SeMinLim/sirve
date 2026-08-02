typedef unsigned int uint32_t;


volatile uint32_t initializedValue = 7;
volatile uint32_t zeroValue;
volatile uint32_t resultValue;


// Use a real stack frame and data-memory accesses.
__attribute__((noinline))
static uint32_t calculateResult( uint32_t input ) {
	volatile uint32_t values[4];
	values[0] = input;
	values[1] = values[0] + 3;
	values[2] = values[1] << 1;
	values[3] = values[2] - 3;

	uint32_t result = 0;
	for ( uint32_t i = 0; i < 4; i ++ ) {
		result += values[i];
	}
	return result;
}


int main( void ) {
	uint32_t result = calculateResult(initializedValue);
	if ( zeroValue != 0 ) result = 0xbad;

	zeroValue = result;
	resultValue = result;
	*(volatile uint32_t*)0x10000 = result;
	return (int)result;
}
