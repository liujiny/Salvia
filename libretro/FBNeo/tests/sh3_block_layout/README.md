# SH3 production Block layout regression

`test.cpp` includes the same `sh3_drc_block.h` as the PPC backend. It verifies snapshot equality against the previous record layout and libc memcmp, while checking that metadata, entry invocation and reset remain intact. The test covers 64 record positions, lengths 0..33, source halfword alignments, all bit mutations and out-of-range tails. Run both optimized and ASan/UBSan host builds.

`contract.cpp` is compile-only with `g++ -m32 -std=gnu++98 -c contract.cpp`. No 32-bit libc link is needed. It enforces size 84, the intended member offsets, and 32768-record metadata size 2752512. The same assertions are compiled into the real Xbox backend.

The host dispatcher regression in `../sh3_dispatch` also includes the production record declaration. Its native callbacks are synthetic: neither these tests nor the compile-only contract execute generated PPC instructions or predict Xbox frame rate.
