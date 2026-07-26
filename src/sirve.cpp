#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <new>
#include <string>

#include "assembler.h"
#include "linenoise.hpp"
#include "memory.h"
#include "rv32i.h"


#define MEM_BYTES 0x10000
#define TEXT_OFFSET 0x0000
#define DATA_OFFSET 0x8000
#define SOURCE_MAP_CNT (DATA_OFFSET / 4)


static void printRegFile( const uint32_t reg[32] ) {
	for ( int i = 0; i < 32; i ++ ) {
		printf( "x%02d:0x%08x ", i, reg[i] );
		if ( (i + 1) % 8 == 0 ) printf( "\n" );
	}
}

static bool parseNumber( const char *text, uint32_t *value ) {
	if ( text == NULL || value == NULL ) return false;
	while ( isspace((unsigned char)*text) ) text ++;
	if ( *text == '\0' ) return false;

	char *end = NULL;
	errno = 0;
	unsigned long long parsed = strtoull(text, &end, 0);
	while ( end != NULL && isspace((unsigned char)*end) ) end ++;
	if ( errno == ERANGE || end == text || (end != NULL && *end != '\0') || parsed > UINT32_MAX ) {
		return false;
	}
	*value = (uint32_t)parsed;
	return true;
}

static int parseRegisterName( const char *text ) {
	if ( text == NULL ) return -1;
	while ( isspace((unsigned char)*text) ) text ++;
	if ( *text == '\0' ) return -1;

	if ( text[0] == 'x' ) {
		uint32_t reg = 0;
		if ( !parseNumber(text + 1, &reg) || reg >= 32 ) return -1;
		return (int)reg;
	}

	static const char *registerNames[32] = {
		"zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
		"s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
		"a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
		"s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
	};
	for ( int i = 0; i < 32; i ++ ) {
		if ( strcmp(text, registerNames[i]) == 0 ) return i;
	}
	if ( strcmp(text, "fp") == 0 ) return 8;
	return -1;
}

static int getSourceLine(
	const AssemblerSourceEntry *sourceMap,
	uint32_t sourceMapCnt,
	uint32_t pc
) {
	uint32_t index = pc / 4;
	if ( sourceMap == NULL || index >= sourceMapCnt ) return -1;
	return sourceMap[index].line;
}

static void getInstructionText(
	const Memory *memory,
	const AssemblerSourceEntry *sourceMap,
	uint32_t sourceMapCnt,
	uint32_t pc,
	char *text,
	size_t textSize
) {
	uint32_t index = pc / 4;
	if ( sourceMap != NULL && index < sourceMapCnt && sourceMap[index].line >= 0 &&
	     sourceMap[index].text[0] != '\0' ) {
		snprintf(text, textSize, "%s", sourceMap[index].text);
		return;
	}

	uint32_t raw = 0;
	if ( memoryFetch32(memory, pc, &raw) != MEMORY_STATUS_OK ) {
		snprintf(text, textSize, "<invalid instruction address>");
		return;
	}
	disassembleRV32I(raw, text, textSize);
}

static void listInstructions(
	const Memory *memory,
	const AssemblerSourceEntry *sourceMap,
	uint32_t sourceMapCnt,
	uint32_t textEnd
) {
	printf( "Address    Raw        Source  Instruction\n" );
	printf( "---------------------------------------------------------------\n" );
	for ( uint32_t addr = 0; addr < textEnd; addr += 4 ) {
		uint32_t raw = 0;
		if ( memoryFetch32(memory, addr, &raw) != MEMORY_STATUS_OK ) break;

		char disassembly[128];
		disassembleRV32I(raw, disassembly, sizeof(disassembly));
		int line = getSourceLine(sourceMap, sourceMapCnt, addr);
		const char *sourceText = "";
		uint32_t index = addr / 4;
		if ( sourceMap != NULL && index < sourceMapCnt && sourceMap[index].text[0] != '\0' ) {
			sourceText = sourceMap[index].text;
		}

		printf( "0x%08x  0x%08x  %6d  %-28s %s\n",
		        addr,
		        raw,
		        line,
		        disassembly,
		        sourceText
		);
	}
}

static void listBreakpoints(
	const bool *breakpoints,
	const AssemblerSourceEntry *sourceMap,
	uint32_t sourceMapCnt
) {
	for ( uint32_t i = 0; i < sourceMapCnt; i ++ ) {
		if ( !breakpoints[i] ) continue;
		printf( "Break at address 0x%08x, line %d: %s\n",
		        i * 4,
		        sourceMap[i].line,
		        sourceMap[i].text
		);
	}
}

static bool addBreakpoint(
	bool *breakpoints,
	const AssemblerSourceEntry *sourceMap,
	uint32_t sourceMapCnt,
	uint32_t sourceLine
) {
	for ( uint32_t i = 0; i < sourceMapCnt; i ++ ) {
		if ( sourceMap[i].line >= (int)sourceLine ) {
			breakpoints[i] = true;
			printf( "Break point added to line %u\n", sourceLine );
			return true;
		}
	}
	printf( "No instruction found at or after line %u\n", sourceLine );
	return false;
}

static bool removeBreakpoint(
	bool *breakpoints,
	const AssemblerSourceEntry *sourceMap,
	uint32_t sourceMapCnt,
	uint32_t sourceLine
) {
	bool removed = false;
	for ( uint32_t i = 0; i < sourceMapCnt; i ++ ) {
		if ( sourceMap[i].line == (int)sourceLine && breakpoints[i] ) {
			breakpoints[i] = false;
			removed = true;
		}
	}
	if ( removed ) printf( "Break point removed from line %u\n", sourceLine );
	else printf( "No breakpoint found at line %u\n", sourceLine );
	return removed;
}

static void inspectMemory( const Memory *memory, char *command ) {
	char *addressText = command + 1;
	while ( isspace((unsigned char)*addressText) ) addressText ++;

	char *countText = addressText;
	while ( *countText != '\0' && !isspace((unsigned char)*countText) ) countText ++;
	if ( *countText != '\0' ) {
		*countText = '\0';
		countText ++;
	}

	uint32_t addr = 0;
	uint32_t count = 1;
	if ( !parseNumber(addressText, &addr) ) {
		printf( "Malformed memory address\n" );
		return;
	}
	while ( isspace((unsigned char)*countText) ) countText ++;
	if ( *countText != '\0' && !parseNumber(countText, &count) ) {
		printf( "Malformed memory word count\n" );
		return;
	}

	uint64_t byteCnt = (uint64_t)count * 4;
	if ( addr >= memory->size || byteCnt > memory->size - addr ) {
		printf( "Memory inspection out of bounds at address 0x%08x\n", addr );
		return;
	}

	for ( uint32_t wordIdx = 0; wordIdx < count; wordIdx ++ ) {
		uint32_t wordAddr = addr + (wordIdx * 4);
		printf( "0x%04x: ", wordAddr );
		for ( uint32_t byteIdx = 0; byteIdx < 4; byteIdx ++ ) {
			uint32_t value = 0;
			memoryInspect8(memory, wordAddr + byteIdx, &value);
			printf( "%02x ", value );
		}
		printf( "\n" );
	}
}

static bool handleDebugger(
	ProcessorState *state,
	const Memory *memory,
	const AssemblerSourceEntry *sourceMap,
	uint32_t sourceMapCnt,
	uint32_t textEnd,
	bool *breakpoints,
	bool *continuous,
	uint32_t *stepCnt
) {
	char nextText[128];
	getInstructionText(memory, sourceMap, sourceMapCnt, state->pc, nextText, sizeof(nextText));
	printf( "\nNext: %s\n", nextText );

	while ( true ) {
		int sourceLine = getSourceLine(sourceMap, sourceMapCnt, state->pc);
		printf( "[inst: %6llu pc: %6u, src line %4d]\n",
		        (unsigned long long)(state->instCnt + 1),
		        state->pc,
		        sourceLine
		);
		fflush( stdout );

		std::string lineBuf;
		if ( !linenoise::Readline(">>", lineBuf) ) return false;
		linenoise::AddHistory(lineBuf.c_str());

		char command[128];
		snprintf(command, sizeof(command), "%s", lineBuf.c_str());
		if ( command[0] == '\0' ) {
			*stepCnt = 1;
			*continuous = false;
			return true;
		}

		if ( command[0] == 'q' ) {
			printf( "Quit command input! Exiting...\n" );
			return false;
		}
		if ( command[0] == 'c' ) {
			*continuous = true;
			*stepCnt = 0;
			return true;
		}
		if ( command[0] == 's' ) {
			uint32_t requested = 1;
			if ( command[1] != '\0' && !parseNumber(command + 1, &requested) ) {
				printf( "Malformed step count\n" );
				continue;
			}
			if ( requested == 0 ) requested = 1;
			*stepCnt = requested;
			*continuous = false;
			return true;
		}
		if ( command[0] == 'r' ) {
			int reg = parseRegisterName(command + 1);
			if ( reg >= 0 ) printf( "rf[%2d] = 0x%x\n", reg, state->reg[reg] );
			else printRegFile(state->reg);
			continue;
		}
		if ( command[0] == 'm' ) {
			inspectMemory(memory, command);
			continue;
		}
		if ( command[0] == 'b' ) {
			uint32_t sourceLine = 0;
			if ( command[1] == '\0' ) listBreakpoints(breakpoints, sourceMap, sourceMapCnt);
			else if ( parseNumber(command + 1, &sourceLine) ) {
				addBreakpoint(breakpoints, sourceMap, sourceMapCnt, sourceLine);
			} else {
				printf( "Malformed breakpoint line\n" );
			}
			continue;
		}
		if ( command[0] == 'B' ) {
			uint32_t sourceLine = 0;
			if ( parseNumber(command + 1, &sourceLine) ) {
				removeBreakpoint(breakpoints, sourceMap, sourceMapCnt, sourceLine);
			} else {
				printf( "Malformed breakpoint line\n" );
			}
			continue;
		}
		if ( command[0] == 'l' ) {
			listInstructions(memory, sourceMap, sourceMapCnt, textEnd);
			continue;
		}

		printf( "Unknown debugger command\n" );
	}
}

static bool printExecutionError( const RV32IStepResult *result ) {
	switch ( result->status ) {
		case RV32I_STEP_INSTRUCTION_MISALIGNED:
			printf( "Instruction address misaligned: 0x%08x\n", result->faultAddr );
			break;
		case RV32I_STEP_INSTRUCTION_ACCESS_FAULT:
			printf( "Instruction address out of bounds: 0x%08x\n", result->faultAddr );
			break;
		case RV32I_STEP_LOAD_MISALIGNED:
			printf( "Misaligned memory read at address 0x%08x\n", result->faultAddr );
			break;
		case RV32I_STEP_LOAD_ACCESS_FAULT:
			printf( "Memory read out of bounds at address 0x%08x\n", result->faultAddr );
			break;
		case RV32I_STEP_STORE_MISALIGNED:
			printf( "Misaligned memory write at address 0x%08x\n", result->faultAddr );
			break;
		case RV32I_STEP_STORE_ACCESS_FAULT:
			printf( "Memory write out of bounds at address 0x%08x\n", result->faultAddr );
			break;
		case RV32I_STEP_ECALL:
			printf( "Environment call at address 0x%08x\n", result->pc );
			break;
		case RV32I_STEP_ILLEGAL_INSTRUCTION:
			printf( "Illegal instruction 0x%08x at address 0x%08x\n",
			        result->raw,
			        result->pc
			);
			break;
		case RV32I_STEP_OK:
		case RV32I_STEP_EBREAK:
		default:
			return false;
	}
	return true;
}

static bool execute(
	Memory *memory,
	ProcessorState *state,
	const AssemblerSourceEntry *sourceMap,
	uint32_t sourceMapCnt,
	uint32_t textEnd,
	bool startImmediate
) {
	uint32_t regMirror[32] = {0};
	bool *breakpoints = new (std::nothrow) bool[sourceMapCnt];
	if ( breakpoints == NULL ) {
		printf( "Memory allocation failed\n" );
		return false;
	}
	memset(breakpoints, 0, sizeof(bool) * sourceMapCnt);

	bool continuous = startImmediate;
	uint32_t stepCnt = 0;
	bool promptOnNext = !startImmediate;
	bool running = true;

	while ( running ) {
		uint32_t index = state->pc / 4;
		bool atBreakpoint = index < sourceMapCnt && breakpoints[index];
		if ( atBreakpoint || promptOnNext ) {
			if ( !handleDebugger(state, memory, sourceMap, sourceMapCnt, textEnd,
			                    breakpoints, &continuous, &stepCnt) ) {
				delete[] breakpoints;
				return true;
			}
			promptOnNext = false;
		}

		RV32IStepResult stepResult;
		RV32IStepStatus status = stepRV32I(state, memory, &stepResult);
		if ( status == RV32I_STEP_EBREAK ) {
			int sourceLine = getSourceLine(sourceMap, sourceMapCnt, stepResult.pc);
			printf( "\n\n----------\n\n" );
			printf( "Reached Halt and Catch Fire instruction!\n" );
			printf( "inst: %6llu pc: %6u src line: %d\n",
			        (unsigned long long)state->instCnt,
			        stepResult.pc,
			        sourceLine
			);
			printRegFile(state->reg);
			printf( "Cache read %llu/%llu Cache write %llu/%llu\n",
			        (unsigned long long)memory->readHitCnt,
			        (unsigned long long)memory->readReqCnt,
			        (unsigned long long)memory->writeHitCnt,
			        (unsigned long long)memory->writeReqCnt
			);
			printf( "Cache flush words: %llu\n",
			        (unsigned long long)memory->flushWordCnt
			);
			running = false;
		} else if ( status != RV32I_STEP_OK ) {
			printExecutionError(&stepResult);
			delete[] breakpoints;
			return false;
		}

		if ( !continuous && stepCnt > 0 ) {
			stepCnt --;
			if ( stepCnt == 0 && running ) {
				promptOnNext = true;
				for ( int i = 0; i < 32; i ++ ) {
					if ( state->reg[i] != regMirror[i] ) {
						printf( ">> rf[x%02d] %x -> %x\n", i, regMirror[i], state->reg[i] );
					}
					regMirror[i] = state->reg[i];
				}
			}
		}
		fflush( stdout );
	}

	delete[] breakpoints;
	return true;
}


int main( int argc, char **argv ) {
	if ( argc < 2 ) {
		printf( "usage: %s asmfile [run]\n", argv[0] );
		return 1;
	}

	bool startImmediate = argc >= 3;
	uint8_t *data = (uint8_t*)calloc(MEM_BYTES, sizeof(uint8_t));
	AssemblerSourceEntry *sourceMap = new (std::nothrow) AssemblerSourceEntry[SOURCE_MAP_CNT];
	if ( data == NULL || sourceMap == NULL ) {
		printf( "Memory allocation failed\n" );
		free(data);
		delete[] sourceMap;
		return 2;
	}

	printf( "Assembling input file\n" );
	AssemblerResult assemblerResult;
	AssemblerError assemblerError;
	if ( !assembleRV32I(argv[1], data, MEM_BYTES, TEXT_OFFSET, DATA_OFFSET,
	                    &assemblerResult, &assemblerError, sourceMap, SOURCE_MAP_CNT) ) {
		if ( assemblerError.line > 0 ) {
			printf( "Line %4d: Syntax error! %s\n",
			        assemblerError.line,
			        assemblerError.message
			);
		} else {
			printf( "%s\n", assemblerError.message );
		}
		free(data);
		delete[] sourceMap;
		return 1;
	}

	Memory memory;
	initializeMemory(&memory, data, MEM_BYTES, DATA_OFFSET);

	ProcessorState state;
	initializeProcessorState(&state, assemblerResult.entryAddr);
	bool success = execute(&memory, &state, sourceMap, SOURCE_MAP_CNT,
	                       assemblerResult.textEnd, startImmediate);

	free(data);
	delete[] sourceMap;
	if ( !success ) return 1;

	printf( "Execution done!\n" );
	return 0;
}
