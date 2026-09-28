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

## Memory map

SIRVE provides 64 KiB of RAM at `0x00000000`–`0x0000FFFF`. Section placement depends on the input format.

| Section / region | Start address | Placement |
|---|---|---|
| `.text` (`--asm`) | `0x00000000` | Built-in assembler instruction region, through `0x00007FFF` |
| `.data` (`--asm`) | `0x00008000` | Built-in assembler data region, through `0x0000FFFF`; shares RAM with any program-managed stack or heap |
| `.rodata`, `.bss` (`--asm`) | No separate region | These section directives are unsupported; `.zero` can reserve zero-filled bytes in `.data` |
| `.text`, `.rodata`, `.data`, `.bss` (`--elf`) | Defined by the ELF linker script | The loader places `PT_LOAD` segments at their `p_vaddr` addresses and zero-fills `p_memsz - p_filesz` bytes, including `.bss` |
| Raw image (`--bin`) | `--load`, default `0x00000000` | Loaded as one image without section metadata |
| Stack | Program-defined; ELF initial `sp` is `0x00010000` | No separate region is reserved. Assembly/raw inputs start with `sp = 0`; the supplied assembly examples set it to `0x00010000` and grow the stack downward |
| Heap | Program-defined | No fixed start address or built-in allocator |
| System output MMIO | `0x00010000` | Outside RAM; writes print `[System output]`, while reads are out of bounds |

`0x00010000` as a stack-top value is one byte past RAM; stack storage must be allocated below it. The fixed `.text` and `.data` addresses above apply only to the built-in assembler, not ELF or raw input.

## Architectural trace
`--trace` emits the PC, raw instruction, next PC, status, register write, load address, and store address/size/value for each instruction. `--max-instructions` provides deterministic bounded execution.

## Debugger commands
| Command | Operation |
|---|---|
| `Enter`, `s`, `s<N>` | Execute one or `N` instructions, then print all 32 registers |
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
