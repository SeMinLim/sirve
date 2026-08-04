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
#include "loader.h"
#include "memory.h"
#include "rv32i.h"


#define MEM_BYTES 0x10000
#define TEXT_OFFSET 0x0000
#define DATA_OFFSET 0x8000
#define SOURCE_MAP_CNT (MEM_BYTES / 4)


typedef enum {
	INPUT_ASSEMBLY = 0,
	INPUT_RAW_BINARY,
	INPUT_ELF32
} InputMode;


typedef struct {
	InputMode inputMode;
	const char *filename;
	uint32_t loadAddr;
	uint32_t entryAddr;
	bool loadAddrSet;
	bool entryAddrSet;
	bool startImmediate;
	bool traceOn;
	bool maxInstructionsSet;
	uint64_t maxInstructions;
} CommandLineOptions;


static void printUsage( const char *program ) {
	printf( "usage:\n" );
	printf( "  %s --asm <assembly-file> [--trace] [--max-instructions <count>] [run]\n",
	        program
	);
	printf( "  %s --bin <binary-file> [--load <address>] [--entry <address>]\n", program );
	printf( "      [--trace] [--max-instructions <count>] [run]\n" );
	printf( "  %s --elf <elf32-file> [--trace] [--max-instructions <count>] [run]\n",
	        program
	);
	printf( "  %s <assembly-file> [--trace] [--max-instructions <count>] [run]\n", program );
}

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

static bool parseCount( const char *text, uint64_t *value ) {
	if ( text == NULL || value == NULL ) return false;
	while ( isspace((unsigned char)*text) ) text ++;
	if ( *text == '\0' ) return false;

	char *end = NULL;
	errno = 0;
	unsigned long long parsed = strtoull(text, &end, 0);
	while ( end != NULL && isspace((unsigned char)*end) ) end ++;
	if ( errno == ERANGE || end == text || (end != NULL && *end != '\0') || parsed == 0 ) {
		return false;
	}
	*value = (uint64_t)parsed;
	return true;
}

static bool parseCommandLine(
	int argc,
	char **argv,
	CommandLineOptions *options
) {
	if ( argc < 2 || options == NULL ) return false;
	memset(options, 0, sizeof(*options));
	options->inputMode = INPUT_ASSEMBLY;

	int argIdx = 1;
	if ( strcmp(argv[argIdx], "--asm") == 0 ) {
		if ( argIdx + 1 >= argc ) {
			printf( "Assembly filename is missing\n" );
			return false;
		}
		options->inputMode = INPUT_ASSEMBLY;
		options->filename = argv[argIdx + 1];
		argIdx += 2;
	} else if ( strcmp(argv[argIdx], "--bin") == 0 ) {
		if ( argIdx + 1 >= argc ) {
			printf( "Raw binary filename is missing\n" );
			return false;
		}
		options->inputMode = INPUT_RAW_BINARY;
		options->filename = argv[argIdx + 1];
		argIdx += 2;
	} else if ( strcmp(argv[argIdx], "--elf") == 0 ) {
		if ( argIdx + 1 >= argc ) {
			printf( "ELF32 filename is missing\n" );
			return false;
		}
		options->inputMode = INPUT_ELF32;
		options->filename = argv[argIdx + 1];
		argIdx += 2;
	} else {
		options->inputMode = INPUT_ASSEMBLY;
		options->filename = argv[argIdx];
		argIdx ++;
	}

	while ( argIdx < argc ) {
		if ( strcmp(argv[argIdx], "run") == 0 ) {
			if ( options->startImmediate ) {
				printf( "Duplicate run option\n" );
				return false;
			}
			options->startImmediate = true;
			argIdx ++;
			continue;
		}
		if ( strcmp(argv[argIdx], "--load") == 0 ) {
			if ( options->inputMode != INPUT_RAW_BINARY ) {
				printf( "--load is only valid with --bin\n" );
				return false;
			}
			if ( options->loadAddrSet || argIdx + 1 >= argc ||
			     !parseNumber(argv[argIdx + 1], &options->loadAddr) ) {
				printf( "Malformed raw binary load address\n" );
				return false;
			}
			options->loadAddrSet = true;
			argIdx += 2;
			continue;
		}
		if ( strcmp(argv[argIdx], "--entry") == 0 ) {
			if ( options->inputMode != INPUT_RAW_BINARY ) {
				printf( "--entry is only valid with --bin\n" );
				return false;
			}
			if ( options->entryAddrSet || argIdx + 1 >= argc ||
			     !parseNumber(argv[argIdx + 1], &options->entryAddr) ) {
				printf( "Malformed raw binary entry address\n" );
				return false;
			}
			options->entryAddrSet = true;
			argIdx += 2;
			continue;
		}
		if ( strcmp(argv[argIdx], "--trace") == 0 ) {
			if ( options->traceOn ) {
				printf( "Duplicate trace option\n" );
				return false;
			}
			options->traceOn = true;
			argIdx ++;
			continue;
		}
		if ( strcmp(argv[argIdx], "--max-instructions") == 0 ) {
			if ( options->maxInstructionsSet || argIdx + 1 >= argc ||
			     !parseCount(argv[argIdx + 1], &options->maxInstructions) ) {
				printf( "Malformed instruction limit\n" );
				return false;
			}
			options->maxInstructionsSet = true;
			argIdx += 2;
			continue;
		}

		printf( "Unknown command-line option: %s\n", argv[argIdx] );
		return false;
	}

	if ( options->inputMode == INPUT_RAW_BINARY && !options->entryAddrSet ) {
		options->entryAddr = options->loadAddr;
	}
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

static void initializeSourceMap(
	AssemblerSourceEntry *sourceMap,
	uint32_t sourceMapCnt
) {
	if ( sourceMap == NULL ) return;
	for ( uint32_t i = 0; i < sourceMapCnt; i ++ ) {
		sourceMap[i].line = -1;
		sourceMap[i].text[0] = '\0';
	}
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
	uint32_t textStart,
	uint32_t textEnd
) {
	printf( "Address    Raw        Source  Instruction\n" );
	printf( "---------------------------------------------------------------\n" );
	if ( textEnd <= textStart || textEnd - textStart < 4 ) return;

	for ( uint32_t addr = textStart; addr <= textEnd - 4; addr += 4 ) {
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
	uint32_t breakpointCnt
) {
	for ( uint32_t i = 0; i < breakpointCnt; i ++ ) {
		if ( !breakpoints[i] ) continue;
		if ( sourceMap != NULL && sourceMap[i].line >= 0 ) {
			printf( "Break at address 0x%08x, line %d: %s\n",
			        i * 4,
			        sourceMap[i].line,
			        sourceMap[i].text
			);
		} else {
			printf( "Break at address 0x%08x\n", i * 4 );
		}
	}
}

static bool addSourceBreakpoint(
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

static bool removeSourceBreakpoint(
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

static bool addressInExecutableRange(
	uint32_t addr,
	uint32_t textStart,
	uint32_t textEnd
) {
	if ( (addr & 0x3u) != 0 ) return false;
	if ( textEnd <= textStart || textEnd - textStart < 4 ) return false;
	return addr >= textStart && addr <= textEnd - 4;
}

static bool addAddressBreakpoint(
	bool *breakpoints,
	uint32_t breakpointCnt,
	uint32_t textStart,
	uint32_t textEnd,
	uint32_t addr
) {
	if ( !addressInExecutableRange(addr, textStart, textEnd) || addr / 4 >= breakpointCnt ) {
		printf( "Invalid breakpoint address: 0x%08x\n", addr );
		return false;
	}
	breakpoints[addr / 4] = true;
	printf( "Break point added to address 0x%08x\n", addr );
	return true;
}

static bool removeAddressBreakpoint(
	bool *breakpoints,
	uint32_t breakpointCnt,
	uint32_t addr
) {
	if ( (addr & 0x3u) != 0 || addr / 4 >= breakpointCnt || !breakpoints[addr / 4] ) {
		printf( "No breakpoint found at address 0x%08x\n", addr );
		return false;
	}
	breakpoints[addr / 4] = false;
	printf( "Break point removed from address 0x%08x\n", addr );
	return true;
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
	uint32_t textStart,
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
			uint32_t value = 0;
			if ( command[1] == '\0' ) {
				listBreakpoints(breakpoints, sourceMap, sourceMapCnt);
			} else if ( command[1] == 'a' ) {
				if ( parseNumber(command + 2, &value) ) {
					addAddressBreakpoint(breakpoints, sourceMapCnt, textStart, textEnd, value);
				} else {
					printf( "Malformed breakpoint address\n" );
				}
			} else if ( parseNumber(command + 1, &value) ) {
				addSourceBreakpoint(breakpoints, sourceMap, sourceMapCnt, value);
			} else {
				printf( "Malformed breakpoint line\n" );
			}
			continue;
		}
		if ( command[0] == 'B' ) {
			uint32_t value = 0;
			if ( command[1] == 'a' ) {
				if ( parseNumber(command + 2, &value) ) {
					removeAddressBreakpoint(breakpoints, sourceMapCnt, value);
				} else {
					printf( "Malformed breakpoint address\n" );
				}
			} else if ( parseNumber(command + 1, &value) ) {
				removeSourceBreakpoint(breakpoints, sourceMap, sourceMapCnt, value);
			} else {
				printf( "Malformed breakpoint line\n" );
			}
			continue;
		}
		if ( command[0] == 'l' ) {
			listInstructions(memory, sourceMap, sourceMapCnt, textStart, textEnd);
			continue;
		}

		printf( "Unknown debugger command\n" );
	}
}

static const char *stepStatusName( RV32IStepStatus status ) {
	switch ( status ) {
		case RV32I_STEP_OK: return "ok";
		case RV32I_STEP_EBREAK: return "ebreak";
		case RV32I_STEP_ECALL: return "ecall";
		case RV32I_STEP_ILLEGAL_INSTRUCTION: return "illegal-instruction";
		case RV32I_STEP_INSTRUCTION_MISALIGNED: return "instruction-misaligned";
		case RV32I_STEP_INSTRUCTION_ACCESS_FAULT: return "instruction-access-fault";
		case RV32I_STEP_LOAD_MISALIGNED: return "load-misaligned";
		case RV32I_STEP_LOAD_ACCESS_FAULT: return "load-access-fault";
		case RV32I_STEP_STORE_MISALIGNED: return "store-misaligned";
		case RV32I_STEP_STORE_ACCESS_FAULT: return "store-access-fault";
		default: return "unknown";
	}
}

static void printTrace( const RV32IStepResult *result ) {
	if ( result == NULL ) return;

	printf( "TRACE pc=0x%08x raw=0x%08x", result->pc, result->raw );
	if ( result->regWrite ) {
		printf( " rd=x%02u value=0x%08x", result->regIdx, result->regValue );
	}
	if ( result->memRead ) {
		printf( " load=0x%08x", result->memReadAddr );
	}
	if ( result->memWrite ) {
		printf( " store=0x%08x size=%u value=0x%08x",
		        result->memWriteAddr,
		        result->memWriteSize,
		        result->memWriteValue
		);
	}
	printf( " next=0x%08x status=%s\n",
	        result->nextPc,
	        stepStatusName(result->status)
	);
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
	uint32_t textStart,
	uint32_t textEnd,
	bool startImmediate,
	bool traceOn,
	uint64_t maxInstructions
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
			if ( !handleDebugger(state, memory, sourceMap, sourceMapCnt, textStart, textEnd,
			                    breakpoints, &continuous, &stepCnt) ) {
				delete[] breakpoints;
				return true;
			}
			promptOnNext = false;
		}

		RV32IStepResult stepResult;
		RV32IStepStatus status = stepRV32I(state, memory, &stepResult);
		if ( traceOn ) printTrace(&stepResult);

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

		if ( running && maxInstructions != 0 && state->instCnt >= maxInstructions ) {
			printf( "Stopped after %llu instructions.\n",
			        (unsigned long long)state->instCnt
			);
			running = false;
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
	CommandLineOptions options;
	if ( !parseCommandLine(argc, argv, &options) ) {
		printUsage(argv[0]);
		return 1;
	}

	uint8_t *data = (uint8_t*)calloc(MEM_BYTES, sizeof(uint8_t));
	AssemblerSourceEntry *sourceMap = new (std::nothrow) AssemblerSourceEntry[SOURCE_MAP_CNT];
	if ( data == NULL || sourceMap == NULL ) {
		printf( "Memory allocation failed\n" );
		free(data);
		delete[] sourceMap;
		return 2;
	}
	initializeSourceMap(sourceMap, SOURCE_MAP_CNT);

	uint32_t entryAddr = 0;
	uint32_t textStart = 0;
	uint32_t textEnd = 0;
	uint32_t executableStart = 0;
	uint32_t executableLimit = DATA_OFFSET;

	if ( options.inputMode == INPUT_ASSEMBLY ) {
		printf( "Assembling input file\n" );
		AssemblerResult assemblerResult;
		AssemblerError assemblerError;
		if ( !assembleRV32I(options.filename, data, MEM_BYTES, TEXT_OFFSET, DATA_OFFSET,
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
		entryAddr = assemblerResult.entryAddr;
		textStart = TEXT_OFFSET;
		textEnd = assemblerResult.textEnd;
	} else if ( options.inputMode == INPUT_RAW_BINARY ) {
		printf( "Loading raw binary\n" );
		RawBinaryResult loaderResult;
		LoaderError loaderError;
		if ( !loadRawBinary(options.filename, data, MEM_BYTES, options.loadAddr,
		                    options.entryAddr, &loaderResult, &loaderError) ) {
			printf( "%s\n", loaderError.message );
			free(data);
			delete[] sourceMap;
			return 1;
		}
		entryAddr = loaderResult.entryAddr;
		textStart = loaderResult.loadAddr;
		textEnd = loaderResult.loadedEnd;
		executableStart = loaderResult.loadAddr;
		executableLimit = loaderResult.loadedEnd;
	} else {
		printf( "Loading ELF32 executable\n" );
		Elf32Result loaderResult;
		LoaderError loaderError;
		if ( !loadElf32(options.filename, data, MEM_BYTES, &loaderResult, &loaderError) ) {
			printf( "%s\n", loaderError.message );
			free(data);
			delete[] sourceMap;
			return 1;
		}
		entryAddr = loaderResult.entryAddr;
		textStart = loaderResult.executableStart;
		textEnd = loaderResult.executableLimit;
		executableStart = loaderResult.executableStart;
		executableLimit = loaderResult.executableLimit;
	}

	Memory memory;
	initializeMemory(&memory, data, MEM_BYTES, executableLimit);
	if ( !setMemoryExecutableRange(&memory, executableStart, executableLimit) ) {
		printf( "Invalid executable memory range\n" );
		free(data);
		delete[] sourceMap;
		return 1;
	}

	ProcessorState state;
	initializeProcessorState(&state, entryAddr);
	if ( options.inputMode == INPUT_ELF32 ) state.reg[2] = MEM_BYTES;
	bool success = execute(&memory, &state, sourceMap, SOURCE_MAP_CNT,
	                       textStart, textEnd, options.startImmediate, options.traceOn,
	                       options.maxInstructionsSet ? options.maxInstructions : 0);

	free(data);
	delete[] sourceMap;
	if ( !success ) return 1;

	printf( "Execution done!\n" );
	return 0;
}
