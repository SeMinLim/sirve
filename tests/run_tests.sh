#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EMULATOR="${1:-$ROOT_DIR/obj/sirve}"
ASM_DIR="$ROOT_DIR/tests/asm"
TMP_DIR="$(mktemp -d)"

TEST_CNT=0
PASS_CNT=0

cleanup() {
	rm -rf "$TMP_DIR"
}
trap cleanup EXIT

if [ ! -x "$EMULATOR" ]; then
	printf "Emulator not found: %s\n" "$EMULATOR"
	exit 1
fi

runEmulator() {
	if command -v timeout > /dev/null 2>&1; then
		timeout 10 "$@"
	else
		"$@"
	fi
}

printPass() {
	local testName="$1"
	PASS_CNT=$((PASS_CNT + 1))
	printf "[TEST %02d] PASS: %s\n" "$TEST_CNT" "$testName"
}

printFailure() {
	local testName="$1"
	local outputFile="$2"
	printf "[TEST %02d] FAIL: %s\n" "$TEST_CNT" "$testName"
	printf '%s\n' "---------------------------------------------------------------------"
	cat "$outputFile"
	printf '%s\n' "---------------------------------------------------------------------"
	exit 1
}

runSuccessTest() {
	local testName="$1"
	local assemblyFile="$2"
	shift 2
	local outputFile="$TMP_DIR/test-$TEST_CNT.log"

	TEST_CNT=$((TEST_CNT + 1))
	if ! runEmulator "$EMULATOR" "$assemblyFile" run > "$outputFile" 2>&1; then
		printFailure "$testName" "$outputFile"
	fi

	for expectedText in "$@"; do
		if ! grep -Fq "$expectedText" "$outputFile"; then
			printf "Missing expected output: %s\n" "$expectedText"
			printFailure "$testName" "$outputFile"
		fi
	done

	printPass "$testName"
}

runFailureTest() {
	local testName="$1"
	local assemblyFile="$2"
	local expectedText="$3"
	local outputFile="$TMP_DIR/test-$TEST_CNT.log"

	TEST_CNT=$((TEST_CNT + 1))
	if runEmulator "$EMULATOR" "$assemblyFile" run > "$outputFile" 2>&1; then
		printFailure "$testName" "$outputFile"
	fi
	if ! grep -Fq "$expectedText" "$outputFile"; then
		printf "Missing expected error: %s\n" "$expectedText"
		printFailure "$testName" "$outputFile"
	fi

	printPass "$testName"
}

runRawBinaryTest() {
	local testName="$1"
	local binaryFile="$2"
	shift 2
	local outputFile="$TMP_DIR/test-$TEST_CNT.log"

	TEST_CNT=$((TEST_CNT + 1))
	if ! runEmulator "$EMULATOR" --bin "$binaryFile" \
		--load 0x100 --entry 0x100 run > "$outputFile" 2>&1; then
		printFailure "$testName" "$outputFile"
	fi

	for expectedText in "$@"; do
		if ! grep -Fq "$expectedText" "$outputFile"; then
			printf "Missing expected output: %s\n" "$expectedText"
			printFailure "$testName" "$outputFile"
		fi
	done

	printPass "$testName"
}

runElf32Test() {
	local testName="$1"
	local elfFile="$2"
	shift 2
	local outputFile="$TMP_DIR/test-$TEST_CNT.log"

	TEST_CNT=$((TEST_CNT + 1))
	if ! runEmulator "$EMULATOR" --elf "$elfFile" run > "$outputFile" 2>&1; then
		printFailure "$testName" "$outputFile"
	fi

	for expectedText in "$@"; do
		if ! grep -Fq "$expectedText" "$outputFile"; then
			printf "Missing expected output: %s\n" "$expectedText"
			printFailure "$testName" "$outputFile"
		fi
	done

	printPass "$testName"
}

printf '%s\n' "---------------------------------------------------------------------"
printf '%s\n' "[STEP 1] Running RV32I functional regression tests."
printf '%s\n' "---------------------------------------------------------------------"

runSuccessTest "RV32I arithmetic, shifts, loads, and stores" \
	"$ASM_DIR/rv32i_core.s" \
	"x00:0x00000000" \
	"x01:0x000000ff" \
	"x02:0xffffffff" \
	"x03:0x0000ffff" \
	"x07:0x00000000" \
	"x08:0xffffffff" \
	"x09:0x00000001" \
	"x10:0x00000000" \
	"x11:0xfffffffe" \
	"x12:0x80000001" \
	"x13:0x80000000" \
	"x14:0x80000000" \
	"x15:0x00000001" \
	"x16:0xffffffff" \
	"x17:0x00000001" \
	"x18:0xfffff800" \
	"x19:0x00000001" \
	"x20:0x00000000" \
	"x21:0xffffff00" \
	"x22:0x000007ff" \
	"x23:0xfffff800" \
	"x24:0x80000000" \
	"x25:0x00000001" \
	"x26:0xffffffff" \
	"x27:0xfffff000" \
	"x28:0x0000106c" \
	"x29:0x00001000" \
	"x30:0xfffff000" \
	"x31:0xffffffff"

runSuccessTest "RV32I branches, JAL, and JALR" \
	"$ASM_DIR/control_flow.s" \
	"x10:0x00000000" \
	"x12:0x00000055" \
	"x13:0x00000066"

runSuccessTest "JALR source and destination overlap" \
	"$ASM_DIR/jalr_overlap.s" \
	"x01:0x00000008" \
	"x02:0x00000000" \
	"x03:0x00000007"

runSuccessTest "Aligned accesses at the memory boundary" \
	"$ASM_DIR/memory_boundary.s" \
	"x01:0x0000fffe" \
	"x02:0x12345678" \
	"x03:0x12345678" \
	"x04:0x00000078" \
	"x05:0x00005678"

runSuccessTest "Exact data-segment boundary" \
	"$ASM_DIR/data_boundary.s" \
	"Reached Halt and Catch Fire instruction!"

RAW_BINARY_FILE="$TMP_DIR/raw-rv32i.bin"
printf '\x93\x00\x70\x00\x73\x00\x10\x00' > "$RAW_BINARY_FILE"
runRawBinaryTest "Execute raw RV32I binary" \
	"$RAW_BINARY_FILE" \
	"x01:0x00000007" \
	"Reached Halt and Catch Fire instruction!"

runElf32Test "Execute RV32I ELF32 executable" \
	"$ROOT_DIR/obj/test-rv32i.elf" \
	"Loading ELF32 executable" \
	"x01:0x00000007" \
	"x02:0x00010000" \
	"x03:0x12345678" \
	"x04:0x00000000" \
	"Reached Halt and Catch Fire instruction!"

printf '%s\n' "---------------------------------------------------------------------"
printf '%s\n' "[STEP 2] Running input and execution boundary tests."
printf '%s\n' "---------------------------------------------------------------------"

runFailureTest "Reject x32" \
	"$ASM_DIR/invalid_register.s" \
	"Malformed register name"

runFailureTest "Reject malformed register token" \
	"$ASM_DIR/invalid_register_token.s" \
	"Malformed register name"

runFailureTest "Reject misaligned word load" \
	"$ASM_DIR/misaligned_load.s" \
	"Misaligned memory read at address 0x00000001"

runFailureTest "Reject misaligned word store" \
	"$ASM_DIR/misaligned_store.s" \
	"Misaligned memory write at address 0x00000001"

runFailureTest "Reject misaligned instruction address" \
	"$ASM_DIR/misaligned_pc.s" \
	"Instruction address misaligned: 0x00000002"

runFailureTest "Reject instruction address outside text segment" \
	"$ASM_DIR/out_of_bounds_pc.s" \
	"Instruction address out of bounds: 0x00008000"

runFailureTest "Reject memory read outside address space" \
	"$ASM_DIR/out_of_bounds_read.s" \
	"Memory read out of bounds at address 0x00010000"

runFailureTest "Reject memory write outside address space" \
	"$ASM_DIR/out_of_bounds_write.s" \
	"Memory write out of bounds at address 0x00010004"

runFailureTest "Reject data-segment overflow" \
	"$ASM_DIR/data_overflow.s" \
	"Data segment out of bounds"

TEXT_BOUNDARY_FILE="$TMP_DIR/text-boundary.s"
TEXT_OVERFLOW_FILE="$TMP_DIR/text-overflow.s"
for ((i = 0; i < 8192; i ++)); do
	printf "hcf\n"
done > "$TEXT_BOUNDARY_FILE"
for ((i = 0; i < 8193; i ++)); do
	printf "hcf\n"
done > "$TEXT_OVERFLOW_FILE"

runSuccessTest "Exact text-segment boundary" \
	"$TEXT_BOUNDARY_FILE" \
	"Reached Halt and Catch Fire instruction!"

runFailureTest "Reject text-segment overflow" \
	"$TEXT_OVERFLOW_FILE" \
	"Instructions exceed the text segment!"

printf '%s\n' "---------------------------------------------------------------------"
printf "[SUMMARY] %d/%d tests passed.\n" "$PASS_CNT" "$TEST_CNT"
printf '%s\n' "---------------------------------------------------------------------"
