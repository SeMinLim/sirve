#include "assembler.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "rv32i.h"

using namespace std;


typedef enum {
	ASSEMBLY_LINE_NONE = 0,
	ASSEMBLY_LINE_SECTION,
	ASSEMBLY_LINE_DATA,
	ASSEMBLY_LINE_INSTRUCTION
} AssemblyLineType;


typedef struct {
	AssemblyLineType type;
	int line;
	uint32_t address;
	uint32_t byteSize;
	bool dataSection;
	string op;
	string sourceText;
	vector<string> operands;
} AssemblyLine;


typedef struct {
	vector<AssemblyLine> lines;
	map<string, uint32_t> labels;
	uint32_t textEnd;
	uint32_t dataEnd;
	uint32_t entryAddr;
	uint32_t instructionCnt;
	bool hasEntry;
} AssemblyProgram;


static void clearError( AssemblerError *error ) {
	if ( error == NULL ) return;
	error->line = 0;
	error->message[0] = '\0';
}

static bool setError( AssemblerError *error, int line, const char *message ) {
	if ( error != NULL ) {
		error->line = line;
		snprintf(error->message, sizeof(error->message), "%s", message);
	}
	return false;
}

static bool setFormattedError(
	AssemblerError *error,
	int line,
	const char *format,
	const char *value
) {
	if ( error != NULL ) {
		error->line = line;
		snprintf(error->message, sizeof(error->message), format, value);
	}
	return false;
}

static string trim( const string &input ) {
	size_t first = 0;
	while ( first < input.size() && isspace((unsigned char)input[first]) ) first ++;

	size_t last = input.size();
	while ( last > first && isspace((unsigned char)input[last - 1]) ) last --;
	return input.substr(first, last - first);
}

static string normalizeLine( const string &input ) {
	string normalized = input;
	size_t commentPos = normalized.find('#');
	if ( commentPos != string::npos ) normalized.erase(commentPos);

	for ( size_t i = 0; i < normalized.size(); i ++ ) {
		normalized[i] = (char)tolower((unsigned char)normalized[i]);
	}
	return trim(normalized);
}

static vector<string> tokenize( const string &input ) {
	vector<string> tokens;
	string token;

	for ( size_t i = 0; i < input.size(); i ++ ) {
		char value = input[i];
		if ( isspace((unsigned char)value) || value == ',' ) {
			if ( !token.empty() ) {
				tokens.push_back(token);
				token.clear();
			}
		} else {
			token.push_back(value);
		}
	}
	if ( !token.empty() ) tokens.push_back(token);
	return tokens;
}

static bool isValidLabel( const string &label ) {
	if ( label.empty() ) return false;
	for ( size_t i = 0; i < label.size(); i ++ ) {
		char value = label[i];
		if ( isspace((unsigned char)value) || value == ',' || value == '(' || value == ')' ) {
			return false;
		}
	}
	return true;
}

static bool parseInteger( const string &token, int64_t *value ) {
	if ( token.empty() || value == NULL ) return false;

	char *end = NULL;
	errno = 0;
	long long parsed = strtoll(token.c_str(), &end, 0);
	if ( errno == ERANGE || end == token.c_str() || *end != '\0' ) return false;

	*value = (int64_t)parsed;
	return true;
}

static int32_t bitCastSigned32( uint32_t value ) {
	int32_t signedValue = 0;
	memcpy(&signedValue, &value, sizeof(signedValue));
	return signedValue;
}

static bool parseWordValue( const string &token, uint32_t *value ) {
	int64_t parsed = 0;
	if ( !parseInteger(token, &parsed) ) return false;
	if ( parsed < INT32_MIN || parsed > (int64_t)UINT32_MAX ) return false;
	*value = (uint32_t)parsed;
	return true;
}

static bool parseRegister( const string &token, uint32_t *reg ) {
	if ( reg == NULL ) return false;

	if ( token.size() >= 2 && token[0] == 'x' ) {
		for ( size_t i = 1; i < token.size(); i ++ ) {
			if ( !isdigit((unsigned char)token[i]) ) return false;
		}

		int64_t value = 0;
		if ( !parseInteger(token.substr(1), &value) || value < 0 || value >= 32 ) return false;
		*reg = (uint32_t)value;
		return true;
	}

	static const char *registerNames[32] = {
		"zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
		"s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
		"a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
		"s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
	};
	for ( uint32_t i = 0; i < 32; i ++ ) {
		if ( token == registerNames[i] ) {
			*reg = i;
			return true;
		}
	}
	if ( token == "fp" ) {
		*reg = 8;
		return true;
	}
	return false;
}

static bool requireOperandCnt(
	const AssemblyLine &line,
	size_t expected,
	AssemblerError *error
) {
	if ( line.operands.size() == expected ) return true;
	return setFormattedError(error, line.line, "Invalid operand count for %s", line.op.c_str());
}

static bool parseRegisterOperand(
	const AssemblyLine &line,
	size_t index,
	uint32_t *reg,
	AssemblerError *error
) {
	if ( index >= line.operands.size() || !parseRegister(line.operands[index], reg) ) {
		return setError(error, line.line, "Malformed register name");
	}
	return true;
}

static bool parseSignedImmediate(
	const string &token,
	int32_t minimum,
	int32_t maximum,
	int32_t *value,
	AssemblerError *error,
	int line
) {
	int64_t parsed = 0;
	if ( !parseInteger(token, &parsed) ) return setError(error, line, "Malformed immediate value");
	if ( parsed < minimum || parsed > maximum ) {
		return setError(error, line, "Immediate is outside the supported range");
	}
	*value = (int32_t)parsed;
	return true;
}

static bool parseUpperImmediate(
	const string &token,
	int32_t *value,
	AssemblerError *error,
	int line
) {
	int64_t parsed = 0;
	if ( !parseInteger(token, &parsed) ) return setError(error, line, "Malformed upper immediate");
	if ( parsed < -(1 << 19) || parsed > 0xfffff ) {
		return setError(error, line, "Upper immediate is outside the 20-bit range");
	}
	*value = (int32_t)parsed;
	return true;
}

static bool parseMemoryOperand(
	const string &token,
	int32_t *immediate,
	uint32_t *reg,
	AssemblerError *error,
	int line
) {
	size_t openPos = token.find('(');
	size_t closePos = token.find(')');
	if ( openPos == string::npos || closePos == string::npos || closePos != token.size() - 1 ||
	     closePos <= openPos + 1 ) {
		return setError(error, line, "Malformed memory operand");
	}

	string immediateToken = token.substr(0, openPos);
	string registerToken = token.substr(openPos + 1, closePos - openPos - 1);
	int64_t parsedImmediate = 0;
	if ( immediateToken.empty() ) parsedImmediate = 0;
	else if ( !parseInteger(immediateToken, &parsedImmediate) ) {
		return setError(error, line, "Malformed memory immediate");
	}
	if ( parsedImmediate < -2048 || parsedImmediate > 2047 ) {
		return setError(error, line, "Memory immediate is outside the signed 12-bit range");
	}
	if ( !parseRegister(registerToken, reg) ) {
		return setError(error, line, "Malformed register name");
	}

	*immediate = (int32_t)parsedImmediate;
	return true;
}

static bool isRealInstruction( const string &op ) {
	static const char *instructionNames[] = {
		"lui", "auipc", "jal", "jalr",
		"beq", "bne", "blt", "bge", "bltu", "bgeu",
		"lb", "lh", "lw", "lbu", "lhu", "sb", "sh", "sw",
		"addi", "slti", "sltiu", "xori", "ori", "andi",
		"slli", "srli", "srai",
		"add", "sub", "sll", "slt", "sltu", "xor", "srl", "sra", "or", "and",
		"fence", "ecall", "ebreak"
	};

	for ( size_t i = 0; i < sizeof(instructionNames) / sizeof(instructionNames[0]); i ++ ) {
		if ( op == instructionNames[i] ) return true;
	}
	return false;
}

static bool isPseudoInstruction( const string &op ) {
	static const char *pseudoNames[] = {
		"li", "la", "lla", "nop", "ret", "jr", "j", "call", "mv",
		"bnez", "beqz", "bgt", "ble", "hcf"
	};

	for ( size_t i = 0; i < sizeof(pseudoNames) / sizeof(pseudoNames[0]); i ++ ) {
		if ( op == pseudoNames[i] ) return true;
	}
	return false;
}

static int64_t floorDivide( int64_t value, int64_t divisor ) {
	if ( value >= 0 ) return value / divisor;
	return -((-value + divisor - 1) / divisor);
}

static void splitUpperLowerImmediate( int64_t value, int32_t *upper, int32_t *lower ) {
	int64_t upperValue = floorDivide(value + 0x800, 0x1000);
	int64_t lowerValue = value - (upperValue * 0x1000);
	*upper = (int32_t)upperValue;
	*lower = (int32_t)lowerValue;
}

static bool getInstructionSize(
	const string &op,
	const vector<string> &operands,
	uint32_t *byteSize,
	uint32_t *instructionCnt,
	AssemblerError *error,
	int line
) {
	if ( isRealInstruction(op) ) {
		*byteSize = 4;
		*instructionCnt = 1;
		return true;
	}
	if ( !isPseudoInstruction(op) ) {
		return setFormattedError(error, line, "Unknown instruction: %s", op.c_str());
	}

	if ( op == "li" ) {
		if ( operands.size() != 2 ) return setError(error, line, "Invalid li format");
		uint32_t rawValue = 0;
		if ( !parseWordValue(operands[1], &rawValue) ) {
			return setError(error, line, "Malformed li immediate");
		}
		int32_t signedValue = bitCastSigned32(rawValue);
		if ( signedValue >= -2048 && signedValue <= 2047 ) {
			*byteSize = 4;
			*instructionCnt = 1;
		} else {
			*byteSize = 8;
			*instructionCnt = 2;
		}
		return true;
	}
	if ( op == "la" || op == "lla" ) {
		*byteSize = 8;
		*instructionCnt = 2;
		return true;
	}

	*byteSize = 4;
	*instructionCnt = 1;
	return true;
}

static bool addLabel(
	const string &label,
	uint32_t address,
	map<string, uint32_t> *labels,
	AssemblerError *error,
	int line
) {
	if ( !isValidLabel(label) ) return setError(error, line, "Malformed label");
	if ( labels->find(label) != labels->end() ) {
		return setFormattedError(error, line, "Duplicate label: %s", label.c_str());
	}
	(*labels)[label] = address;
	return true;
}

static bool checkSectionBounds(
	bool dataSection,
	uint32_t currentAddress,
	uint32_t byteSize,
	uint32_t memorySize,
	uint32_t dataOffset,
	AssemblerError *error,
	int line
) {
	uint32_t sectionLimit = dataSection ? memorySize : dataOffset;
	if ( currentAddress > sectionLimit || byteSize > sectionLimit - currentAddress ) {
		if ( dataSection ) return setError(error, line, "Data segment out of bounds");
		return setError(error, line, "Instructions exceed the text segment!");
	}
	return true;
}

static bool parseSource(
	const char *filename,
	uint32_t memorySize,
	uint32_t textOffset,
	uint32_t dataOffset,
	AssemblyProgram *program,
	AssemblerError *error
) {
	ifstream input(filename);
	if ( !input.is_open() ) {
		return setFormattedError(error, 0, "Unable to open assembly file: %s", filename);
	}

	program->lines.clear();
	program->labels.clear();
	program->textEnd = textOffset;
	program->dataEnd = dataOffset;
	program->entryAddr = textOffset;
	program->instructionCnt = 0;
	program->hasEntry = false;

	uint32_t currentAddress = textOffset;
	bool dataSection = false;
	string rawLine;
	int lineNum = 0;

	while ( getline(input, rawLine) ) {
		lineNum ++;
		string lineText = normalizeLine(rawLine);
		if ( lineText.empty() ) continue;

		while ( true ) {
			size_t colonPos = lineText.find(':');
			if ( colonPos == string::npos ) break;

			string label = trim(lineText.substr(0, colonPos));
			if ( label.find_first_of(" \t,") != string::npos ) break;
			if ( !addLabel(label, currentAddress, &program->labels, error, lineNum) ) return false;
			lineText = trim(lineText.substr(colonPos + 1));
			if ( lineText.empty() ) break;
		}
		if ( lineText.empty() ) continue;

		vector<string> tokens = tokenize(lineText);
		if ( tokens.empty() ) continue;

		AssemblyLine line;
		line.type = ASSEMBLY_LINE_NONE;
		line.line = lineNum;
		line.address = currentAddress;
		line.byteSize = 0;
		line.dataSection = dataSection;
		line.op = tokens[0];
		line.sourceText = lineText;
		for ( size_t i = 1; i < tokens.size(); i ++ ) line.operands.push_back(tokens[i]);

		if ( line.op == ".text" || line.op == ".data" ) {
			if ( !line.operands.empty() ) return setError(error, lineNum, "Section directive has operands");
			line.type = ASSEMBLY_LINE_SECTION;
			dataSection = line.op == ".data";
			currentAddress = dataSection ? dataOffset : textOffset;
			line.address = currentAddress;
			line.dataSection = dataSection;
			program->lines.push_back(line);
			continue;
		}

		if ( line.op == ".byte" || line.op == ".half" || line.op == ".word" || line.op == ".zero" ) {
			line.type = ASSEMBLY_LINE_DATA;
			if ( line.op == ".zero" ) {
				if ( line.operands.size() != 1 ) return setError(error, lineNum, "Invalid .zero format");
				int64_t zeroSize = 0;
				if ( !parseInteger(line.operands[0], &zeroSize) || zeroSize < 0 ||
				     zeroSize > (int64_t)UINT32_MAX ) {
					return setError(error, lineNum, "Invalid .zero size");
				}
				line.byteSize = (uint32_t)zeroSize;
			} else {
				if ( line.operands.empty() ) return setError(error, lineNum, "Data directive has no values");
				uint32_t unitSize = line.op == ".byte" ? 1 : (line.op == ".half" ? 2 : 4);
				if ( line.operands.size() > UINT32_MAX / unitSize ) {
					return setError(error, lineNum, "Data directive is too large");
				}
				line.byteSize = (uint32_t)line.operands.size() * unitSize;
			}
		} else if ( !line.op.empty() && line.op[0] == '.' ) {
			return setFormattedError(error, lineNum, "Unsupported assembler directive: %s", line.op.c_str());
		} else {
			line.type = ASSEMBLY_LINE_INSTRUCTION;
			uint32_t instructionCnt = 0;
			if ( !getInstructionSize(line.op, line.operands, &line.byteSize, &instructionCnt,
			                         error, lineNum) ) {
				return false;
			}
			if ( dataSection ) return setError(error, lineNum, "Instruction found in data section");
			if ( (currentAddress & 0x3u) != 0 ) {
				return setError(error, lineNum, "Instruction address is not 4-byte aligned");
			}
			if ( !program->hasEntry ) {
				program->entryAddr = currentAddress;
				program->hasEntry = true;
			}
			program->instructionCnt += instructionCnt;
		}

		if ( !checkSectionBounds(dataSection, currentAddress, line.byteSize,
		                         memorySize, dataOffset, error, lineNum) ) {
			return false;
		}
		currentAddress += line.byteSize;
		if ( dataSection ) {
			if ( currentAddress > program->dataEnd ) program->dataEnd = currentAddress;
		} else {
			if ( currentAddress > program->textEnd ) program->textEnd = currentAddress;
		}
		program->lines.push_back(line);
	}
	return true;
}

static uint32_t encodeRType(
	uint32_t funct7,
	uint32_t rs2,
	uint32_t rs1,
	uint32_t funct3,
	uint32_t rd,
	uint32_t opcode
) {
	return ((funct7 & 0x7f) << 25) |
	       ((rs2 & 0x1f) << 20) |
	       ((rs1 & 0x1f) << 15) |
	       ((funct3 & 0x07) << 12) |
	       ((rd & 0x1f) << 7) |
	       (opcode & 0x7f);
}

static uint32_t encodeIType(
	int32_t immediate,
	uint32_t rs1,
	uint32_t funct3,
	uint32_t rd,
	uint32_t opcode
) {
	uint32_t value = (uint32_t)immediate & 0xfff;
	return (value << 20) |
	       ((rs1 & 0x1f) << 15) |
	       ((funct3 & 0x07) << 12) |
	       ((rd & 0x1f) << 7) |
	       (opcode & 0x7f);
}

static uint32_t encodeSType(
	int32_t immediate,
	uint32_t rs2,
	uint32_t rs1,
	uint32_t funct3,
	uint32_t opcode
) {
	uint32_t value = (uint32_t)immediate & 0xfff;
	return (((value >> 5) & 0x7f) << 25) |
	       ((rs2 & 0x1f) << 20) |
	       ((rs1 & 0x1f) << 15) |
	       ((funct3 & 0x07) << 12) |
	       ((value & 0x1f) << 7) |
	       (opcode & 0x7f);
}

static uint32_t encodeBType(
	int32_t immediate,
	uint32_t rs2,
	uint32_t rs1,
	uint32_t funct3,
	uint32_t opcode
) {
	uint32_t value = (uint32_t)immediate & 0x1fff;
	return (((value >> 12) & 0x01) << 31) |
	       (((value >> 5) & 0x3f) << 25) |
	       ((rs2 & 0x1f) << 20) |
	       ((rs1 & 0x1f) << 15) |
	       ((funct3 & 0x07) << 12) |
	       (((value >> 1) & 0x0f) << 8) |
	       (((value >> 11) & 0x01) << 7) |
	       (opcode & 0x7f);
}

static uint32_t encodeUType( int32_t immediate, uint32_t rd, uint32_t opcode ) {
	return (((uint32_t)immediate & 0xfffff) << 12) |
	       ((rd & 0x1f) << 7) |
	       (opcode & 0x7f);
}

static uint32_t encodeJType( int32_t immediate, uint32_t rd, uint32_t opcode ) {
	uint32_t value = (uint32_t)immediate & 0x1fffff;
	return (((value >> 20) & 0x01) << 31) |
	       (((value >> 1) & 0x3ff) << 21) |
	       (((value >> 11) & 0x01) << 20) |
	       (((value >> 12) & 0xff) << 12) |
	       ((rd & 0x1f) << 7) |
	       (opcode & 0x7f);
}

static void writeValueLE( uint8_t *memory, uint32_t address, uint32_t value, uint32_t bytes ) {
	for ( uint32_t i = 0; i < bytes; i ++ ) {
		memory[address + i] = (uint8_t)((value >> (i * 8)) & 0xffu);
	}
}

static bool resolveLabelOrImmediate(
	const string &token,
	const map<string, uint32_t> &labels,
	int64_t *value,
	bool labelIsAbsolute,
	uint32_t pc,
	AssemblerError *error,
	int line
) {
	map<string, uint32_t>::const_iterator labelIt = labels.find(token);
	if ( labelIt != labels.end() ) {
		if ( labelIsAbsolute ) *value = labelIt->second;
		else *value = (int64_t)labelIt->second - (int64_t)pc;
		return true;
	}
	if ( parseInteger(token, value) ) return true;
	return setFormattedError(error, line,
	                         "Undefined label or malformed immediate: %s", token.c_str());
}

static bool checkBranchOffset( int64_t offset, AssemblerError *error, int line ) {
	if ( (offset & 1) != 0 ) return setError(error, line, "Branch target is not 2-byte aligned");
	if ( offset < -4096 || offset > 4094 ) return setError(error, line, "Branch target is out of range");
	return true;
}

static bool checkJumpOffset( int64_t offset, AssemblerError *error, int line ) {
	if ( (offset & 1) != 0 ) return setError(error, line, "Jump target is not 2-byte aligned");
	if ( offset < -1048576 || offset > 1048574 ) return setError(error, line, "Jump target is out of range");
	return true;
}

static bool encodeRealInstruction(
	const AssemblyLine &line,
	const map<string, uint32_t> &labels,
	uint32_t *word,
	AssemblerError *error
) {
	uint32_t rd = 0;
	uint32_t rs1 = 0;
	uint32_t rs2 = 0;
	int32_t immediate = 0;
	int64_t target = 0;

	if ( line.op == "lui" || line.op == "auipc" ) {
		if ( !requireOperandCnt(line, 2, error) ||
		     !parseRegisterOperand(line, 0, &rd, error) ||
		     !parseUpperImmediate(line.operands[1], &immediate, error, line.line) ) return false;
		*word = encodeUType(immediate, rd, line.op == "lui" ? 0x37 : 0x17);
		return true;
	}

	if ( line.op == "jal" ) {
		if ( line.operands.size() == 1 ) {
			rd = 1;
			if ( !resolveLabelOrImmediate(line.operands[0], labels, &target, false,
			                              line.address, error, line.line) ) return false;
		} else if ( line.operands.size() == 2 ) {
			if ( !parseRegisterOperand(line, 0, &rd, error) ) return false;
			if ( !resolveLabelOrImmediate(line.operands[1], labels, &target, false,
			                              line.address, error, line.line) ) return false;
		} else {
			return setError(error, line.line, "Invalid operand count for jal");
		}
		if ( !checkJumpOffset(target, error, line.line) ) return false;
		*word = encodeJType((int32_t)target, rd, 0x6f);
		return true;
	}

	if ( line.op == "jalr" ) {
		if ( line.operands.size() == 2 ) {
			if ( !parseRegisterOperand(line, 0, &rd, error) ||
			     !parseMemoryOperand(line.operands[1], &immediate, &rs1, error, line.line) ) return false;
		} else if ( line.operands.size() == 3 ) {
			if ( !parseRegisterOperand(line, 0, &rd, error) ||
			     !parseRegisterOperand(line, 1, &rs1, error) ||
			     !parseSignedImmediate(line.operands[2], -2048, 2047, &immediate,
			                           error, line.line) ) return false;
		} else {
			return setError(error, line.line, "Invalid operand count for jalr");
		}
		*word = encodeIType(immediate, rs1, 0x0, rd, 0x67);
		return true;
	}

	if ( line.op == "beq" || line.op == "bne" || line.op == "blt" ||
	     line.op == "bge" || line.op == "bltu" || line.op == "bgeu" ) {
		if ( !requireOperandCnt(line, 3, error) ||
		     !parseRegisterOperand(line, 0, &rs1, error) ||
		     !parseRegisterOperand(line, 1, &rs2, error) ||
		     !resolveLabelOrImmediate(line.operands[2], labels, &target, false,
		                              line.address, error, line.line) ) return false;
		if ( !checkBranchOffset(target, error, line.line) ) return false;

		uint32_t funct3 = 0;
		if ( line.op == "bne" ) funct3 = 0x1;
		else if ( line.op == "blt" ) funct3 = 0x4;
		else if ( line.op == "bge" ) funct3 = 0x5;
		else if ( line.op == "bltu" ) funct3 = 0x6;
		else if ( line.op == "bgeu" ) funct3 = 0x7;
		*word = encodeBType((int32_t)target, rs2, rs1, funct3, 0x63);
		return true;
	}

	if ( line.op == "lb" || line.op == "lh" || line.op == "lw" ||
	     line.op == "lbu" || line.op == "lhu" ) {
		if ( !requireOperandCnt(line, 2, error) ||
		     !parseRegisterOperand(line, 0, &rd, error) ||
		     !parseMemoryOperand(line.operands[1], &immediate, &rs1, error, line.line) ) return false;

		uint32_t funct3 = 0;
		if ( line.op == "lh" ) funct3 = 0x1;
		else if ( line.op == "lw" ) funct3 = 0x2;
		else if ( line.op == "lbu" ) funct3 = 0x4;
		else if ( line.op == "lhu" ) funct3 = 0x5;
		*word = encodeIType(immediate, rs1, funct3, rd, 0x03);
		return true;
	}

	if ( line.op == "sb" || line.op == "sh" || line.op == "sw" ) {
		if ( !requireOperandCnt(line, 2, error) ||
		     !parseRegisterOperand(line, 0, &rs2, error) ||
		     !parseMemoryOperand(line.operands[1], &immediate, &rs1, error, line.line) ) return false;

		uint32_t funct3 = line.op == "sb" ? 0x0 : (line.op == "sh" ? 0x1 : 0x2);
		*word = encodeSType(immediate, rs2, rs1, funct3, 0x23);
		return true;
	}

	if ( line.op == "addi" || line.op == "slti" || line.op == "sltiu" ||
	     line.op == "xori" || line.op == "ori" || line.op == "andi" ) {
		if ( !requireOperandCnt(line, 3, error) ||
		     !parseRegisterOperand(line, 0, &rd, error) ||
		     !parseRegisterOperand(line, 1, &rs1, error) ||
		     !parseSignedImmediate(line.operands[2], -2048, 2047, &immediate,
		                           error, line.line) ) return false;

		uint32_t funct3 = 0;
		if ( line.op == "slti" ) funct3 = 0x2;
		else if ( line.op == "sltiu" ) funct3 = 0x3;
		else if ( line.op == "xori" ) funct3 = 0x4;
		else if ( line.op == "ori" ) funct3 = 0x6;
		else if ( line.op == "andi" ) funct3 = 0x7;
		*word = encodeIType(immediate, rs1, funct3, rd, 0x13);
		return true;
	}

	if ( line.op == "slli" || line.op == "srli" || line.op == "srai" ) {
		if ( !requireOperandCnt(line, 3, error) ||
		     !parseRegisterOperand(line, 0, &rd, error) ||
		     !parseRegisterOperand(line, 1, &rs1, error) ||
		     !parseSignedImmediate(line.operands[2], 0, 31, &immediate,
		                           error, line.line) ) return false;

		uint32_t funct3 = line.op == "slli" ? 0x1 : 0x5;
		int32_t encodedImmediate = immediate;
		if ( line.op == "srai" ) encodedImmediate |= 0x400;
		*word = encodeIType(encodedImmediate, rs1, funct3, rd, 0x13);
		return true;
	}

	if ( line.op == "add" || line.op == "sub" || line.op == "sll" ||
	     line.op == "slt" || line.op == "sltu" || line.op == "xor" ||
	     line.op == "srl" || line.op == "sra" || line.op == "or" || line.op == "and" ) {
		if ( !requireOperandCnt(line, 3, error) ||
		     !parseRegisterOperand(line, 0, &rd, error) ||
		     !parseRegisterOperand(line, 1, &rs1, error) ||
		     !parseRegisterOperand(line, 2, &rs2, error) ) return false;

		uint32_t funct3 = 0;
		uint32_t funct7 = 0;
		if ( line.op == "sub" ) funct7 = 0x20;
		else if ( line.op == "sll" ) funct3 = 0x1;
		else if ( line.op == "slt" ) funct3 = 0x2;
		else if ( line.op == "sltu" ) funct3 = 0x3;
		else if ( line.op == "xor" ) funct3 = 0x4;
		else if ( line.op == "srl" ) funct3 = 0x5;
		else if ( line.op == "sra" ) { funct3 = 0x5; funct7 = 0x20; }
		else if ( line.op == "or" ) funct3 = 0x6;
		else if ( line.op == "and" ) funct3 = 0x7;
		*word = encodeRType(funct7, rs2, rs1, funct3, rd, 0x33);
		return true;
	}

	if ( line.op == "fence" ) {
		if ( !requireOperandCnt(line, 0, error) ) return false;
		*word = 0x0ff0000f;
		return true;
	}
	if ( line.op == "ecall" || line.op == "ebreak" ) {
		if ( !requireOperandCnt(line, 0, error) ) return false;
		*word = line.op == "ecall" ? 0x00000073 : 0x00100073;
		return true;
	}

	return setFormattedError(error, line.line, "Unknown instruction: %s", line.op.c_str());
}

static bool encodePseudoInstruction(
	const AssemblyLine &line,
	const map<string, uint32_t> &labels,
	vector<uint32_t> *words,
	AssemblerError *error
) {
	uint32_t rd = 0;
	uint32_t rs1 = 0;
	uint32_t rs2 = 0;
	uint32_t rawValue = 0;
	int64_t target = 0;

	if ( line.op == "li" ) {
		if ( !requireOperandCnt(line, 2, error) ||
		     !parseRegisterOperand(line, 0, &rd, error) ||
		     !parseWordValue(line.operands[1], &rawValue) ) {
			if ( error != NULL && error->message[0] == '\0' ) {
				setError(error, line.line, "Malformed li immediate");
			}
			return false;
		}

		int32_t signedValue = bitCastSigned32(rawValue);
		if ( signedValue >= -2048 && signedValue <= 2047 ) {
			words->push_back(encodeIType(signedValue, 0, 0x0, rd, 0x13));
		} else {
			int32_t upper = 0;
			int32_t lower = 0;
			splitUpperLowerImmediate(signedValue, &upper, &lower);
			words->push_back(encodeUType(upper, rd, 0x37));
			words->push_back(encodeIType(lower, rd, 0x0, rd, 0x13));
		}
		return true;
	}

	if ( line.op == "la" || line.op == "lla" ) {
		if ( !requireOperandCnt(line, 2, error) ||
		     !parseRegisterOperand(line, 0, &rd, error) ||
		     !resolveLabelOrImmediate(line.operands[1], labels, &target, true,
		                              line.address, error, line.line) ) return false;

		int64_t offset = target - (int64_t)line.address;
		if ( offset < INT32_MIN || offset > INT32_MAX ) {
			return setError(error, line.line, "Address target is out of range");
		}
		int32_t upper = 0;
		int32_t lower = 0;
		splitUpperLowerImmediate(offset, &upper, &lower);
		words->push_back(encodeUType(upper, rd, 0x17));
		words->push_back(encodeIType(lower, rd, 0x0, rd, 0x13));
		return true;
	}

	if ( line.op == "nop" ) {
		if ( !requireOperandCnt(line, 0, error) ) return false;
		words->push_back(encodeIType(0, 0, 0x0, 0, 0x13));
		return true;
	}
	if ( line.op == "ret" ) {
		if ( !requireOperandCnt(line, 0, error) ) return false;
		words->push_back(encodeIType(0, 1, 0x0, 0, 0x67));
		return true;
	}
	if ( line.op == "jr" ) {
		if ( !requireOperandCnt(line, 1, error) ||
		     !parseRegisterOperand(line, 0, &rs1, error) ) return false;
		words->push_back(encodeIType(0, rs1, 0x0, 0, 0x67));
		return true;
	}
	if ( line.op == "j" || line.op == "call" ) {
		if ( !requireOperandCnt(line, 1, error) ||
		     !resolveLabelOrImmediate(line.operands[0], labels, &target, false,
		                              line.address, error, line.line) ) return false;
		if ( !checkJumpOffset(target, error, line.line) ) return false;
		words->push_back(encodeJType((int32_t)target, line.op == "call" ? 1 : 0, 0x6f));
		return true;
	}
	if ( line.op == "mv" ) {
		if ( !requireOperandCnt(line, 2, error) ||
		     !parseRegisterOperand(line, 0, &rd, error) ||
		     !parseRegisterOperand(line, 1, &rs1, error) ) return false;
		words->push_back(encodeIType(0, rs1, 0x0, rd, 0x13));
		return true;
	}
	if ( line.op == "bnez" || line.op == "beqz" ) {
		if ( !requireOperandCnt(line, 2, error) ||
		     !parseRegisterOperand(line, 0, &rs1, error) ||
		     !resolveLabelOrImmediate(line.operands[1], labels, &target, false,
		                              line.address, error, line.line) ) return false;
		if ( !checkBranchOffset(target, error, line.line) ) return false;
		words->push_back(encodeBType((int32_t)target, 0, rs1,
		                             line.op == "bnez" ? 0x1 : 0x0, 0x63));
		return true;
	}
	if ( line.op == "bgt" || line.op == "ble" ) {
		if ( !requireOperandCnt(line, 3, error) ||
		     !parseRegisterOperand(line, 0, &rs1, error) ||
		     !parseRegisterOperand(line, 1, &rs2, error) ||
		     !resolveLabelOrImmediate(line.operands[2], labels, &target, false,
		                              line.address, error, line.line) ) return false;
		if ( !checkBranchOffset(target, error, line.line) ) return false;
		words->push_back(encodeBType((int32_t)target, rs1, rs2,
		                             line.op == "bgt" ? 0x4 : 0x5, 0x63));
		return true;
	}
	if ( line.op == "hcf" ) {
		if ( !requireOperandCnt(line, 0, error) ) return false;
		words->push_back(0x00100073);
		return true;
	}

	return setFormattedError(error, line.line, "Unknown pseudo-instruction: %s", line.op.c_str());
}

static bool encodeInstructionLine(
	const AssemblyLine &line,
	const map<string, uint32_t> &labels,
	vector<uint32_t> *words,
	AssemblerError *error
) {
	words->clear();
	if ( isRealInstruction(line.op) ) {
		uint32_t word = 0;
		if ( !encodeRealInstruction(line, labels, &word, error) ) return false;
		words->push_back(word);
		return true;
	}
	return encodePseudoInstruction(line, labels, words, error);
}

static bool writeDataLine(
	const AssemblyLine &line,
	uint8_t *memory,
	AssemblerError *error
) {
	if ( line.op == ".zero" ) {
		memset(memory + line.address, 0, line.byteSize);
		return true;
	}

	uint32_t unitSize = line.op == ".byte" ? 1 : (line.op == ".half" ? 2 : 4);
	int64_t minimum = unitSize == 1 ? -128 : (unitSize == 2 ? -32768 : INT32_MIN);
	uint64_t maximum = unitSize == 1 ? 255 : (unitSize == 2 ? 65535 : UINT32_MAX);

	for ( size_t i = 0; i < line.operands.size(); i ++ ) {
		int64_t value = 0;
		if ( !parseInteger(line.operands[i], &value) || value < minimum ||
		     (value >= 0 && (uint64_t)value > maximum) ) {
			return setError(error, line.line, "Data value is outside the directive range");
		}
		writeValueLE(memory, line.address + ((uint32_t)i * unitSize),
		             (uint32_t)value, unitSize);
	}
	return true;
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

static void writeSourceMap(
	const AssemblyLine &line,
	uint32_t address,
	AssemblerSourceEntry *sourceMap,
	uint32_t sourceMapCnt
) {
	if ( sourceMap == NULL ) return;
	uint32_t index = address / 4;
	if ( index >= sourceMapCnt ) return;

	sourceMap[index].line = line.line;
	snprintf(sourceMap[index].text, sizeof(sourceMap[index].text),
	         "%s", line.sourceText.c_str());
}


bool assembleRV32I(
	const char *filename,
	uint8_t *memory,
	uint32_t memorySize,
	uint32_t textOffset,
	uint32_t dataOffset,
	AssemblerResult *result,
	AssemblerError *error,
	AssemblerSourceEntry *sourceMap,
	uint32_t sourceMapCnt
) {
	clearError(error);
	if ( filename == NULL || memory == NULL || result == NULL ) {
		return setError(error, 0, "Invalid assembler argument");
	}
	if ( textOffset > dataOffset || dataOffset > memorySize ) {
		return setError(error, 0, "Invalid assembler memory layout");
	}

	memset(memory, 0, memorySize);
	initializeSourceMap(sourceMap, sourceMapCnt);

	AssemblyProgram program;
	if ( !parseSource(filename, memorySize, textOffset, dataOffset, &program, error) ) return false;

	uint32_t writtenByteCnt = 0;
	for ( size_t i = 0; i < program.lines.size(); i ++ ) {
		const AssemblyLine &line = program.lines[i];
		if ( line.type == ASSEMBLY_LINE_SECTION || line.type == ASSEMBLY_LINE_NONE ) continue;

		if ( line.type == ASSEMBLY_LINE_DATA ) {
			if ( !writeDataLine(line, memory, error) ) return false;
			writtenByteCnt += line.byteSize;
			continue;
		}

		vector<uint32_t> words;
		if ( !encodeInstructionLine(line, program.labels, &words, error) ) return false;
		if ( words.size() * 4 != line.byteSize ) {
			return setError(error, line.line, "Internal instruction-size mismatch");
		}

		for ( size_t wordIdx = 0; wordIdx < words.size(); wordIdx ++ ) {
			uint32_t address = line.address + ((uint32_t)wordIdx * 4);
			DecodedInstr decoded;
			if ( !decodeRV32I(words[wordIdx], &decoded) ) {
				return setError(error, line.line, "Assembler generated an invalid RV32I word");
			}
			writeValueLE(memory, address, words[wordIdx], 4);
			writeSourceMap(line, address, sourceMap, sourceMapCnt);
		}
		writtenByteCnt += line.byteSize;
	}

	result->entryAddr = program.entryAddr;
	result->textEnd = program.textEnd;
	result->dataEnd = program.dataEnd;
	result->instructionCnt = program.instructionCnt;
	result->writtenByteCnt = writtenByteCnt;
	return true;
}
