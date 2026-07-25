#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <new>

#include "cachesim.h"

#include "linenoise.hpp"

// 64 KB
#define MEM_BYTES 0x10000
#define TEXT_OFFSET 0
#define DATA_OFFSET 32768

#define MAX_LABEL_COUNT 128
#define MAX_LABEL_LEN 32
#define MAX_SRC_LEN (1024*1024)

typedef struct {
	char* src;
	int offset;
} source;

bool streq(char* s, const char* q) {
	if ( strcmp(s,q) == 0 ) return true;

	return false;
}

uint32_t signextend(uint32_t in, int bits) {
	if ( in & (1u << (bits - 1)) ) return (~0u << bits) | in;
	return in;
}

uint32_t readWordLE(const uint8_t* mem, uint32_t addr) {
	uint32_t value = 0;
	for ( int i = 0; i < 4; i ++ ) {
		value |= ((uint32_t)mem[addr + i]) << (i * 8);
	}
	return value;
}

void writeWordLE(uint8_t* mem, uint32_t addr, uint32_t value) {
	for ( int i = 0; i < 4; i ++ ) {
		mem[addr + i] = (uint8_t)((value >> (i * 8)) & 0xffu);
	}
}

int32_t asSigned32(uint32_t value) {
	int32_t signedValue = 0;
	memcpy(&signedValue, &value, sizeof(signedValue));
	return signedValue;
}

uint32_t arithmeticShiftRight(uint32_t value, uint32_t shiftAmount) {
	shiftAmount &= 0x1f;
	if ( shiftAmount == 0 ) return value;

	uint32_t shifted = value >> shiftAmount;
	if ( value & 0x80000000u ) shifted |= ~0u << (32 - shiftAmount);
	return shifted;
}

void print_syntax_error(int line, const char* msg) {
	printf( "Line %4d: Syntax error! %s\n", line, msg );
	exit(1);
}

void copyLabel(char* destination, const char* label, int line) {
	if ( !label || strlen(label) >= MAX_LABEL_LEN ) {
		print_syntax_error(line, "Malformed label");
	}
	memcpy(destination, label, strlen(label) + 1);
}

void print_regfile(uint32_t rf[32]) {
	for ( int i = 0; i < 32; i++ ) {
		printf( "x%02d:0x%08x ", i, rf[i] );
		if ( (i+1) % 8 == 0 ) printf( "\n" );
	}
}


typedef enum {
	UNIMPL = 0,
	ADD,
	ADDI,
	AND,
	ANDI,
	AUIPC,
	BEQ,
	BGE,
	BGEU,
	BLT,
	BLTU,
	BNE,
	JAL,
	JALR,
	LB,
	LBU,
	LH,
	LHU,
	LUI,
	LW,
	OR,
	ORI,
	SB,
	SH,
	SLL,
	SLLI,
	SLT,
	SLTI,
	SLTIU,
	SLTU,
	SRA,
	SRAI,
	SRL,
	SRLI,
	SUB,
	SW,
	XOR,
	XORI,
	HCF
} instr_type;

instr_type parse_instr(char* tok) {
	// 2r->1r
	if ( streq(tok, "add") ) return ADD;
	if ( streq(tok, "sub") ) return SUB;
	if ( streq(tok, "slt") ) return SLT;
	if ( streq(tok, "sltu") ) return SLTU;
	if ( streq(tok, "and") ) return AND;
	if ( streq(tok, "or") ) return OR;
	if ( streq(tok, "xor") ) return XOR;
	if ( streq(tok, "sll") ) return SLL;
	if ( streq(tok, "srl") ) return SRL;
	if ( streq(tok, "sra") ) return SRA;

	// 1r, imm -> 1r
	if ( streq(tok, "addi") ) return ADDI;
	if ( streq(tok, "slti") ) return SLTI;
	if ( streq(tok, "sltiu") ) return SLTIU;
	if ( streq(tok, "andi") ) return ANDI;
	if ( streq(tok, "ori") ) return ORI;
	if ( streq(tok, "xori") ) return XORI;
	if ( streq(tok, "slli") ) return SLLI;
	if ( streq(tok, "srli") ) return SRLI;
	if ( streq(tok, "srai") ) return SRAI;

	//load/store
	if ( streq(tok, "lb") ) return LB;
	if ( streq(tok, "lbu") ) return LBU;
	if ( streq(tok, "lh") ) return LH;
	if ( streq(tok, "lhu") ) return LHU;
	if ( streq(tok, "lw") ) return LW;
	if ( streq(tok, "sb") ) return SB;
	if ( streq(tok, "sh") ) return SH;
	if ( streq(tok, "sw") ) return SW;

	//branch
	if ( streq(tok, "beq") ) return BEQ;
	if ( streq(tok, "bge") ) return BGE;
	if ( streq(tok, "bgeu") ) return BGEU;
	if ( streq(tok, "blt") ) return BLT;
	if ( streq(tok, "bltu") ) return BLTU;
	if ( streq(tok, "bne") ) return BNE;

	// jal
	if ( streq(tok, "jal") ) return JAL;
	if ( streq(tok, "jalr") ) return JALR;

	// lui
	if ( streq(tok, "auipc") ) return AUIPC;
	if ( streq(tok, "lui") ) return LUI;

	// unimpl
	//if ( streq(tok, "unimpl") ) return UNIMPL;
	if ( streq(tok, "hcf") ) return HCF;
	return UNIMPL;
}

int parse_reg(char* tok, int line, bool strict = true) {
	if ( !tok ) {
		if ( strict ) print_syntax_error(line, "Malformed register name");
		return -1;
	}

	if ( tok[0] == 'x' ) {
		if ( tok[1] == '\0' ) {
			if ( strict ) print_syntax_error(line, "Malformed register name");
			return -1;
		}
		for ( char* p = tok + 1; *p; p ++ ) {
			if ( !isdigit((unsigned char)*p) ) {
				if ( strict ) print_syntax_error(line, "Malformed register name");
				return -1;
			}
		}

		char* end = NULL;
		errno = 0;
		long ri = strtol(tok + 1, &end, 10);
		if ( errno == ERANGE || end == tok + 1 || *end != '\0' || ri < 0 || ri >= 32 ) {
			if ( strict ) print_syntax_error(line, "Malformed register name");
			return -1;
		}
		return (int)ri;
	}
	if ( streq(tok, "zero") ) return 0; 
	if ( streq(tok, "ra") ) return 1;
	if ( streq(tok, "sp") ) return 2;
	if ( streq(tok, "gp") ) return 3;
	if ( streq(tok, "tp") ) return 4;
	if ( streq(tok, "t0") ) return 5;
	if ( streq(tok, "t1") ) return 6;
	if ( streq(tok, "t2") ) return 7;
	if ( streq(tok, "s0") ) return 8;
	if ( streq(tok, "s1") ) return 9;
	if ( streq(tok, "a0") ) return 10;
	if ( streq(tok, "a1") ) return 11;
	if ( streq(tok, "a2") ) return 12;
	if ( streq(tok, "a3") ) return 13;
	if ( streq(tok, "a4") ) return 14;
	if ( streq(tok, "a5") ) return 15;
	if ( streq(tok, "a6") ) return 16;
	if ( streq(tok, "a7") ) return 17;
	if ( streq(tok, "s2") ) return 18;
	if ( streq(tok, "s3") ) return 19;
	if ( streq(tok, "s4") ) return 20;
	if ( streq(tok, "s5") ) return 21;
	if ( streq(tok, "s6") ) return 22;
	if ( streq(tok, "s7") ) return 23;
	if ( streq(tok, "s8") ) return 24;
	if ( streq(tok, "s9") ) return 25;
	if ( streq(tok, "s10") ) return 26;
	if ( streq(tok, "s11") ) return 27;
	if ( streq(tok, "t3") ) return 28;
	if ( streq(tok, "t4") ) return 29;
	if ( streq(tok, "t5") ) return 30;
	if ( streq(tok, "t6") ) return 31;

	if ( strict ) print_syntax_error(line, "Malformed register name");
	return -1;
}

uint32_t parse_imm(char* tok, int bits, int line, bool strict = true) {
	if ( !tok || tok[0] == '\0' ) {
		if ( strict ) print_syntax_error(line, "Malformed immediate value");
		return 0;
	}

	char* end = NULL;
	errno = 0;
	long long imml = strtoll(tok, &end, 0);
	if ( errno == ERANGE || end == tok || (strict && *end != '\0') ) {
		if ( strict ) print_syntax_error(line, "Malformed immediate value");
		return 0;
	}

	long long minValue = -(1LL << (bits - 1));
	unsigned long long maxValue = (1ULL << bits) - 1;
	if ( imml < minValue || (imml >= 0 && (unsigned long long)imml > maxValue) ) {
		printf( "Syntax error at token %s\n", tok );
		exit(1);
	}

	return (uint32_t)imml;
}

void parse_mem(char* tok, int* reg, uint32_t* imm, int bits, int line) {
	if ( !tok ) print_syntax_error(line, "Malformed memory operand");

	char* openParen = strchr(tok, '(');
	char* closeParen = strchr(tok, ')');
	if ( !openParen || !closeParen || closeParen < openParen || closeParen[1] != '\0' ) {
		print_syntax_error(line, "Malformed memory operand");
	}

	*openParen = '\0';
	*closeParen = '\0';
	*imm = parse_imm(tok, bits, line);
	*reg = parse_reg(openParen + 1, line);
}

uint32_t mem_flush_words = 0;
uint32_t mem_write_reqs = 0;
uint32_t cache_write_hits = 0;
uint32_t mem_read_reqs = 0;
uint32_t cache_read_hits = 0;


void mem_write(uint8_t* mem, uint32_t addr, uint32_t data, instr_type op) {
	int bytes = 0;
	switch ( op ) {
		case SB: bytes = 1; break;
		case SH: bytes = 2; break;
		case SW: bytes = 4; break;
		default: break;
	}

	if ( addr == MEM_BYTES ) {
		printf( "[System output]: 0x%x\n", data );
		return;
	}
	if ( bytes == 0 || addr >= MEM_BYTES || (uint32_t)bytes > MEM_BYTES - addr ) {
		printf( "Memory write out of bounds at address 0x%08x\n", addr );
		exit(1);
	}
	if ( bytes > 1 && (addr & (uint32_t)(bytes - 1)) != 0 ) {
		printf( "Misaligned memory write at address 0x%08x\n", addr );
		exit(1);
	}

	mem_write_reqs ++;
	int way = cache_peek(addr, bytes);
	if ( way < 0 ) {
		cache_flush(addr, mem);
		uint32_t lineBytes = CACHE_LINE_WORD * 4;
		uint32_t waddr = addr & ~(lineBytes - 1u);
		if ( waddr > MEM_BYTES - lineBytes ) {
			printf( "Cache line out of memory bounds at address 0x%08x\n", waddr );
			exit(1);
		}
		for ( int i = 0; i < CACHE_LINE_WORD; i ++ ) {
			cache_update(waddr + (i * 4), readWordLE(mem, waddr + (i * 4)));
		}
		mem_flush_words += CACHE_LINE_WORD;
	} else {
		cache_write_hits ++;
	}
	cache_write(addr, data, bytes);
}

uint32_t mem_read(uint8_t* mem, uint32_t addr, instr_type op) {
	uint32_t ret = 0;
	int bytes = 0;

	switch ( op ) {
		case LB:
		case LBU: bytes = 1; break;
		case LH:
		case LHU: bytes = 2; break;
		case LW: bytes = 4; break;
		default: break;
	}

	if ( bytes == 0 || addr >= MEM_BYTES || (uint32_t)bytes > MEM_BYTES - addr ) {
		printf( "Memory read out of bounds at address 0x%08x\n", addr );
		exit(1);
	}
	if ( bytes > 1 && (addr & (uint32_t)(bytes - 1)) != 0 ) {
		printf( "Misaligned memory read at address 0x%08x\n", addr );
		exit(1);
	}

	mem_read_reqs ++;
	int way = cache_peek(addr, bytes);
	if ( way < 0 ) {
		cache_flush(addr, mem);
		mem_flush_words += CACHE_LINE_WORD;
		uint32_t lineBytes = CACHE_LINE_WORD * 4;
		uint32_t waddr = addr & ~(lineBytes - 1u);
		if ( waddr > MEM_BYTES - lineBytes ) {
			printf( "Cache line out of memory bounds at address 0x%08x\n", waddr );
			exit(1);
		}
		for ( int i = 0; i < CACHE_LINE_WORD; i ++ ) {
			cache_update(waddr + (i * 4), readWordLE(mem, waddr + (i * 4)));
		}
	} else {
		cache_read_hits ++;
	}

	uint32_t cr = cache_read(addr, bytes);
	switch ( op ) {
		case LB: ret = signextend(cr, 8); break;
		case LBU: ret = cr & 0xff; break;
		case LH: ret = signextend(cr, 16); break;
		case LHU: ret = cr & 0xffff; break;
		case LW: ret = cr; break;
		default: break;
	}

	return ret;
}

typedef enum {
	OPTYPE_NONE, // more like "don't care"
	OPTYPE_REG,
	OPTYPE_IMM,
	OPTYPE_LABEL,
} operand_type;
typedef struct {
	operand_type type = OPTYPE_NONE;
	char label[MAX_LABEL_LEN];
	int reg;
	uint32_t imm;

} operand;
typedef struct {
	instr_type op;
	operand a1;
	operand a2;
	operand a3;
	char* psrc = NULL;
	int orig_line=-1;
	bool breakpoint = false;
} instr;

void append_source(const char* op, const char* a1, const char* a2, const char* a3, source* src, instr* i) {
	char tbuf[128];
	int slen = -1;
	if ( op && a1 && !a2 && !a3 ) {
		slen = snprintf(tbuf, sizeof(tbuf), "%s %s", op, a1);
	} else if ( op && a1 && a2 && !a3 ) {
		slen = snprintf(tbuf, sizeof(tbuf), "%s %s, %s", op, a1, a2);
	} else if ( op && a1 && a2 && a3 ) {
		slen = snprintf(tbuf, sizeof(tbuf), "%s %s, %s, %s", op, a1, a2, a3);
	} else {
		return;
	}

	if ( slen < 0 || slen >= (int)sizeof(tbuf) ) {
		printf( "Compiled instruction text is too long\n" );
		exit(1);
	}
	if ( src->offset < 0 || src->offset > MAX_SRC_LEN - (slen + 1) ) {
		printf( "Compiled instruction source buffer is full\n" );
		exit(1);
	}

	memcpy(src->src + src->offset, tbuf, (size_t)slen + 1);
	i->psrc = src->src + src->offset;
	src->offset += slen + 1;
}


typedef struct {
	char label[MAX_LABEL_LEN];
	int loc = -1;
} label_loc;

uint32_t label_addr(char* label , label_loc* labels, int label_count, int orig_line) {
	for ( int i = 0; i < label_count; i++ ) {
		if (streq(labels[i].label, label)) return labels[i].loc;
	}
	print_syntax_error(orig_line, "Undefined label" );
}



//typedef enum {SECTION_NONE, SECTION_TEXT, SECTION_DATA} sectionType;
int parse_data_element(int line, int size, uint8_t* mem, int offset) {
	while ( char* t = strtok(NULL, " \t\r\n") ) {
		char* end = NULL;
		errno = 0;
		long long parsed = strtoll(t, &end, 0);
		long long minValue = -(1LL << ((size * 8) - 1));
		unsigned long long maxValue = (1ULL << (size * 8)) - 1;
		if ( errno == ERANGE || end == t || *end != '\0' || parsed < minValue ||
		     (parsed >= 0 && (unsigned long long)parsed > maxValue) ) {
			printf( "Value out of bounds at line %d : %s\n", line, t );
			exit(2);
		}
		if ( offset < 0 || offset > MEM_BYTES - size ) {
			printf( "Data segment out of bounds at line %d\n", line );
			exit(2);
		}

		uint64_t value = (uint64_t)parsed;
		for ( int i = 0; i < size; i ++ ) {
			mem[offset + i] = (uint8_t)((value >> (i * 8)) & 0xffu);
		}
		offset += size;
	}
	return offset;
}

int parse_data_zero(int line, uint8_t* mem, int offset) {
	char* t = strtok(NULL, " \t\r\n");
	if ( !t ) print_syntax_error(line, "Missing .zero size");

	char* end = NULL;
	errno = 0;
	long bytes = strtol(t, &end, 0);
	if ( errno == ERANGE || end == t || *end != '\0' || bytes < 0 ||
	     offset < 0 || offset > MEM_BYTES ||
	     (unsigned long)bytes > (unsigned long)(MEM_BYTES - offset) ) {
		printf( "Data segment out of bounds at line %d\n", line );
		exit(2);
	}

	memset(&mem[offset], 0, (size_t)bytes);
	return offset + (int)bytes;
}
int parse_assembler_directive(int line, char* ftok, uint8_t* mem, int memoff) {
	//printf( "assembler directive %s\n", ftok );
	if ( 0 == memcmp(ftok, ".text", strlen(ftok) ) ) {
		if (strtok(NULL, " \t\r\n") ) {
			print_syntax_error(line, "Tokens after assembler directive");
		}
		//cur_section = SECTION_TEXT;
		memoff = TEXT_OFFSET;
		//printf( "starting text section\n" );
	} else if ( 0 == memcmp(ftok, ".data", strlen(ftok) ) ) {
		//cur_section = SECTION_TEXT;
		memoff = DATA_OFFSET;
		//printf( "starting data section\n" );
	} else if ( 0 == memcmp(ftok, ".byte", strlen(ftok)) ) memoff = parse_data_element(line, 1, mem, memoff);
	else if ( 0 == memcmp(ftok, ".half", strlen(ftok)) ) memoff = parse_data_element(line, 2, mem, memoff);
	else if ( 0 == memcmp(ftok, ".word", strlen(ftok)) ) memoff = parse_data_element(line, 4, mem, memoff);
	else if ( 0 == memcmp(ftok, ".zero", strlen(ftok)) ) memoff = parse_data_zero(line, mem, memoff);
	else {
		printf( "Undefined assembler directive at line %d: %s\n", line, ftok );
		//exit(3);
	}
	return memoff;
}

int parse_pseudoinstructions(int line, char* ftok, instr* imem, int ioff, label_loc* labels, char* o1, char* o2, char* o3, char* o4, source* src) {

	if ( streq(ftok, "li" ) ) {
		if ( !o1 || !o2 || o3 ) print_syntax_error(line, "Invalid format");

		int reg = parse_reg(o1, line);
		long int imml = strtol(o2, NULL, 0);


		if (reg < 0 || imml > UINT32_MAX || imml < INT32_MIN ) {
			printf( "Syntax error at line %d -- %lx, %x\n", line, imml, INT32_MAX);
			exit(1);
		}
		uint32_t hv = (uint32_t)imml;

		char areg[4]; sprintf(areg, "x%02d", reg);
		char immu[12]; sprintf(immu, "0x%08x" , (hv>>12));
		char immd[12]; sprintf(immd, "0x%08x" , (hv&((1<<12)-1)));


		instr* i = &imem[ioff];
		i->op = LUI;
		i->a1.type = OPTYPE_REG; i->a1.reg = reg;
		i->a2.type = OPTYPE_IMM; i->a2.imm = hv>>12;
		i->orig_line = line;
		append_source("lui", areg, immu, NULL, src, i);
		instr* i2 = &imem[ioff+1];

		i2->op = ADDI;
		i2->a1.type = OPTYPE_REG; i2->a1.reg = reg;
		i2->a2.type = OPTYPE_REG; i2->a2.reg = reg;
		i2->a3.type = OPTYPE_IMM; i2->a3.imm = (hv&((1<<12)-1)); 
		i2->orig_line = line;
		append_source("addi", areg, areg, immd, src, i2);
		//printf( ">> %d %x %d\n", reg, i->a2.imm, i->a2.imm );
		//printf( ">> %d %x %d\n", reg, i2->a3.imm, i2->a3.imm );
		return 2;
	}
	if ( streq(ftok, "la") || streq(ftok, "lla") ) {
		if ( !o1 || !o2 || o3 ) print_syntax_error(line, "Invalid format");

		int reg = parse_reg(o1, line);

		instr* i = &imem[ioff];
		i->op = LUI;
		i->a1.type = OPTYPE_REG; i->a1.reg = reg;
		i->a2.type = OPTYPE_LABEL; copyLabel(i->a2.label, o2, line);
		i->orig_line = line;
		//append_source(ftok, o1, o2, o3, src, i); // done in normalize
		instr* i2 = &imem[ioff+1];
		i2->op = ADDI;
		i2->a1.type = OPTYPE_REG; i2->a1.reg = reg;
		i2->a2.type = OPTYPE_REG; i2->a2.reg = reg;
		i2->a3.type = OPTYPE_LABEL; copyLabel(i2->a3.label, o2, line);
		i2->orig_line = line;
		//append_source(ftok, o1, o2, o3, src, i2); // done in normalize
		return 2;
	}
	if ( streq(ftok, "nop" )) {
		if ( o1 ) print_syntax_error(line, "Invalid format");

		instr* i = &imem[ioff];
		i->op = ADDI;
		i->a1.type = OPTYPE_REG; i->a1.reg = 0;
		i->a2.type = OPTYPE_REG; i->a2.reg = 0;
		i->a3.type = OPTYPE_IMM; i->a3.imm = 0;
		i->orig_line = line;
		append_source("nop", "zero", "zero", "0", src, i);
		return 1;
	}
	if ( streq(ftok, "ret" )) {
		if ( o1 ) print_syntax_error(line, "Invalid format");

		instr* i = &imem[ioff];
		i->op = JALR;
		i->a1.type = OPTYPE_REG; i->a1.reg = 0;
		i->a2.type = OPTYPE_REG; i->a2.reg = 1;
		i->a3.type = OPTYPE_IMM; i->a3.imm = 0;
		i->orig_line = line;
		append_source("jalr", "x0", "x1", "x0", src, i);
		return 1;
	}
	if ( streq(ftok, "jr" )) {
		if ( !o1 || o2 ) print_syntax_error(line, "Invalid format");

		instr* i = &imem[ioff];
		i->op = JALR;
		i->a1.type = OPTYPE_REG; i->a1.reg = 0;
		i->a2.type = OPTYPE_REG; i->a2.reg = parse_reg(o1, line);
		i->a3.type = OPTYPE_IMM; i->a3.imm = 0;
		i->orig_line = line;
		append_source("jalr", "x0", o1, "x0", src, i);
		return 1;
	}
	if ( streq(ftok, "j" )) {
		if ( !o1 || o2 ) print_syntax_error(line, "Invalid format");

		instr* i = &imem[ioff];
		i->op = JAL;
		i->a1.type = OPTYPE_REG; i->a1.reg = 0;
		i->a2.type = OPTYPE_LABEL; copyLabel(i->a2.label, o1, line);
		i->orig_line = line;
		append_source("j", "x0", o1, NULL, src, i);
		return 1;
	}
	if ( streq(ftok, "call" )) {
		if ( !o1 || o2 ) print_syntax_error(line, "Invalid format");

		instr* i = &imem[ioff];
		i->op = JAL;
		i->a1.type = OPTYPE_REG; i->a1.reg = 1;
		i->a2.type = OPTYPE_LABEL; copyLabel(i->a2.label, o1, line);
		i->orig_line = line;
		append_source("jal", "x1", o1, NULL, src, i);
		return 1;
	}
	if ( streq(ftok, "mv" )) {
		if ( !o1 || !o2 || o3 ) print_syntax_error(line, "Invalid format");
		instr* i = &imem[ioff];
		i->op = ADDI;
		i->a1.type = OPTYPE_REG; i->a1.reg = parse_reg(o1, line);
		i->a2.type = OPTYPE_REG; i->a2.reg = parse_reg(o2, line);
		i->a3.type = OPTYPE_IMM; i->a3.imm = 0;
		i->orig_line = line;
		append_source("addi",o1, o2, NULL, src, i);
		return 1;
	}
	if ( streq(ftok, "bnez" )) {
		if ( !o1 || !o2 || o3 ) print_syntax_error(line, "Invalid format");
		instr* i = &imem[ioff];
		i->op = BNE;
		i->a1.type = OPTYPE_REG; i->a1.reg = parse_reg(o1, line);
		i->a2.type = OPTYPE_REG; i->a2.reg = 0;
		i->a3.type = OPTYPE_LABEL; copyLabel(i->a3.label, o2, line);
		i->orig_line = line;
		append_source("bne", "x0", o1, o2, src, i);
		return 1;
	}
	if ( streq(ftok, "beqz" )) {
		if ( !o1 || !o2 || o3 ) print_syntax_error(line, "Invalid format");
		instr* i = &imem[ioff];
		i->op = BEQ;
		i->a1.type = OPTYPE_REG; i->a1.reg = parse_reg(o1, line);
		i->a2.type = OPTYPE_REG; i->a2.reg = 0;
		i->a3.type = OPTYPE_LABEL; copyLabel(i->a3.label, o2, line);
		i->orig_line = line;
		append_source("beq", "x0", o1, o2, src, i);
		return 1;
	}
	if ( streq(ftok, "bgt" )) {
		if ( !o1 || !o2 || !o3 || o4 ) print_syntax_error(line, "Invalid format");
		instr* i = &imem[ioff];
		i->op = BLT;
		i->a1.type = OPTYPE_REG; i->a2.reg = parse_reg(o1, line);
		i->a2.type = OPTYPE_REG; i->a1.reg = parse_reg(o2, line);
		i->a3.type = OPTYPE_LABEL; copyLabel(i->a3.label, o3, line);
		i->orig_line = line;
		append_source("blt", o2, o1, o3, src, i);
		return 1;
	}
	if ( streq(ftok, "ble" )) {
		if ( !o1 || !o2 || !o3 || o4 ) print_syntax_error(line, "Invalid format");
		instr* i = &imem[ioff];
		i->op = BGE;
		i->a1.type = OPTYPE_REG; i->a2.reg = parse_reg(o1, line);
		i->a2.type = OPTYPE_REG; i->a1.reg = parse_reg(o2, line);
		i->a3.type = OPTYPE_LABEL; copyLabel(i->a3.label, o3, line);
		i->orig_line = line;
		append_source("bge", o2, o1, o3, src, i);
		return 1;
	}
	return 0;
}

int parse_instr(int line, char* ftok, instr* imem, int memoff, label_loc* labels, source* src) {
	int instrBytes = 4;
	if ( streq(ftok, "li") || streq(ftok, "la") || streq(ftok, "lla") ) instrBytes = 8;
	if ( memoff < TEXT_OFFSET || memoff > DATA_OFFSET - instrBytes ) {
		printf( "Instructions exceed the text segment! Line %d\n", line );
		exit(1);
	}
	char* o1 = strtok(NULL, " \t\r\n,");
	char* o2 = strtok(NULL, " \t\r\n,");
	char* o3 = strtok(NULL, " \t\r\n,");
	char* o4 = strtok(NULL, " \t\r\n,");

	int ioff = memoff/4;
	int pscnt = parse_pseudoinstructions(line, ftok, imem, ioff, labels, o1, o2, o3, o4, src);
	if ( pscnt > 0 ) {
		return pscnt;
	} else {
		instr* i = &imem[ioff];
		instr_type op = parse_instr(ftok);
		i->op = op;
		i->orig_line = line;
		append_source(ftok, o1, o2, o3, src, i);

		switch( op ) {
			case UNIMPL: return 1;
			case JAL:
				if ( o2 ) { // two operands, reg, label
					if ( !o1 || !o2 || o3 || o4 ) print_syntax_error( line, "Invalid format" );
					i->a1.type = OPTYPE_REG; i->a1.reg = parse_reg(o1, line);
					i->a2.type = OPTYPE_LABEL; copyLabel(i->a2.label, o2, line);
				} else { // one operand, label
					if ( !o1 || o2 || o3 || o4 ) print_syntax_error( line, "Invalid format" );

					i->a1.type = OPTYPE_REG; i->a1.reg = 1;
					i->a2.type = OPTYPE_LABEL; copyLabel(i->a2.label, o1, line);
				}
				return 1;
			case JALR:
				if ( !o1 || !o2 || o3 || o4 ) print_syntax_error( line, "Invalid format" );
				i->a1.reg = parse_reg(o1, line);
				parse_mem(o2, &i->a2.reg, &i->a3.imm, 12, line);
				return 1;
			case ADD: case SUB: case SLT: case SLTU: case AND: case OR: case XOR: case SLL: case SRL: case SRA:
				if ( !o1 || !o2 || !o3 || o4 ) print_syntax_error( line, "Invalid format" );
				i->a1.reg = parse_reg(o1, line);
				i->a2.reg = parse_reg(o2, line);
				i->a3.reg = parse_reg(o3, line);
				return 1;
			case LB: case LBU: case LH: case LHU: case LW: case SB: case SH: case SW:
				if ( !o1 || !o2 || o3 || o4 ) print_syntax_error( line, "Invalid format" );
				i->a1.reg = parse_reg(o1, line);
				parse_mem(o2, &i->a2.reg, &i->a3.imm, 12, line);
				return 1;
			case ADDI: case SLTI: case SLTIU: case ANDI: case ORI: case XORI: case SLLI: case SRLI: case SRAI:
				if ( !o1 || !o2 || !o3 || o4 ) print_syntax_error( line, "Invalid format" );

				i->a1.reg = parse_reg(o1, line);
				i->a2.reg = parse_reg(o2, line);
				i->a3.imm = signextend(parse_imm(o3, 12, line), 12);
				return 1;
			case BEQ: case BGE: case BGEU: case BLT: case BLTU: case BNE:
				if ( !o1 || !o2 || !o3 || o4 ) print_syntax_error( line, "Invalid format" );
				i->a1.reg = parse_reg(o1, line);
				i->a2.reg = parse_reg(o2, line);
				i->a3.type = OPTYPE_LABEL; copyLabel(i->a3.label, o3, line);
				return 1;
			case LUI: 
			case AUIPC: // how to deal with LSB correctly? FIXME
				if ( !o1 || !o2 || o3 || o4 ) print_syntax_error( line, "Invalid format" );
				i->a1.reg = parse_reg(o1, line);
				i->a2.imm = (parse_imm(o2, 20, line));
				return 1;
			case HCF: return 1;
		}

	}
	return 1;
}

void parse(FILE* fin, uint8_t* mem, instr* imem, int& memoff, label_loc* labels, int& label_count, source* src) {
	int line = 0;

	printf( "Parsing input file\n" );

	//sectionType cur_section = SECTION_NONE;

	char rbuf[1024];
	while(!feof(fin)) {
		if ( !fgets(rbuf, 1024, fin) ) break;
		for ( char* p = rbuf; *p; p ++ ) *p = tolower((unsigned char)*p);
		line++;
		
		char *comment_ptr = strchr(rbuf, '#');
		if ( comment_ptr ) *comment_ptr = '\0';

		char* ftok = strtok(rbuf, " \t\r\n");
		if ( !ftok ) continue;

		if ( ftok[strlen(ftok)-1] == ':' ) {
			ftok[strlen(ftok)-1] = 0;
			if ( strlen(ftok) >= MAX_LABEL_LEN ) {
				printf( "Exceeded maximum length of label: %s\n", ftok );
				exit(3);
			}
			if ( label_count >= MAX_LABEL_COUNT ) {
				printf( "Exceeded maximum number of supported labels" );
				exit(3);
			}
			copyLabel(labels[label_count].label, ftok, line);
			labels[label_count].loc = memoff;
			label_count++;
			//printf( "Parsing label %s at mem %x\n", ftok, memoff );

			char* ntok = strtok(NULL, " \t\r\n");
			// there is more code after label
			if ( ntok ) {
				if ( ntok[0] == '.' ) {
					memoff = parse_assembler_directive(line, ntok, mem, memoff);
				} else {
					int count = parse_instr(line, ntok, imem, memoff, labels, src);
					for ( int i = 0; i < count; i ++ ) writeWordLE(mem, memoff + (i * 4), 0xcccccccc);
					memoff += count*4;
				}
			}
		} else if ( ftok[0] == '.' ) {
			memoff = parse_assembler_directive(line, ftok, mem, memoff);
		} else {
			int count = parse_instr(line, ftok, imem, memoff, labels, src);
			for ( int i = 0; i < count; i ++ ) writeWordLE(mem, memoff + (i * 4), 0xcccccccc);
			memoff += count*4;
		}
	}
}

void execute(uint8_t* mem, instr* imem, label_loc* labels, int label_count, bool start_immediate) {
	uint32_t rf[32];
	uint32_t rf_mirror[32];
	uint32_t pc = 0;
	uint32_t inst_cnt = 0;
	for ( int i = 0; i < 32; i++ ) {
		rf[i] = 0;
		rf_mirror[i] = 0;
	}

	bool stepping = !start_immediate;
	int stepcnt = 0;
	char keybuf[128];

	bool dexit = false;
	while(!dexit) {
		if ( (pc & 0x3u) != 0 ) {
			printf( "Instruction address misaligned: 0x%08x\n", pc );
			exit(1);
		}
		if ( pc >= DATA_OFFSET ) {
			printf( "Instruction address out of bounds: 0x%08x\n", pc );
			exit(1);
		}

		uint32_t iid = pc / 4;
		instr i = imem[iid];
		inst_cnt ++;

		if ( stepping || i.breakpoint ) {
			if ( stepcnt > 0 ) {
				stepcnt -= 1;
			} 

			if ( stepcnt == 0 || i.breakpoint ) {
				stepping = true;
				printf( "\n" );
				if ( i.psrc ) printf( "Next: %s\n", i.psrc );
				while (true) {
					printf( "[inst: %6d pc: %6d, src line %4d]\n", inst_cnt, pc, i.orig_line );

					std::string linebuf;
					fflush(stdout);
					linenoise::Readline(">>", linebuf);
					snprintf(keybuf, sizeof(keybuf), "%s", linebuf.c_str());
					linenoise::AddHistory(linebuf.c_str());
					//while ((kbp = linenoise?::Readline(">>")) == NULL);
					//fgets(keybuf, 128, stdin);


					for ( int i = 0; i < strlen(keybuf); i++ ) if (keybuf[i] == '\n') keybuf[i] = '\0';

					if ( keybuf[0] == 'q' ) {
						printf( "Quit command input! Exiting...\n" );
						exit(0);
					}
					if ( keybuf[0] == 'c' ) {
						stepping = false;
						break;
					}
					if ( strlen(keybuf) == 0 ) {
						break;
					}
					if ( keybuf[0] == 's' ) {
						stepcnt = parse_imm(keybuf+1, 16, 0, false);
						break;
					}
					if ( keybuf[0] == 'b' ) { // todo breakpoint!
						uint32_t break_line = parse_imm(keybuf+1, 16, 0, false);
						if ( strlen(keybuf+1) == 0 ) {
							for ( int i = 0; i < DATA_OFFSET/4; i++ ) {
								if ( imem[i].breakpoint ) printf( "Break at line %d: %s\n", imem[i].orig_line, imem[i].psrc );
							}
						} else {
							for ( int i = 0; i < DATA_OFFSET/4; i++ ) {
								if ( imem[i].orig_line >= break_line ) {
									printf( "Break point added to line %d\n", break_line );
									imem[i].breakpoint = true;
									break;
								}
							}
						}
					}
					if ( keybuf[0] == 'B' ) { // breakpoint remove
						uint32_t break_line = parse_imm(keybuf+1, 16, 0, false);
						for ( int i = 0; i < DATA_OFFSET/4; i++ ) {
							if ( imem[i].orig_line == break_line && imem[i].breakpoint ) {
								printf( "Break point removed from line %d\n", break_line );
								imem[i].breakpoint = false;
								break;
							}
						}
					}
					if ( keybuf[0] == 'r' ) {
						int reg = parse_reg(keybuf+1, 0, false);
						if ( reg >= 0 ) printf( "rf[%2d] = 0x%x\n", reg, rf[reg] );
						if ( reg < 0 ) print_regfile(rf);
					}
					if ( keybuf[0] == 'm' ) {
						uint32_t addr = parse_imm(keybuf + 1, 31, 0, false);
						int cnt = 1;
						char* ftok = strtok(keybuf, " \t\r\n");
						ftok = strtok(NULL, " \t\r\n");
						if ( ftok ) cnt = parse_imm(ftok, 16, 0, false);

						uint64_t byteCount = (uint64_t)cnt * 4;
						if ( addr >= MEM_BYTES || byteCount > MEM_BYTES - addr ) {
							printf( "Memory inspection out of bounds at address 0x%08x\n", addr );
							continue;
						}

						for ( int w = 0; w < cnt; w ++ ) {
							printf( "0x%04x: ", addr + (w * 4) );
							for ( int i = 0; i < 4; i ++ ) {
								printf( "%02x ", mem[addr + (w * 4) + i] );
							}
							printf( "\n" );
						}
					}
					if ( keybuf[0] == 'l' ) {
						printf( "Listing compiled isntructions\n" );
						printf( " srcline : Compiled instruction\n" );
						for ( int i = 0; i < DATA_OFFSET/4; i++ ) {
							instr* ii = &imem[i];
							if ( ii->orig_line >= 0 && ii->psrc ) {
								printf( "%9d: %s\n", ii->orig_line, ii->psrc );
							}
						}
					}
				}
			}
		}
		
		uint32_t pc_next = pc + 4;
		switch (i.op) {
			case ADD: rf[i.a1.reg] = rf[i.a2.reg] + rf[i.a3.reg]; break;
			case SUB: rf[i.a1.reg] = rf[i.a2.reg] - rf[i.a3.reg]; break;
			case SLT: rf[i.a1.reg] = asSigned32(rf[i.a2.reg]) < asSigned32(rf[i.a3.reg]) ? 1 : 0; break;
			case SLTU: rf[i.a1.reg] = rf[i.a2.reg] < rf[i.a3.reg] ? 1 : 0; break;
			case AND: rf[i.a1.reg] = rf[i.a2.reg] & rf[i.a3.reg]; break;
			case OR: rf[i.a1.reg] = rf[i.a2.reg] | rf[i.a3.reg]; break;
			case XOR: rf[i.a1.reg] = rf[i.a2.reg] ^ rf[i.a3.reg]; break;
			case SLL: rf[i.a1.reg] = rf[i.a2.reg] << (rf[i.a3.reg] & 0x1f); break;
			case SRL: rf[i.a1.reg] = rf[i.a2.reg] >> (rf[i.a3.reg] & 0x1f); break;
			case SRA: rf[i.a1.reg] = arithmeticShiftRight(rf[i.a2.reg], rf[i.a3.reg]); break;


			case ADDI: rf[i.a1.reg] = rf[i.a2.reg] + i.a3.imm; break;
			case SLTI: rf[i.a1.reg] = asSigned32(rf[i.a2.reg]) < asSigned32(i.a3.imm) ? 1 : 0; break;
			case SLTIU: rf[i.a1.reg] = rf[i.a2.reg] < i.a3.imm ? 1 : 0; break;
			case ANDI: rf[i.a1.reg] = rf[i.a2.reg] & i.a3.imm; break;
			case ORI: rf[i.a1.reg] = rf[i.a2.reg] | i.a3.imm; break;
			case XORI: rf[i.a1.reg] = rf[i.a2.reg] ^ i.a3.imm; break;
			case SLLI: rf[i.a1.reg] = rf[i.a2.reg] << (i.a3.imm & 0x1f); break;
			case SRLI: rf[i.a1.reg] = rf[i.a2.reg] >> (i.a3.imm & 0x1f); break;
			case SRAI: rf[i.a1.reg] = arithmeticShiftRight(rf[i.a2.reg], i.a3.imm); break;

			case LB:
			case LBU: 
			case LH:
			case LHU:
			case LW: rf[i.a1.reg] = mem_read(mem, rf[i.a2.reg]+i.a3.imm, i.op); break;
			
			case SB: 
			case SH: 
			case SW: mem_write(mem, rf[i.a2.reg]+i.a3.imm, rf[i.a1.reg], i.op); break;
			/*

			case SB: mem[rf[i.a2.reg]+i.a3.imm] = *(uint8_t*)&(rf[i.a1.reg]); break;
			case SH: *(uint16_t*)&(mem[rf[i.a2.reg]+i.a3.imm]) = *(uint16_t*)&(rf[i.a1.reg]); break;
			case SW: 
				*(uint32_t*)&(mem[rf[i.a2.reg]+i.a3.imm]) = rf[i.a1.reg]; 
				//printf( "Writing %x to addr %x\n", rf[i.a1.reg], rf[i.a2.reg]+i.a3.imm );
			break;
			*/

			case BEQ: if ( rf[i.a1.reg] == rf[i.a2.reg] ) pc_next = i.a3.imm; break;
			case BGE: if ( asSigned32(rf[i.a1.reg]) >= asSigned32(rf[i.a2.reg]) ) pc_next = i.a3.imm; break;
			case BGEU: if ( rf[i.a1.reg] >= rf[i.a2.reg] ) pc_next = i.a3.imm; 
				break;
			case BLT: if ( asSigned32(rf[i.a1.reg]) < asSigned32(rf[i.a2.reg]) ) pc_next = i.a3.imm; break;
			case BLTU: if ( rf[i.a1.reg] < rf[i.a2.reg] ) pc_next = i.a3.imm; break;
			case BNE: if ( rf[i.a1.reg] != rf[i.a2.reg] ) pc_next = i.a3.imm; break;

			case JAL:
				rf[i.a1.reg] = pc + 4;
				pc_next = i.a2.imm;
				//printf( "jal %d %x\n", pc+4, pc_next );
				break;
			case JALR: {
				uint32_t jalrTarget = (rf[i.a2.reg] + i.a3.imm) & ~1u;
				rf[i.a1.reg] = pc + 4;
				pc_next = jalrTarget;
				//printf( "jalr %d %d(%d)\n", i.a1.reg, i.a3.imm, i.a2.reg );
				break;
			}
			case AUIPC:
				rf[i.a1.reg] = pc + (i.a2.imm<<12);
				//printf( "auipc %x \n", rf[i.a1.reg] );
				break;
			case LUI:
				rf[i.a1.reg] = (i.a2.imm<<12);
				//printf( "lui %x \n", rf[i.a1.reg] );
				break;
			
			case HCF:
				printf( "\n\n----------\n\n" );
				printf( "Reached Halt and Catch Fire instruction!\n" );
				printf( "inst: %6d pc: %6d src line: %d\n", inst_cnt, pc, i.orig_line );
				print_regfile(rf);
				printf( "Cache read %d/%d Cache write %d/%d\n", cache_read_hits, mem_read_reqs, cache_write_hits, mem_write_reqs );
				printf( "Cache flush words: %d\n", mem_flush_words);
				dexit = true;
				break;
			case UNIMPL:
			default:
				printf( "Reached an unimplemented instruction!\n" );
				if ( i.psrc ) printf( "Instruction: %s\n", i.psrc );
				printf( "inst: %6d pc: %6d src line: %d\n", inst_cnt, pc, i.orig_line );
				print_regfile(rf);
				dexit = true;
				break;
		}
		pc = pc_next;
		rf[0] = 0;// cleaner way to do this?
		if ( stepping ) {
			for ( int i = 0; i < 32; i++ ) {
				if ( rf[i] != rf_mirror[i] ) printf( ">> rf[x%02d] %x -> %x\n", i, rf_mirror[i], rf[i] );
				rf_mirror[i] = rf[i];
			}
		}

		//printf( "reg dst %d -> %x %d\n", i.a1.reg, rf[i.a1.reg], rf[i.a1.reg] );

		fflush(stdout);
		
	}
}

void normalize_labels(instr* imem, label_loc* labels, int label_count, source* src) {
	for ( int i = 0; i < DATA_OFFSET/4; i++ ) {
		instr* ii = &imem[i];
		if ( ii->op == UNIMPL ) continue;

		if ( ii->a1.type == OPTYPE_LABEL ) {
			ii->a1.type = OPTYPE_IMM;
			ii->a1.imm = label_addr(ii->a1.label, labels, label_count, ii->orig_line);
		}
		if ( ii->a2.type == OPTYPE_LABEL ) {
			ii->a2.type = OPTYPE_IMM;
			ii->a2.imm = label_addr(ii->a2.label, labels, label_count, ii->orig_line);
			switch (ii->op) {
				case LUI: {
					ii->a2.imm = (ii->a2.imm>>12); 
					char areg[4]; sprintf(areg, "x%02d", ii->a1.reg);
					char immu[12]; sprintf(immu, "0x%08x" , ii->a2.imm);
					//printf( "LUI %d 0x%x %s\n", ii->a1.reg, ii->a2.imm, immu );
					append_source("lui", areg, immu, NULL, src, ii);
					break;
				}
				case JAL:
				int pc = (i*4);
				int target = ii->a2.imm;
				int diff = pc - target;
				if ( diff < 0 ) diff = -diff;

				if ( diff >= (1<<21) ) {
					printf( "JAL instruction target out of bounds\n" );
					exit(3);
				}
				break;
			}
		}
		if ( ii->a3.type == OPTYPE_LABEL ) {
			ii->a3.type = OPTYPE_IMM;
			ii->a3.imm = label_addr(ii->a3.label, labels, label_count, ii->orig_line);
			switch(ii->op) {
				case ADDI: {
					ii->a3.imm = ii->a3.imm&((1<<12)-1);
					char a1reg[4]; sprintf(a1reg, "x%02d", ii->a1.reg);
					char a2reg[4]; sprintf(a2reg, "x%02d", ii->a2.reg);
					char immd[12]; sprintf(immd, "0x%08x" , ii->a3.imm);
					//printf( "ADDI %d %d 0x%x %s\n", ii->a1.reg, ii->a2.reg, ii->a3.imm, immd );
					append_source("addi", a1reg, a2reg, immd, src, ii);
					break;
				}
				case BEQ: case BGE: case BGEU: case BLT: case BLTU: case BNE: {
					int pc = (i*4);
					int target = ii->a3.imm;
					int diff = pc - target;
					if ( diff < 0 ) diff = -diff;

					if ( diff >= (1<<13) ) {
						printf( "Branch instruction target out of bounds\n" );
						exit(3);
					}
					break;
				}
			}
		}
	}
}

int
main(int argc, char** argv) {
	if ( argc < 2 ) {
		printf( "usage: %s asmfile\n", argv[0] );
		exit(1);
	}

	FILE* fin = fopen(argv[1], "r" );
	if ( !fin ) {
		printf( "%s: No such file\n", argv[1] );
		exit(2);
	}

	bool start_immediate = false;
	if ( argc >= 3 ) {
		start_immediate = true;
	}


	//ProcessorState* ps = new ProcessorState();
	
	int memoff = 0;
	uint8_t* mem = (uint8_t*)calloc(MEM_BYTES, sizeof(uint8_t));
	instr* imem = new (std::nothrow) instr[DATA_OFFSET / 4];
	label_loc* labels = new (std::nothrow) label_loc[MAX_LABEL_COUNT];
	int label_count = 0;
	source src;
	src.offset = 0;
	src.src = (char*)calloc(MAX_SRC_LEN, sizeof(char));

	if ( !mem || !labels || !imem || !src.src ) {
		printf( "Memory allocation failed\n" );
		exit(2);
	}

	for ( int i = 0; i < DATA_OFFSET / 4; i ++ ) {
		imem[i].op = UNIMPL;
		imem[i].a1.type = OPTYPE_NONE;
		imem[i].a1.label[0] = '\0';
		imem[i].a1.reg = 0;
		imem[i].a1.imm = 0;
		imem[i].a2.type = OPTYPE_NONE;
		imem[i].a2.label[0] = '\0';
		imem[i].a2.reg = 0;
		imem[i].a2.imm = 0;
		imem[i].a3.type = OPTYPE_NONE;
		imem[i].a3.label[0] = '\0';
		imem[i].a3.reg = 0;
		imem[i].a3.imm = 0;
		imem[i].psrc = NULL;
		imem[i].orig_line = -1;
		imem[i].breakpoint = false;
	}
	for ( int i = 0; i < MAX_LABEL_COUNT; i ++ ) {
		labels[i].label[0] = '\0';
		labels[i].loc = -1;
	}

	parse(fin, mem, imem, memoff, labels, label_count, &src);
	normalize_labels(imem, labels, label_count, &src);
	
	execute(mem, imem, labels, label_count, start_immediate);

	fclose(fin);
	free(mem);
	delete[] imem;
	delete[] labels;
	free(src.src);

	printf( "Execution done!\n" );
	return 0;
}
