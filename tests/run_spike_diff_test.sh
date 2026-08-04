#!/usr/bin/env bash

set -euo pipefail

export LC_ALL=C

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EMULATOR="${1:-$ROOT_DIR/obj/sirve}"
ELF_FILE="${2:-$ROOT_DIR/obj/spike-diff-rv32i.elf}"
SOURCE_DIR="$ROOT_DIR/tests/differential"
COMPARE_SCRIPT="$ROOT_DIR/tests/compare_spike_trace.py"
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

	if command -v clang > /dev/null 2>&1; then
		command -v clang
	fi
}

runWithTimeout() {
	if command -v timeout > /dev/null 2>&1; then
		timeout 20 "$@"
	else
		"$@"
	fi
}

if [ ! -x "$EMULATOR" ]; then
	printf "Emulator not found: %s\n" "$EMULATOR"
	exit 1
fi
if [ ! -x "$COMPARE_SCRIPT" ]; then
	printf "Trace comparator not found: %s\n" "$COMPARE_SCRIPT"
	exit 1
fi

SPIKE_PATH="${SPIKE:-$(command -v spike 2> /dev/null || true)}"
if [ -z "$SPIKE_PATH" ] || [ ! -x "$SPIKE_PATH" ]; then
	printf "Spike was not found. Set SPIKE or install the Spike ISA simulator.\n"
	exit 1
fi
if ! "$SPIKE_PATH" --help 2>&1 | grep -q -- '--log-commits' ||
   ! "$SPIKE_PATH" --help 2>&1 | grep -q -- '--instructions' ||
   ! "$SPIKE_PATH" --help 2>&1 | grep -q -- '--pcs'; then
	printf "Spike does not provide the required --log-commits, --instructions, and --pcs options.\n"
	exit 1
fi

COMPILER_PATH="$(findCompiler)"
if [ -z "$COMPILER_PATH" ]; then
	printf "A RISC-V compiler was not found. Set RISCV_GCC or install GNU RISC-V GCC or Clang.\n"
	exit 1
fi

mkdir -p "$(dirname "$ELF_FILE")"

printf '%s\n' "---------------------------------------------------------------------"
printf '%s\n' "[STEP 1] Building the deterministic RV32I differential-test ELF."
printf '%s\n' "---------------------------------------------------------------------"

if [ "$(basename "$COMPILER_PATH")" = "clang" ]; then
	"$COMPILER_PATH" \
		--target=riscv32-unknown-elf \
		-march=rv32i \
		-mabi=ilp32 \
		-mno-relax \
		-nostdlib \
		-fno-pic \
		-fno-pie \
		-Wl,--build-id=none \
		-Wl,--no-relax \
		-Wl,-T,"$SOURCE_DIR/link.ld" \
		"$SOURCE_DIR/program.S" \
		-o "$ELF_FILE"
else
	"$COMPILER_PATH" \
		-march=rv32i \
		-mabi=ilp32 \
		-mno-relax \
		-nostdlib \
		-nostartfiles \
		-fno-pic \
		-fno-pie \
		-no-pie \
		-Wl,--build-id=none \
		-Wl,--no-relax \
		-Wl,-T,"$SOURCE_DIR/link.ld" \
		"$SOURCE_DIR/program.S" \
		-o "$ELF_FILE"
fi

TEST_CNT=$((TEST_CNT + 1))
if [ ! -s "$ELF_FILE" ]; then
	printFailure "Build differential-test ELF" "The compiler did not produce an ELF file."
fi
printPass "Build differential-test ELF"

printf '%s\n' "---------------------------------------------------------------------"
printf '%s\n' "[STEP 2] Recording SIRVE and Spike architectural traces."
printf '%s\n' "---------------------------------------------------------------------"

SIRVE_LOG="$TMP_DIR/sirve.log"
SPIKE_LOG="$TMP_DIR/spike.log"
SPIKE_STDERR="$TMP_DIR/spike.stderr"

TEST_CNT=$((TEST_CNT + 1))
if ! runWithTimeout "$EMULATOR" --elf "$ELF_FILE" --trace run > "$SIRVE_LOG" 2>&1; then
	printFailure "Record SIRVE trace" "$(cat "$SIRVE_LOG")"
fi
INSTRUCTION_CNT="$(grep -c '^TRACE .* status=ok$' "$SIRVE_LOG" || true)"
if [ "$INSTRUCTION_CNT" -le 0 ] || ! grep -q '^TRACE .* status=ebreak$' "$SIRVE_LOG"; then
	printFailure "Record SIRVE trace" "$(cat "$SIRVE_LOG")"
fi
printPass "Record SIRVE trace"

TEST_CNT=$((TEST_CNT + 1))
set +e
runWithTimeout "$SPIKE_PATH" \
	--isa="${SPIKE_ISA:-RV32I}" \
	--priv=m \
	--disable-dtb \
	-m0x0:0x10000 \
	--pcs=0:0x1000 \
	--instructions="$INSTRUCTION_CNT" \
	--log-commits \
	--log="$SPIKE_LOG" \
	"$ELF_FILE" > /dev/null 2> "$SPIKE_STDERR"
SPIKE_STATUS=$?
set -e
if [ ! -s "$SPIKE_LOG" ]; then
	cp "$SPIKE_STDERR" "$SPIKE_LOG"
fi
if [ "$(grep -c '^core[[:space:]]*0:' "$SPIKE_LOG" || true)" -ne "$INSTRUCTION_CNT" ]; then
	printf "Spike exit status: %d\n" "$SPIKE_STATUS"
	printFailure "Record Spike trace" "$(cat "$SPIKE_STDERR"; cat "$SPIKE_LOG")"
fi
printPass "Record Spike trace"

printf '%s\n' "---------------------------------------------------------------------"
printf '%s\n' "[STEP 3] Comparing retired instructions and architectural effects."
printf '%s\n' "---------------------------------------------------------------------"

TEST_CNT=$((TEST_CNT + 1))
COMPARE_OUTPUT="$TMP_DIR/compare.txt"
if ! python3 "$COMPARE_SCRIPT" "$SIRVE_LOG" "$SPIKE_LOG" > "$COMPARE_OUTPUT" 2>&1; then
	printFailure "Match SIRVE and Spike traces" "$(cat "$COMPARE_OUTPUT")"
fi
cat "$COMPARE_OUTPUT"
printPass "Match SIRVE and Spike traces"

printf '%s\n' "---------------------------------------------------------------------"
printf "[SUMMARY] %d/%d Spike differential tests passed.\n" "$PASS_CNT" "$TEST_CNT"
printf '%s\n' "---------------------------------------------------------------------"
