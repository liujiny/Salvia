# CV1000: continuing a delayed conditional's fall-through in the same block

Baseline for this experiment: `72a4dea6`, the dispatcher-probe attribution
(`2026-10-09-cv1k-dispatch-split-console.md`) and the end-reason histogram
(`2026-10-09-cv1k-block-end-histogram.md`) that this change acts on. One
logical change, host-verified only; no console frame rate is claimed.

## Why

The console attribution puts 7.09 ms/frame (49% of the dispatch phase, 484
cycles per entry) in the pre-entry span and 2,540,571 of the 5,658,274 entries
of the 60-frame reference run *end at a delayed conditional*. The interpreter
(`sh4.cpp`) shows why most of those ends are avoidable:

- `BFS`/`BTS` only touch `m_delay` and `m_pc` **inside** their taken branch. A
  not-taken delayed conditional is a pure no-op that leaves PC at `pc+2` and
  the pending slot clear, exactly like a not-taken `BF`/`BT`.
- The emitter already continues the fall-through in the block for non-delayed
  conditionals, and for the taken edge of a delayed conditional it already
  compiles the delay instruction natively when it can.

What was left was the not-taken edge: it always exited at `pc+2`, so the delay
instruction and everything after it were reached through a second dispatcher
round trip (lookup, record access, source validation).

## Change

`sh3_drc_ppc.h` only:

- In `control()`, a delayed conditional with a compile-time constant target does
  not end the block. The taken edge is emitted **out of line** (jump over it on
  the fall-through path) and keeps the existing behaviour: it folds the delay
  instruction natively through `delay_instruction()`/`finish_delay()` when it
  can, and otherwise leaves the slot pending for the interpreter. The
  fall-through path costs one extra unconditional branch and then continues at
  `pc+2` like any other instruction, so the whole delay slot and the code after
  it stay in the block.
- Cycle accounting matches the interpreter: the fall-through pays the branch's
  single `EAT`, the taken edge pays one cycle more (`BFS`/`BTS` taken decrement
  `m_sh4_icount` once), and `finish_delay()` adds the folded slot's own cycle.
  `Compiler::max_taken` publishes the worst path to the dispatcher's admission
  budget.
- The out-of-line path is emitted with the branch-time slot cache, which is then
  handed back: the fall-through path jumps over the code that would have
  changed it.
- `guard()` now keys an exit on PC **and** the delay-slot context. A guard
  inside a delay slot publishes a pending slot instead of PC, so it can no
  longer be shared with a guard for the same instruction compiled on a path
  that arrives there directly. This was a latent bug: the new fall-through
  compilation is what made the two contexts meet at the same PC.

### The first attempt and why it failed

An earlier version of this change (never committed, reverted in the worktree)
failed the PowerPC differential suite with `expected pc=0c00010a got
0c000108`-class mismatches. Two independent causes:

1. It reused the non-delayed conditional's exit record, whose cost is `+3`
   (EAT plus the two-cycle `BT`/`BF` taken cost). A taken delayed conditional
   costs `+2`. One extra charged cycle moves the interpreter's stopping point
   one instruction away from the block's, which is exactly what the suite
   reported.
2. The guard-exit coalescing above: with the delay-slot guard exit shared, the
   fall-through path published a *pending* delay slot (and no PC) instead of PC
   at `pc+2`.

Both are fixed; the current emitter is the corrected design.

## Measurement

`ddpdfk` from `user-ddpdfk4-core.state`, `dips=00,07,00,00`, `render_cores=2`,
no input, 60 frames, `qemu-ppc -cpu g4`. Baseline is a build of `72a4dea6`
(`drc_work_block_ends` and `drc_work_codegen` counted in both builds):

| 60 frames | `72a4dea6` | this change | delta |
| --- | ---: | ---: | ---: |
| block entries (`native_calls`) | 5,658,274 | 4,725,573 | **-16.5%** |
| source validations | 5,702,610 | 4,789,825 | -16.0% |
| validated words | 50,912,473 | 62,133,378 | +22.0% |
| `rebuilds` | 9,327 | 9,475 | +1.6% |
| dispatcher calls | 108,871 | 193,335 | +77.6% |
| interpreter steps | 106,385 | 192,493 | +80.9% |
| ends: delayed conditional | 2,540,571 | 239,216 | -90.6% |
| ends: instruction window | 257,593 | 782,092 | +203% |
| ends: PC-relative jump | 762,560 | 1,041,723 | +37% |
| ends: register-indirect | 2,095,309 | 2,779,664 | +33% |
| arena `peak_words` | 1,229,584 | 1,790,644 | +45.6% |
| generated words | 1,154,593 | 1,752,496 | +51.8% |
| words per block | 123.9 | 185.0 | +49.4% |
| `short_budget` exits | 51,543 | 71,607 | +38.9% |
| `boundary` exits | 35,458 | 99,858 | +181.6% |

1800 frames, same state and DIPs:

| 1800 frames | `72a4dea6` | this change | delta |
| --- | ---: | ---: | ---: |
| block entries (`native_calls`) | 80,011,513 | 66,849,642 | -16.5% |
| source validations | 80,898,296 | 68,031,011 | -15.9% |
| validated words | 723,951,294 | 918,214,701 | +26.8% |
| `rebuilds` | 30,541 | 33,108 | +8.4% |
| blocks compiled | 30,532 | 33,099 | +8.4% |
| generated words | 3,615,984 | 5,555,159 | +53.6% |
| dispatcher calls | 2,158,202 | 3,228,044 | +49.6% |
| interpreter steps | 1,961,501 | 3,057,677 | +55.9% |
| arena `peak_words` | 3,718,292 (70.9%) | 5,238,984 (99.9%) | +40.9% |
| `evictions` / `evicted_slots` | 0 / 0 | 2 / 3,234 | |
| `recycles` | 0 | 0 | = |

Both runs end in `STATE dff81882efc1b24a`, matching the value the earlier epoch
run recorded for this state over the same length.

### What the numbers say

- The delayed-conditional ends drop by 90.6% and entries by 16.5%, which is the
  dispatcher work this change was aimed at. Each avoided entry is worth the
  pre-entry span (484 cycles) plus its share of the chain loop.
- The cost is code volume: the fused fall-through is inlined into every
  predecessor, so generated words rise 51.8%, `words/block` 49.4% and the
  60-frame arena peak 45.6%. This is inherent to fusion (it is the trade the
  code-size round did not have to make) and it is why the console attribution's
  measured coupling matters: the code-size round moved frame time 1-3% for a
  31% *reduction* in words/block, so this growth is expected to cost a small
  fraction of what the removed entries save, not a proportional one.
- The interpreter steps rise 80.9%, from the same budget-boundary effect that
  raises `short_budget` and `boundary` exits: a longer block consumes the time
  slice in larger pieces, so the chained loop and the admission test trip more
  often. This is the part of the trade that is a straight loss.
- The 1800-frame arena loses its headroom: 70.9% -> 99.9% of 20 MiB, with the
  first two sector evictions and 8.4% more rebuilds. The absolute numbers are
  small (2 sector reuses in 1800 frames), but this is the signal the arena work
  was built to watch, and it moves the wrong way.

## Status: measured, not adopted

The change is correct and does what it set out to do (90.6% of the
delayed-conditional block ends disappear, entries and validations fall 16%),
but it fails this project's stated host gate for a console A/B: the 1800-frame
arena, rebuild and generated-code numbers all move against it, and only the
entry/validation side moves for it. The console prediction is genuinely
two-sided:

- for: the avoided entry is worth the pre-entry span (484 of the 995 cycles per
  entry). The only console A/B that measured an entry reduction without
  changing per-entry work (`2026-10-08-cv1k-emitter-block-link.md`) saw -19.2%
  entries, and its phase regression is fully explained by the five store words
  per generated store that experiment added -- not by the link itself. This
  change adds no store or per-entry instruction.
- against: 41% more generated code behind a 1 MiB L2, 56% more budget-boundary
  interpreter steps, and 8.4% more rebuilds. The same attribution says the
  generated code is the second-largest phase, so a footprint change of this
  size is not obviously free even though the earlier -31% words/block round
  only moved frame time 1-3%.

Under the user's rule ("bring `recycles` down first, then ask for hardware") no
console run is requested for this commit. The implementation is preserved at
this commit's SHA so a diagnostics image can be built and A/B'd against the
deployed `dispatch-probe-split` image without this change, if that is wanted.
The follow-up commit reverts the emitter change and keeps this document.

## Verification

- `tests/sh3_ppc/run.py`: **PASS 846,026 cases, compiled 727,372, fallback
  118,654**. The recorded reference is 842,085/724,270/117,815 and the mode-loop
  totals are byte-identical (51,317 / 102,634 / 153,951 / 205,268 / 256,585);
  the +3,941 cases come from the "every supported opcode in a taken branch's
  delay slot" section, which only checks a block whose `words == 2`. A delayed
  conditional whose slot the emitter cannot fold now continues through the slot
  instead of ending at the branch, so those cases are inside the checked set
  and they pass.
- Per-frame video and audio hashes and the final `STATE 0cf251c3d512ddbb` are
  byte-identical to the baseline over 60 frames, and the 1800-frame state
  `dff81882efc1b24a` matches the value recorded by the earlier epoch run of the
  same state.
- `tests/sh3_hot_fallback/run.py`, `tests/sh3_block_layout/contract.cpp`
  (`-m32`) and the layout regression all pass. `tests/sh3_dispatch/run_inline.py`
  does not compile in this tree **and does not compile on the baseline either**:
  its synthetic translation unit has no `slot_sector`, which the dispatcher
  header has referenced since the sector-ring commit `06a89f1c`. That is a
  pre-existing gap in the host test set, not an effect of this change.

## Limits

Host-only evidence: a 32-bit big-endian PowerPC build under `qemu-ppc`, which
executes the generated PPC but does not model Xenon cache behaviour. QEMU wall
time is not a 360 measurement and no frame-rate number is claimed. The suite
cannot see the two things the console pair must decide: whether 16.5% fewer
dispatcher entries pays more than 46% more generated code costs on a 1 MiB L2,
and whether the extra budget-boundary interpreter steps cost more than they
look.

## If it is A/B'd anyway

Diagnostics builds, same state, no input, 3-4 pauses: the deployed
`dispatch-probe-split` image against this change's diagnostics image. The signal
to read is `core_phase_ms drc_dispatch` (`dispatch_pre`, `blk_entry`,
`dispatch_post`), the work-sampled `native_calls` and rebuild counters, and
`core_frame_ms cpu_io`. `dispatch_pre` is the number that should fall most; if
it falls by much less than the 16% of entries this removes, the pre-entry span
is not per-entry work on the console either and this whole lever is dead.

## Revert

Revert this commit: it touches only `sh3_drc_ppc.h` (the delayed-conditional
fall-through, `max_taken`, and the guard-exit coalescing key).
