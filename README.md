# SIRVE
* **SATA Interactive RISC-V Emulator — a compact, interactive RV32I assembly emulator and debugger developed by SATA Lab.**
* `SIRVE` parses assembly source directly, expands common pseudo-instructions, models registers, memory, and cache behavior, and exposes the execution state through a command-line debugger.

## File structure
```text
sirve/
├── src/
│   ├── sirve.cpp       assembly parser, execution engine, and debugger
│   ├── rv32i.cpp/.h    RV32I machine-code decoder
│   ├── cache.cpp/.h    configurable cache model
│   └── linenoise.hpp   interactive command-line input
├── examples/           example RV32I assembly programs
├── tests/              decoder, functional, and boundary tests
└── Makefile            build and local validation targets
```

## Prerequisites & Dependencies
* GNU Make and G++ with C++11 support.
* Bash for the regression-test script.
* No external runtime library is required; `linenoise.hpp` is included.

## How to build and run
* Build: `make`
* Start interactive debugging: `./obj/sirve examples/reduction.s`
* Run continuously without the debugger prompt: `./obj/sirve examples/reduction.s run`
* Remove generated binaries: `make clean`

`SIRVE` accepts RV32I assembly source rather than ELF executables or raw machine-code binaries.

## Assembly support
* RV32I integer arithmetic, logical operations, shifts, branches, jumps, upper immediates, and byte/halfword/word loads and stores.
* Pseudo-instructions: `li`, `la`, `lla`, `nop`, `ret`, `jr`, `j`, `call`, `mv`, `bnez`, `beqz`, `bgt`, and `ble`.
* Directives: `.text`, `.data`, `.byte`, `.half`, `.word`, and `.zero`.
* `hcf` is a SIRVE-specific halt instruction that prints the register and cache state.

Operands may be separated by whitespace or commas. Full-line comments begin with `#`.

## Debugger commands
| Command | Operation |
|---|---|
| `Enter`, `s`, `s<N>` | Execute one or `N` instructions |
| `c` | Continue until termination or a breakpoint |
| `r`, `r<register>` | Print all registers or one register, such as `rx5` or `rra` |
| `m<address> [count]` | Print one or more 4-byte memory words |
| `b`, `b<line>` | List breakpoints or add a source-line breakpoint |
| `B<line>` | Remove a source-line breakpoint |
| `l` | List parsed instructions and their source lines |
| `q` | Quit the emulator |

## Testing
* Decoder, functional, and boundary regression tests: `make test`
* AddressSanitizer regression tests: `make test-asan`
* UndefinedBehaviorSanitizer regression tests: `make test-ubsan`
* Complete local validation: `make check`

## Working examples
* `examples/reduction.s`
* `examples/sort.s`
* `examples/graph.s`
* `examples/sudoku.s`

## Notes
* Maintained by Se-Min Lim.
* `FENCE`, `ECALL`, `EBREAK`, privileged instructions, and ISA extensions are not currently implemented.
