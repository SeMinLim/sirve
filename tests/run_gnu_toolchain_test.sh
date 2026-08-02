#!/usr/bin/env bash

set -euo pipefail

export LC_ALL=C

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EMULATOR="${1:-$ROOT_DIR/obj/sirve}"
ELF_FILE="${2:-$ROOT_DIR/obj/freestanding-rv32i.elf}"
SOURCE_DIR="$ROOT_DIR/tests/freestanding"
TMP_DIR="$(mktemp -d)"

TEST_CNT=0
PASS_CNT=0

cleanup() {
	rm -rf "$TMP_DIR"
}
trap cleanup EXIT

printFailure() {
	local testName="$1"
	local message="$2"
	printf "[TEST %02d] FAIL: %s\n" "$TEST_CNT" "$testName"
	printf "%s\n" "$message"
	exit 1
}

printPass() {
	local testName="$1"
	PASS_CNT=$((PASS_CNT + 1))
	printf "[TEST %02d] PASS: %s\n" "$TEST_CNT" "$testName"
}

findCompiler() {
	if [ -n "${RISCV_GCC:-}" ]; then
		command -v "$RISCV_GCC" 2> /dev/null || true
		return
	fi

	local compiler
	for compiler in riscv64-unknown-elf-gcc riscv-none-elf-gcc riscv32-unknown-elf-gcc; do
		if command -v "$compiler" > /dev/null 2>&1; then
			command -v "$compiler"
			return
		fi
	done
}

if [ ! -x "$EMULATOR" ]; then
	printf "Emulator not found: %s\n" "$EMULATOR"
	exit 1
fi

RISCV_GCC_PATH="$(findCompiler)"
if [ -z "$RISCV_GCC_PATH" ]; then
	printf "GNU RISC-V bare-metal GCC was not found.\n"
	printf "Set RISCV_GCC or install riscv64-unknown-elf-gcc, riscv-none-elf-gcc, or riscv32-unknown-elf-gcc.\n"
	exit 1
fi

TOOL_PREFIX="${RISCV_GCC_PATH%gcc}"
RISCV_READELF_PATH="${RISCV_READELF:-${TOOL_PREFIX}readelf}"
RISCV_OBJDUMP_PATH="${RISCV_OBJDUMP:-${TOOL_PREFIX}objdump}"
if [ ! -x "$RISCV_READELF_PATH" ]; then
	printf "GNU RISC-V readelf was not found: %s\n" "$RISCV_READELF_PATH"
	exit 1
fi
if [ ! -x "$RISCV_OBJDUMP_PATH" ]; then
	printf "GNU RISC-V objdump was not found: %s\n" "$RISCV_OBJDUMP_PATH"
	exit 1
fi

mkdir -p "$(dirname "$ELF_FILE")"

printf "%s\n" "---------------------------------------------------------------------"
printf "%s\n" "[STEP 1] Building a freestanding RV32I C program with GNU GCC."
printf "%s\n" "---------------------------------------------------------------------"

"$RISCV_GCC_PATH" \
	-std=c11 \
	-march=rv32i \
	-mabi=ilp32 \
	-mno-relax \
	-msmall-data-limit=0 \
	-Os \
	-Wall \
	-Wextra \
	-Wpedantic \
	-ffreestanding \
	-fno-builtin \
	-fno-common \
	-fno-pic \
	-fno-pie \
	-fno-stack-protector \
	-nostdlib \
	-nostartfiles \
	-no-pie \
	-Wl,--build-id=none \
	-Wl,--no-relax \
	-Wl,-T,"$SOURCE_DIR/link.ld" \
	"$SOURCE_DIR/crt0.S" \
	"$SOURCE_DIR/main.c" \
	-o "$ELF_FILE"

TEST_CNT=$((TEST_CNT + 1))
if [ ! -s "$ELF_FILE" ]; then
	printFailure "Build GNU RV32I ELF32 executable" "The compiler did not produce an ELF file."
fi
printPass "Build GNU RV32I ELF32 executable"

printf "%s\n" "---------------------------------------------------------------------"
printf "%s\n" "[STEP 2] Validating the GNU-generated ELF32 executable."
printf "%s\n" "---------------------------------------------------------------------"

HEADER_FILE="$TMP_DIR/header.txt"
PROGRAM_FILE="$TMP_DIR/program.txt"
DISASSEMBLY_FILE="$TMP_DIR/disassembly.txt"
"$RISCV_READELF_PATH" -h "$ELF_FILE" > "$HEADER_FILE"
"$RISCV_READELF_PATH" -l "$ELF_FILE" > "$PROGRAM_FILE"
"$RISCV_OBJDUMP_PATH" -d "$ELF_FILE" > "$DISASSEMBLY_FILE"

TEST_CNT=$((TEST_CNT + 1))
if ! grep -Eq 'Class:[[:space:]]+ELF32' "$HEADER_FILE" ||
   ! grep -Eq 'Data:[[:space:]]+2.s complement, little endian' "$HEADER_FILE" ||
   ! grep -Eq 'Type:[[:space:]]+EXEC' "$HEADER_FILE" ||
   ! grep -Eq 'Machine:[[:space:]]+RISC-V' "$HEADER_FILE" ||
   ! grep -Eq 'Entry point address:[[:space:]]+0x1000' "$HEADER_FILE"; then
	printFailure "Validate RV32I ELF32 header" "The generated ELF header is not the expected static RV32I executable."
fi
if grep -Eq 'Flags:.*RVC' "$HEADER_FILE"; then
	printFailure "Validate RV32I ELF32 header" "The generated ELF enables compressed instructions."
fi
printPass "Validate RV32I ELF32 header"

TEST_CNT=$((TEST_CNT + 1))
LOAD_CNT="$(grep -Ec '^[[:space:]]*LOAD[[:space:]]' "$PROGRAM_FILE" || true)"
if [ "$LOAD_CNT" -ne 2 ] || grep -Eq '^[[:space:]]*(INTERP|DYNAMIC|TLS)[[:space:]]' "$PROGRAM_FILE"; then
	printFailure "Validate static PT_LOAD layout" "The generated ELF does not contain the expected static text and data segments."
fi
if grep -Eq '^[[:space:]]*[0-9a-f]+:[[:space:]]+[0-9a-f]{4}[[:space:]]' "$DISASSEMBLY_FILE"; then
	printFailure "Validate static PT_LOAD layout" "The generated ELF contains a 16-bit instruction encoding."
fi
printPass "Validate static PT_LOAD layout"

printf "%s\n" "---------------------------------------------------------------------"
printf "%s\n" "[STEP 3] Executing the GNU-generated ELF32 executable in SIRVE."
printf "%s\n" "---------------------------------------------------------------------"

OUTPUT_FILE="$TMP_DIR/sirve.txt"
TEST_CNT=$((TEST_CNT + 1))
if command -v timeout > /dev/null 2>&1; then
	if ! timeout 10 "$EMULATOR" --elf "$ELF_FILE" run > "$OUTPUT_FILE" 2>&1; then
		printFailure "Execute GNU-generated RV32I ELF32 executable" "$(cat "$OUTPUT_FILE")"
	fi
else
	if ! "$EMULATOR" --elf "$ELF_FILE" run > "$OUTPUT_FILE" 2>&1; then
		printFailure "Execute GNU-generated RV32I ELF32 executable" "$(cat "$OUTPUT_FILE")"
	fi
fi

for expectedText in \
	"Loading ELF32 executable" \
	"[System output]: 0x36" \
	"x02:0x00010000" \
	"x10:0x00000036" \
	"Reached Halt and Catch Fire instruction!"; do
	if ! grep -Fq "$expectedText" "$OUTPUT_FILE"; then
		printf "Missing expected output: %s\n" "$expectedText"
		printFailure "Execute GNU-generated RV32I ELF32 executable" "$(cat "$OUTPUT_FILE")"
	fi
done
printPass "Execute GNU-generated RV32I ELF32 executable"

printf "%s\n" "---------------------------------------------------------------------"
printf "[SUMMARY] %d/%d GNU toolchain tests passed.\n" "$PASS_CNT" "$TEST_CNT"
printf "%s\n" "---------------------------------------------------------------------"
