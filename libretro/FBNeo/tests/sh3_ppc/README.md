# SH3 PowerPC differential tests

Run on Linux with a **32-bit, big-endian** `powerpc-linux-gnu-g++` toolchain and
`qemu-ppc`. The test executes actual generated machine code. It does not need
ROMs or the Xbox SDK.

```sh
python3 tests/sh3_ppc/run.py --output /tmp/fbneo-sh3-ppc
```

`--toolchain-root /path/to/root` also supports locally extracted Debian cross
compiler / QEMU packages. `PPC_CXX` and `QEMU_PPC` override the installed tools.

Coverage:

- Every translated opcode encoding in five address / alias modes, repeated with
  the verified direct-RAM path enabled.
- Random mixed blocks, register eviction, branches, delay slots and short budgets.
- Compiled delay slots with branch-register, PR and T dependencies, including
  memory guards that exit after the branch has committed.
- Conditional fallthrough, adjacent memory guards and maximum exit-cycle budgets.
- Division, carry rotation, signed boundaries and overlapping operands.
- Loops retained in generated code, watched RAM reads and device fallback.
- Writes to instruction pages, direct instruction changes, fetch remapping,
  guest / host page boundaries, cache recycling, reset and state-load invalidation.
- Exact code-write ranges: byte/word/longword overlaps at both ends, aliases,
  nearby data on the same page and protection of the DT lookahead opcode.
- Direct-RAM registration, changed literal data, every external alias, internal
  address rejection, mirrored backing RAM, watched reads and mapping/handler
  changes that must revoke embedded pointers.

The oracle is the existing interpreter in `sh4.cpp`. It compares the complete
shared CPU register file, consumed cycles, RAM and device callback counts.
Unsupported instructions and guarded accesses execute through the interpreter.
The test exits on the first mismatch and saves `failed-code.bin` for disassembly.
Random addresses use synthetic device callbacks throughout the address space;
real timer/DMA/internal-register state is outside the instruction-level snapshot
and is exercised by the complete game regression instead.

The separate CV1000 game regression uses private, user-supplied ROMs. ROMs and
save states must not be added to this directory. Xbox frame rates require console
measurements; QEMU timing is not an Xbox performance result.
