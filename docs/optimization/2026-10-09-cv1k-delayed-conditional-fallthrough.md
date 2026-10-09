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

## Console A/B: the change wins

The host numbers above fail the gate this round had been using for *size*
changes (arena headroom, rebuilds, generated words all move against it), so the
emitter change was reverted and only the record kept. The console pair was
built and run anyway, because the change moves the two halves of the dispatch
phase in opposite directions and the probe split can therefore measure the
per-entry coefficient on its own. It does, and it reverses the host-only
verdict.

`ddpdfk`, same save state, no input, `dips=00,07,00,00`, `render_cores=2`, one
long window per image (3,520 frames A / 3,524 frames B, 52 timing samples
each). Workload is matched: `worker_jobs` 0.935/frame both, `timer_callbacks`
36.06/frame both, workload-sampled `native_guest_cycles` 5,608,195 vs
5,513,051.

| Per frame | A `958ad026` | B `dd00ff88` | delta |
| --- | ---: | ---: | ---: |
| `core_frame_ms total` | 17.233 | 16.801 | **-2.5%** |
| `core_frame_ms cpu_io` | 16.219 | 15.762 | **-2.8%** |
| `core_phase_ms drc_dispatch` | 14.532 | 13.874 | **-4.5%** |
| `dispatch_pre` (tick-corrected) | 7.19 | 6.66 | -7.4% |
| `blk_entry` (tick-corrected) | 5.73 | 5.61 | -2.1% |
| `dispatch_post` (tick-corrected) | 0.47 | 0.55 | +17% |
| chain loop and remainder | 1.14 | 1.05 | -8% |
| entries (`entry_samples`, 1-in-64, per frame) | 734.6 | 611.4 | -16.8% |
| `dispatch_calls` per frame | 1,447.6 | 2,514.2 | +73.7% |
| validations / validated words | 615,568 / 5.55M | 506,650 / 6.92M | -17.7% / +24.8% |
| generated words / blocks | 3,757,880 / 32,115 | 6,051,213 / 36,144 | +61% / +12.5% |
| arena `peak_words` | 3,837,816 (73.2%) | 5,239,092 (99.9%) | full |
| `evictions` / `evicted_slots` | 0 / 0 | 4 / 5,633 | |
| `interpreter_steps` | 13,082 | 21,781 | +66.5% |
| `short_budget` / `boundary` exits | 5,183 / 4,317 | 7,468 / 10,736 | +44% / +149% |
| `sampled_peak_ms` / frames >=25 ms | 24.08 / 0 of 52 | 25.55 / 1 of 52 | +6% |

### The coefficients this buys

- Per-entry pre-entry cost: 7.19 ms / 47,014 entries = 490 cycles in A (the
  phase split measured 484), 6.66 ms / 39,133 = 545 cycles in B. The per-entry
  cost went *up* because each surviving entry validates 24.8% more words.
- Marginal value of a removed entry: -0.53 ms/frame for -7,881 entries/frame =
  67 ns = **215 cycles per entry**, net of the extra validation bytes that come
  with longer blocks. That is the number the entry-reduction family needs: any
  mechanism that removes an entry for less than ~215 cycles of added work is a
  win on this hardware, and a linked successor (a compare and a `bctr`, no
  duplicated code, no store-side hook) is far below that.
- The generated code span did **not** grow: -2.1% per frame for +61% generated
  words and +18% per entry (blocks execute more guest instructions each). This
  is the same weak size-to-time coupling the -31% words/block round measured,
  now confirmed in the other direction; it is why the host-only size gate was
  the wrong filter for this class of change.
- Costs that are real but small: the arena loses its headroom (73.2% -> 99.9%,
  first four sector evictions, +12.5% blocks compiled), `dispatch_calls` rise
  73.7% (the chain breaks more often) and interpreted steps rise 66.5%. All of
  that is inside the -4.5% phase and the -2.5% frame.

Status: **adopted**. The emitter change is restored on `main` by the commit
after the revert (`dd00ff88` is the implementation, `6aad7ef4` the revert, the
restore is the commit that carries this section).

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

## The console pair

A diagnostics pair was built, because this change moves the two halves of the
dispatch phase in opposite directions and the probe split can therefore measure
the per-entry coefficient that the rejected block-link A/B left unknown (its
entry reduction was entangled with five store words per generated store).

Both are `SALVIA_FBNEO_DIAGNOSTICS 1`, `SALVIA_CV1K_PROBE 1`, built by
`salvia-tests/xex-build/build-xex.sh --baseline 3336c491 --paths
/tmp/mirror-extra.txt` with the same nine mirrored paths, `--source-rev
dd00ff88` (B, deployed as the primary flavor) and `--source-rev 958ad026
--no-deploy` (A):

| Image | Source | Bytes | SHA256 |
| --- | --- | ---: | --- |
| `Distro360/fbneo-head-base-diag.xex` (A) | `958ad026` (HEAD, no emitter change) | 34,631,680 | `f9e8c60a30478af14631f156136e1e94bce83ea608ce1bcc7d20f9dbf82a8fc8` |
| `Distro360/fbneo.xex` = `fbneo-delayed-fallthrough-diag.xex` (B) | `dd00ff88` (this change) | 34,631,680 | `cf1f860b04a94db01b7cc9af37138a3c7ff0ae8e2586ef205f74405054012179` |

Archives: `xex-archive/fbneo-20261009-1917-head-base-fallthrough-ab-diag.xex` and
`xex-archive/fbneo-20261009-1914-delayed-fallthrough-diag.xex`. The runtime
mirror now holds the A sources (A was built last), so rebuilding B needs
`--source-rev dd00ff88`. No release flavor was compiled: the change is not
accepted, and the AGENTS rule is one release compile at acceptance. The XEX
container is encrypted, so this environment cannot verify the embedded PPC
image; identity comes from the mirror log and from the build tag the log
self-reports (`2026-10-08-cv1k-build-identity.md`).

Protocol: same save state, no input, `dips=00,07,00,00` (DIP B bit 2 enables
the `drc_work_*` counters), two render cores, one long window per image; the
windows are then paired by cumulative frame number. The identity check that
settled the images was the work-sampled entry count: 609,986 in A against
498,788 in B, with `drc_work_block_ends` delayed-conditional bucket collapsed
from 310,579 to 28,897, and `requested_words` up 24.8% exactly as the host
harness predicted.

A release flavor is still not compiled: the AGENTS rule is one release compile
at acceptance, and the acceptance decision needs the user's word on the frame
result above. The diagnostics image that produced B is deployed as
`Distro360/fbneo.xex`.

## Revert

Revert the restore commit: it re-applies `dd00ff88`, which touches only
`sh3_drc_ppc.h` (the delayed-conditional fall-through, `max_taken`, and the
guard-exit coalescing key). The history is deliberately three-step:
`dd00ff88` implements it, `6aad7ef4` reverts it on host-only evidence, and the
restore commit re-applies it on the console pair above.
