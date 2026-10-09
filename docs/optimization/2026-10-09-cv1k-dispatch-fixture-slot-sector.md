# CV1000: the dispatcher host fixture was uncompilable since the arena ring

Baseline: `6aad7ef4`. Test-only change, no production code.

## What was wrong

`tests/sh3_dispatch/test.cpp` builds a synthetic `Sh3Ppc` namespace (a fixed
table, a stub `compile()`, a stub `arena_note_slot()`) and then includes the
production `sh3_drc_dispatch.h`. The sector-ring change `06a89f1c` made that
header read `slot_sector[index*WAYS+way]` to bucket a block's end reason, but
the fixture never declared `slot_sector`, and it does not include
`sh3_drc_ppc.h` (which declares it) because it replaces the whole allocator.
The suite has therefore failed to compile since `06a89f1c`:

```
sh3_drc_dispatch.h:101:30: error: 'slot_sector' was not declared in this scope
```

Verified on an unmodified `6aad7ef4` working tree (stash the working changes,
run, restore), so this is a pre-existing gap and not an effect of the
delayed-conditional work that found it.

## Change

Declare the same array in the fixture (`static UINT8 slot_sector[TABLE_SIZE]`)
and reset it to "no slot has code yet" (0xFF) in `clear()`. The fixture never
reuses sectors, so the array stays 0xFF and only satisfies the header's read.

## Result

`python3 tests/sh3_dispatch/run_inline.py` now compiles and runs again:
**PASS 8 host suites in optimized and ASan/UBSan modes**, including the
12,309-case service suite, the fallback-detail suites and the random
dispatch-trace suites that compare state, memory, callback order, compilation
counts, emulated cycles and final tags against the reference. Nothing else
touches this change: no production file is modified.

## Revert

Revert this commit; it touches only the fixture and this document.
