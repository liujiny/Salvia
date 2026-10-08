# CV1000: shrink the emitted PPC per block

Baseline: `3336c491` (the phase split of `2026-10-08-cv1k-codegen-split.md`; the
emitter block link is already reverted).

The phase split said where the generated code goes: 60.7 words per block of cold
guard-exit code (33.6% of everything emitted) and 51.9 words of address
machinery (28.7%), of which the 32-bit host pointers are the avoidable part.
This round attacks both.

## What changed

Two mechanisms, one goal (fewer emitted words per block at identical guest
behaviour). They are independent, and both live in the SH3 PPC backend
(`sh3_drc_ppc.h`, plus one driver field in `sh4.cpp`):

1. **One shared completion tail per block.** Every guard exit and the block's
   own completion used to carry a full copy of *publish pc/ppc, charge
   total/icount, return the dispatcher code*. That sequence is now emitted once
   per block and reached with an unconditional branch. Each caller passes the
   pc value in r0, the guest cycles in r12 and the result code in r5, all of
   which are dead once the cached guest registers are written back — the exits
   flush first. Exits are cold, so the extra branch costs nothing on the hot
   path; the block's own completion also shares it, and `finish()` therefore
   emits five words instead of twelve.
   Two tail entry points exist: one stores pc, one does not. A delay-slot exit
   must leave pc alone (the branch already committed its target and the pending
   slot travels in `delay`), and a conditional exit publishes a target that
   differs from its ppc.
2. **The registered RAM window base stays in r4.** A translated RAM access used
   to rebuild the 32-bit host pointer — `lis`+`ori` plus an add — at every
   access, and the store code-write check rebuilt the block's source pointer
   per store. The base is now loaded once per block from a new unscanned driver
   field (`Sh3PpcState::ram_base`, refreshed wherever `ram_window.base` is set,
   and every registration, mapping or handler change already revokes the code),
   and `add r12,r12,r4` replaces the three words. The base register costs one
   guest-register slot: `HOST_REGS` drops 7 -> 6 and the slots move to r5..r10.
   The compile-time-bound literal operands use `addi r12,r4,off` when the
   offset fits the 16-bit immediate. The emitter keeps the immediate form as a
   fallback whenever the register and the window disagree or the offset is too
   large, so the register is an optimisation, never an assumption.

Phase attribution shifts slightly: the shared tail is counted inside `exit`
(it is emitted with the exits), so `complete` shrinks by more than the text it
lost, and a smaller `HOST_REGS` means fewer dirty slots to flush at every exit
and completion.

## Host measurement

`ddpdfk` from `user-ddpdfk4-core.state`, 60 frames, dips `00,07,00,00`,
`render_cores=2`, 9,323 compiled blocks in every build. All three runs print
`STATE 0cf251c3d512ddbb` and `rebuilds=9327`, and their per-frame video/audio
hash files are byte-identical.

| Emitted words | baseline `3336c491` | + tail | + RAM base |
| --- | ---: | ---: | ---: |
| body | 209,455 | 209,455 | 212,781 |
| memaddr | 484,218 | 484,218 | 428,686 |
| memguard | 135,965 | 135,965 | 135,965 |
| memaccess | 103,997 | 103,997 | 103,997 |
| complete | 188,189 | 157,630 | 152,608 |
| exit | 566,257 | 390,787 | 375,975 |
| **total** | **1,688,081** | **1,482,052** | **1,410,012** |
| words / block | 181.07 | 158.97 | 151.24 |
| `drc_work_arena peak_words` | 1,702,184 | 1,496,092 | 1,424,320 |

The tail removes 206,029 words (-12.2%) and the RAM base another 72,040
(-4.3%, including the 9,323 one-word block prologues); together **-16.5%** of
the emitted words and **-16.3%** of the arena high-water mark. The hot-path
phases (body, memaddr, memguard, memaccess, complete) fall from 1,121,824 to
1,034,037 words.

Per mechanism, in words per block: the tail turns a 15.6-word exit into an
8-word exit plus ~2.6 words of shared tail, and the RAM base removes two words
from each of the 34,591 direct accesses at the cost of one word per block. The
store-side source pointer still materialises its immediate: the block's own
source sits up to 8 MiB into the window, so only 79 of the 14,592 stores fit
the 16-bit `addi`.

## Long run

The same state run for 1,800 and 3,000 frames, report taken at exit. The host
baseline already reaches 97.8% of the 20 MiB arena at 1,800 frames without
overflowing, so the window has to run to 3,000 frames for the overflow guard to
fire in the baseline:

| | baseline 1,800 | this change 1,800 | baseline 3,000 | this change 3,000 |
| --- | ---: | ---: | ---: | ---: |
| compiled blocks | 30,532 | 30,532 | 56,535 | 36,939 |
| `drc_work_arena peak_words` | 5,127,536 (97.8%) | 4,318,532 (82.4%) | 5,239,368 (99.93%) | 5,118,948 (97.6%) |
| `drc_work_arena recycles` | 0 | 0 | **1** | **0** |
| `drc_work_dispatch rebuilds` | 30,541 | 30,541 | 56,553 | 36,948 |
| `drc_work_codegen exit` | 1,702,859 | 1,147,420 | 3,210,623 | 1,358,146 |
| `STATE` | dff81882efc1b24a | dff81882efc1b24a | d0a3e4101ebe7de5 | d0a3e4101ebe7de5 |

In the 3,000-frame window the baseline overflows the arena once, which discards
every compiled block and forces the working set back through the interpreter:
that is the extra 19,596 compiles and 19,605 rebuilds in its column, and on the
console it is a visible stall. This change does not overflow, and its high-water
mark at 3,000 frames is what the baseline already reached around 1,800. The
per-frame video and audio hash files of the two 3,000-frame runs are
byte-identical, so both ran the same workload.

At the same frame count the high-water mark tracks the per-block size:
5,127,536 -> 4,318,532 words (-15.8%) at 1,800 frames, with identical block
counts and identical rebuilds, so the drop is the code size and not a changed
workload.

Host wall time (`qemu-ppc` after 14 interleaved 60-frame runs of the two
binaries) is bimodal — each binary lands near either 14.9 s or 18.7 s depending
on machine state — so only the minimum over interleaved runs is usable:
baseline 15.01 s, this change 14.89 s. QEMU executes the generated PPC and the
C++ dispatcher natively, so it cannot show what the smaller arena high-water
mark does to a Xenon stall; the number is reported only to show that the extra
branch on the hot path and the one-word prologue did not cost anything
measurable here.

## Why this is the right lever

The emitter-link experiment established that removing 22-25% of the
dispatcher's block entries changed nothing on the console, so the frame cost is
not the dispatcher metadata traffic and not the number of entries. The arena is
what stalls: 20 MiB runs at ~97.5% in long console sessions and the host
baseline reaches 97.8% of it in 1,800 frames, so the overflow guard — a full
recompile — sits just past the measurement window. Shrinking the generated code
is the lever that moves that high-water mark directly, and it moves the I-cache
footprint with it.

## Verification

- `tests/sh3_ppc/run.py` (executes the emitted PPC under `qemu-ppc -cpu g4`
  against the interpreter): `PASS 842085 cases compiled 724270 fallback
  117815` — identical case/compile/fallback counts to the recorded baseline.
- `tests/sh3_dispatch/run_inline.py`: `PASS 8 host suites in optimized and
  ASan/UBSan modes`.
- `tests/sh3_hot_fallback/run.py`: PASS.
- `tests/sh3_block_layout/contract.cpp` still compiles with `g++ -m32` (the
  `Block` record stays 84 bytes; only a driver field was added, and it is never
  scanned, so existing save states and the `STATE` hash are unaffected).
- 60-frame `ddpdfk`: `STATE 0cf251c3d512ddbb` and byte-identical per-frame
  video/audio hashes against the baseline.

## Validation limits

Host `qemu-ppc` evidence only. No console frame rate or console stall count is
claimed. QEMU executes the generated PPC natively, so it cannot show what the
smaller arena high-water mark does to a Xenon stall; that needs a console run of
the diagnostics image, and only if a candidate actually reduces recycles.

## Revert

Revert this commit: `sh3_drc_ppc.h` plus the `ram_base` field in `sh4.cpp`. No
artifact, runtime copy or save-state format is part of it.
