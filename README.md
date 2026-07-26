# SIRVE
* **SATA Interactive RISC-V Emulator — a compact RV32I machine-code emulator, assembler, and debugger developed by SATA Lab.**
* `SIRVE` assembles RV32I source into real 32-bit instruction words, stores them in emulated memory, and executes them through a fetch-decode-execute engine.

## File structure
```text
sirve/
├── src/
│   ├── sirve.cpp          CLI and interactive debugger
│   ├── assembler.cpp/.h   two-pass parsing, label resolution, and RV32I encoding
│   ├── rv32i.cpp/.h       machine-code decode and instruction execution
│   ├── memory.cpp/.h      instruction fetch, data access, and MMIO
│   ├── cache.cpp/.h       configurable data-cache model
│   └── linenoise.hpp      interactive command-line input
├── examples/              example RV32I assembly programs
├── tests/                 assembler, decoder, execution, and boundary tests
└── Makefile               build and local validation targets
```

## Prerequisites & Dependencies
* GNU Make and G++ with C++11 support.
* Bash for the regression-test script.
* No external runtime library is required; `linenoise.hpp` is included.

## How to build and run
* Build: `make`
* Start interactive debugging: `./obj/sirve examples/reduction.s`
* Run continuously: `./obj/sirve examples/reduction.s run`
* Remove generated binaries: `make clean`

## Execution flow
```text
RV32I assembly -> two-pass assembler -> 32-bit machine code in memory
                                      -> fetch -> decode -> execute
```

The execution engine reads each instruction word from memory; it does not execute an intermediate parsed-instruction representation.

## Assembly support
* RV32I integer arithmetic, logical operations, shifts, branches, jumps, upper immediates, loads, stores, `FENCE`, `ECALL`, and `EBREAK`.
* Pseudo-instructions: `li`, `la`, `lla`, `nop`, `ret`, `jr`, `j`, `call`, `mv`, `bnez`, `beqz`, `bgt`, `ble`, and `hcf`.
* Directives: `.text`, `.data`, `.byte`, `.half`, `.word`, and `.zero`.
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
| `B<line>` | Remove a source-line breakpoint |
| `l` | List addresses, machine words, decoded instructions, and source lines |
| `q` | Quit the emulator |

## Testing
* Decoder, assembler, execution, and boundary tests: `make test`
* AddressSanitizer validation: `make test-asan`
* UndefinedBehaviorSanitizer validation: `make test-ubsan`
* Complete local validation: `make check`

## Working examples
* `examples/reduction.s`
* `examples/sort.s`
* `examples/graph.s`
* `examples/sudoku.s`

## Notes
* Maintained by Se-Min Lim.
* Raw binary and ELF32 loading are not yet implemented.
* Privileged instructions, CSR instructions, interrupts, virtual memory, and ISA extensions are not currently supported.
