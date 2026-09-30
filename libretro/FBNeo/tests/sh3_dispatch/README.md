# SH3 C++ dispatcher fusion regression

Compile `test.cpp` using a C++03-capable host compiler. The test includes the exact production `sh3_drc_dispatch.h` with synthetic native block callbacks and a preserved pre-change single-block dispatcher as oracle. It compares full callback traces, registers, RAM, mappings, cycle budgets, cache compilation counts and slice-end timer observations across 12000 seeded traces and twelve slices per trace.

Run both optimized and ASan/UBSan builds. The test deliberately exercises before/after-partial guards, pending delay slots and IRQs, aliases, read/fetch remaps, short and exhausted budgets, allocation failure, cache eviction and arena reset. A straight-chain test measures the number of C++ dispatcher entries eliminated, not console performance.

The existing `tests/sh3_ppc` entry remains single-block so its instruction-level coverage is unchanged. This host test does not execute PowerPC instructions or establish Xbox FPS. Native XDK compilation and console validation remain separate gates.
