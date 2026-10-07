# CV1000: account for SH3 PPC code-arena pressure

Baseline SHA for this round: `c4e92f3380ac85ef1ee09897806806790ace8b9a` (HEAD).

## Why

`sh3_drc_ppc.h` compiles SH3 guest code into a fixed code arena. When the arena
overflows, `compile()` resets `blocks`, `lookup` and `used`, which discards every
compiled block and forces the whole native working set to be rebuilt through the
existing interpreter fallback. The published hardware measurements in
`README.ppc-drc.md` recorded 7 to 16 full-arena recycling events inside one
600-frame CV1000 segment, but nothing reported how much of the arena is actually
live during normal gameplay, so the size could not be chosen from evidence.

The equivalent PS4 FBNeo problem (`sh3-arena64m`) was resolved by enlarging the
native code arena, and it is one of that project's retained optimizations. Before
trying the same change here we need an Xbox-independent measurement of arena
pressure.

## Change

Counting only. No emitter, dispatch, timing, memory or allocation behaviour is
modified.

- `Sh3DrcWorkProfile` gains `arena_recycles` and `arena_peak`.
- The overflow branch in `Sh3Ppc::compile()` increments `arena_recycles`.
- `used` after each block is compared against `arena_peak`, so the high-water
  mark is available both when the arena overflows and when it never does.
- `Sh3WorkReport()` emits one additional line while the game is paused:
  `drc_work_arena bytes=<arena> words=<words> recycles=<n> peak_words=<n> peak_bytes=<n>`.

Both counters are touched on the compile path only. Generated code, the five-way
lookup dispatch, guest cycle accounting and the existing 1-in-256 pause sampling
are untouched, and no per-instruction timer or live file I/O is added.

## Validation limits

The host differential harness compiles the same `sh4.cpp` for
`powerpc-linux-gnu` and runs it under `qemu-ppc`; it is host evidence for
equivalence and for arena accounting, not an Xbox FPS measurement. The counters
are compared between two builds that differ only in the arena size constant.

## Status

Instrumentation only: no performance claim, no Xbox runtime result yet.
