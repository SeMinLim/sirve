# SIRVE
* **SATA Interactive RISC-V Emulator — a compact RV32I machine-code emulator, assembler, loader, and debugger developed by SATA Lab.**
* `SIRVE` executes real 32-bit RV32I instruction words produced by its two-pass assembler or loaded directly from a raw binary.

## File structure
```text
sirve/
├── src/
│   ├── sirve.cpp          CLI and interactive debugger
│   ├── assembler.cpp/.h   two-pass parsing, label resolution, and RV32I encoding
│   ├── loader.cpp/.h      raw binary loading and validation
│   ├── rv32i.cpp/.h       machine-code decode and instruction execution
│   ├── memory.cpp/.h      instruction fetch, data access, and MMIO
│   ├── cache.cpp/.h       configurable data-cache model
│   └── linenoise.hpp      interactive command-line input
├── examples/              example RV32I assembly programs
├── tests/                 assembler, decoder, loader, execution, and boundary tests
└── Makefile               build and local validation targets
```

## Prerequisites & Dependencies
* GNU Make and G++ with C++11 support.
* Bash for the regression-test script.
* No external runtime library is required; `linenoise.hpp` is included.

## How to build and run
* Build: `make`
* Debug assembly: `./obj/sirve --asm examples/reduction.s`
* Run assembly continuously: `./obj/sirve --asm examples/reduction.s run`
* Run a raw binary: `./obj/sirve --bin program.bin --load 0x0 --entry 0x0 run`
* Remove generated binaries: `make clean`

The legacy assembly form `./obj/sirve examples/reduction.s [run]` remains supported. Raw binaries are copied to `--load`; execution starts at `--entry`, which defaults to the load address.

## Execution flow
```text
RV32I assembly -> two-pass assembler ---┐
                                        ├-> machine code in memory -> fetch -> decode -> execute
Raw binary -----------------------------┘
```

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
| `ba<address>` | Add an address breakpoint, including for raw binaries |
| `B<line>`, `Ba<address>` | Remove a source-line or address breakpoint |
| `l` | List addresses, machine words, decoded instructions, and source lines |
| `q` | Quit the emulator |

## Testing
* Decoder, assembler, loader, execution, and boundary tests: `make test`
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
* ELF32 loading is not yet implemented.
* Privileged instructions, CSR instructions, interrupts, virtual memory, and ISA extensions are not currently supported.
