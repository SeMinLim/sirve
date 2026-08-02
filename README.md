# SIRVE
* **SATA Interactive RISC-V Emulator — a compact RV32I machine-code emulator, assembler, loader, and debugger developed by SATA Lab.**
* `SIRVE` executes real 32-bit RV32I instruction words produced by its assembler, loaded from a raw binary, or loaded from a static ELF32 executable.

## File structure
```text
sirve/
├── src/
│   ├── sirve.cpp          CLI and interactive debugger
│   ├── assembler.cpp/.h   two-pass parsing, label resolution, and RV32I encoding
│   ├── loader.cpp/.h      raw binary and ELF32 loading
│   ├── rv32i.cpp/.h       machine-code decode and instruction execution
│   ├── memory.cpp/.h      instruction fetch, data access, and MMIO
│   ├── cache.cpp/.h       configurable data-cache model
│   └── linenoise.hpp      interactive command-line input
├── examples/              example RV32I assembly programs
├── tests/                 regression tests and a freestanding RV32I C program
└── Makefile               build and local validation targets
```

## Prerequisites & Dependencies
* GNU Make and G++ with C++11 support.
* Bash for the regression-test scripts.
* GNU RISC-V bare-metal GCC for the freestanding C/ELF32 test. Supported compiler names are `riscv64-unknown-elf-gcc`, `riscv-none-elf-gcc`, and `riscv32-unknown-elf-gcc`.
* No external runtime library is required; `linenoise.hpp` is included.

## How to build and run
* Build: `make`
* Debug assembly: `./obj/sirve --asm examples/reduction.s`
* Run assembly continuously: `./obj/sirve --asm examples/reduction.s run`
* Run a raw binary: `./obj/sirve --bin program.bin --load 0x0 --entry 0x0 run`
* Run an ELF32 executable: `./obj/sirve --elf program.elf run`
* Build and execute the freestanding C test: `make test-toolchain`
* Remove generated binaries: `make clean`

The legacy assembly form `./obj/sirve examples/reduction.s [run]` remains supported. Raw binaries use `--load` and `--entry`; ELF32 executables obtain their load addresses and entry point from the ELF program headers.

## Execution flow
```text
RV32I assembly -> two-pass assembler ---┐
Raw binary -----------------------------├-> machine code in memory -> fetch -> decode -> execute
ELF32 executable -> PT_LOAD segments ---┘
```

## Input support
* Assembly: RV32I instructions, common pseudo-instructions, and `.text`, `.data`, `.byte`, `.half`, `.word`, and `.zero` directives.
* Raw binary: explicit load and entry addresses with bounds and alignment validation.
* ELF32: little-endian static RISC-V executables, contiguous executable `PT_LOAD` regions, `.bss` zero initialization, and `e_entry` execution.
* ELF32 mode initializes `sp` to `0x10000`, the top of the current 64-KB memory space.
* `hcf` is encoded as the standard RV32I `EBREAK` instruction.

Operands may be separated by whitespace or commas. Comments begin with `#`.

## Debugger commands
| Command | Operation |
|---|---|
| `Enter`, `s`, `s<N>` | Execute one or `N` instructions |
| `c` | Continue until termination or a breakpoint |
| `r`, `r<register>` | Print all registers or one register, such as `rx5` or `rra` |
| `m<address> [count]` | Print one or more 4-byte memory words |
| `b`, `b<line>` | List breakpoints or add a source-line breakpoint |
| `ba<address>` | Add an address breakpoint for any input mode |
| `B<line>`, `Ba<address>` | Remove a source-line or address breakpoint |
| `l` | List addresses, machine words, decoded instructions, and source lines |
| `q` | Quit the emulator |

## Testing
* Decoder, assembler, raw-loader, ELF32-loader, execution, and boundary tests: `make test`
* GNU GCC-generated freestanding C/ELF32 test: `make test-toolchain`
* AddressSanitizer validation: `make test-asan`
* UndefinedBehaviorSanitizer validation: `make test-ubsan`
* Complete local validation, including the GNU toolchain test: `make check`

## Working examples
* `examples/reduction.s`
* `examples/sort.s`
* `examples/graph.s`
* `examples/sudoku.s`

## Notes
* Maintained by Se-Min Lim.
* Dynamic ELF, relocatable objects, shared libraries, thread-local storage, privileged instructions, CSR instructions, interrupts, virtual memory, and ISA extensions are not currently supported.
