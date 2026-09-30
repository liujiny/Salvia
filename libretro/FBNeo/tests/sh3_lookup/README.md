# Four-way SH3 cache lookup regression

The production helper preserves lowest matching way, unchanged tags, miss-only
round-robin advancement, unsigned wrap and every matching-way mask.
Build `g++ -std=c++98 -O3 test.cpp -o test` and run it. Also build/run with
`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`.

The independent reference uses the previous four-way loop. Tests exhaust 5^4 tag
combinations including duplicates and zero, 5 PCs and 9 replacement cursor values,
then run 200000 random probes and 1000000 stateful replacement/reset operations.
`tests/sh3_dispatch` additionally compares final cache tags/cursors and full
synthetic execution behavior between reference, single and fused dispatch.
These tests do not execute PowerPC instructions or measure console FPS.
