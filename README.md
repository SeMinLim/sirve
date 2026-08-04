# SIRVE
* **SATA Interactive RISC-V Emulator — a compact RV32I machine-code emulator, assembler, loader, and debugger developed by SATA Lab.**
* `SIRVE` executes RV32I assembly, raw binaries, and static ELF32 executables through one fetch-decode-execute engine.

## File structure
```text
sirve/
├── src/          assembler, loader, RV32I engine, memory, cache, CLI/debugger
├── examples/     example RV32I assembly programs
├── tests/        regression, toolchain, and Spike differential tests
└── Makefile      build and local validation targets
```

## Prerequisites
* GNU Make and G++ with C++11 support.
* GNU RISC-V bare-metal GCC for `make test-toolchain`.
* Spike, Python 3, and GNU RISC-V GCC or Clang for `make test-spike`.

## Build and run
```bash
make
./obj/sirve --asm examples/reduction.s
./obj/sirve --asm examples/reduction.s run
./obj/sirve --bin program.bin --load 0x0 --entry 0x0 run
./obj/sirve --elf program.elf run
./obj/sirve --elf program.elf --trace --max-instructions 100 run
```

The legacy form `./obj/sirve examples/reduction.s [run]` remains supported. `hcf` is encoded as the standard RV32I `EBREAK` instruction.

## Architectural trace
`--trace` emits the PC, raw instruction, next PC, status, register write, load address, and store address/size/value for each instruction. `--max-instructions` provides deterministic bounded execution.

## Debugger commands
| Command | Operation |
|---|---|
| `Enter`, `s`, `s<N>` | Execute one or `N` instructions |
| `c` | Continue until termination or a breakpoint |
| `r`, `r<register>` | Print registers |
| `m<address> [count]` | Print memory words |
| `b`, `b<line>`, `ba<address>` | List or add breakpoints |
| `B<line>`, `Ba<address>` | Remove breakpoints |
| `l` | List machine words, disassembly, and source lines |
| `q` | Quit |

## Testing
```bash
make test
make test-toolchain
make test-spike
make test-asan
make test-ubsan
make check
```

`make test-spike` compares the ordered SIRVE and Spike commit traces, including PC, raw instruction, integer register writes, loads, and stores. `make check` excludes this optional external comparison.

## Notes
* Maintained by Se-Min Lim.
* Dynamic ELF, relocatable objects, shared libraries, privileged execution, virtual memory, and ISA extensions are not supported.
