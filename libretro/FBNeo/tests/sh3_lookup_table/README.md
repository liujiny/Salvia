# Split SH3 lookup table regression

The production table contains 8192 contiguous four-tag sets followed by 8192 unsigned replacement cursors. Its single allocation remains 163840 bytes. The hot tag region is 131072 bytes; cursors occupy 32768 bytes and are read/written only for lookup misses. Block metadata, code cache and source validation are unchanged.

Build `test.cpp` with a host C++ compiler and run it, both optimized and with AddressSanitizer/UndefinedBehaviorSanitizer. It compares 2000000 operations with an independent old interleaved table, including duplicate/high-bit/zero tags, unsigned cursor wrap, full reset during compilation, and free/reallocate. Run the sibling `sh3_dispatch`, `sh3_lookup`, `sh3_source_check` and `sh3_block_layout` regressions as well. The dispatcher test includes the actual production layout.

These tests check equality semantics and metadata lifetime, not generated PowerPC execution, console FPS, or actual cache misses. The Xbox build's compile-time layout assertions and native disassembly must be checked separately.
