# Exact SH3 source equality validation

`sh3_drc_source_check.h` replaces the Xbox compiler's inlined byte-wise comparison in the dispatcher. It compares the SAME `Block.words` halfwords on EVERY entry. It does not cache equality, skip mutable RAM, hash code, modify the code-generation backend, or change invalidation semantics.

Run the standalone check on a host:

```sh
g++ -std=gnu++98 -O3 tests/sh3_source_check/test.cpp -o /tmp/sh3-source-test
/tmp/sh3-source-test
g++ -std=gnu++98 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/sh3_source_check/test.cpp -o /tmp/sh3-source-asan
/tmp/sh3-source-asan
```

The independent oracle uses libc `memcmp` for equality. Coverage includes lengths 0..33, all 16 bit positions in every word, each halfword alignment modulo 8, 200000 random pairs, changes beyond the requested range, tight heap allocations, and guard pages at each end. The length never rounds up: partially filled groups check only their 1..3 valid words. UINT16 pointers retain their existing 2-byte alignment contract; no uint32/uint64 pointer cast is introduced.

The production dispatcher is also tested by `tests/sh3_dispatch/test.cpp`, whose synthetic block compiler now generates snapshot lengths 1..33 bounded by a mapped page. Its reference retains the old bytewise `memcmp` equality check. Both single and continuous dispatch must retain state, memory, compilation counts, mappings, callback order and consumed cycles through 12000 randomized traces of 12 slices.

These are host correctness tests, not SH3 machine-code or PowerPC execution tests and not console performance measurements. Native XDK disassembly is reviewed separately to verify the intended halfword loads survive optimization.
