# CV1000: why the generated blocks end

Diagnostics-only change on top of `b183b7e`. The dispatch attribution
(`2026-10-09-cv1k-dispatch-split-console.md`) says each block entry costs ~995
cycles for ~6.8 guest cycles of work, so "fewer, longer blocks" is the lever
that matters; the call-weighted snapshot-length histogram cannot say *why* a
block ends, and that is what decides whether longer blocks are reachable
without a validation redesign.

## What was added

`compile()` records one of eight end reasons (the slot map's three spare bits,
so no new array), and the dispatcher counts them **per entry** in work-sampled
builds: `drc_work_block_ends`. The last two buckets split the delayed
conditionals by whether the block could instead have continued through the
delay slot into its fall-through -- which is what the emitter already does for
*non-delayed* conditionals. Eligible means the delay instruction compiles, does
not modify T (the branch's condition is evaluated before the delay slot runs)
and is not one of the memory forms that add guard exits.

## Measurement

`ddpdfk` from `user-ddpdfk4-core.state`, 60 frames, 5,658,274 entries, all
suites and `STATE 0cf251c3d512ddbb` plus the per-frame hashes unchanged:

| Block end | entries | share |
| --- | ---: | ---: |
| delayed conditional (BF/S, BT/S) | 2,540,571 | 44.9% |
| ... of which usable delay slot (buckets 6) | **1,420,769** | **25.1%** |
| ... of which not usable (bucket 7) | 1,119,802 | 19.8% |
| register-indirect transfer (JMP/JSR/RTS) | 2,095,309 | 37.0% |
| PC-relative jump (BRA/BSR) | 762,560 | 13.5% |
| instruction window ran out | 257,593 | 4.6% |
| unsupported opcode | 2,120 | 0.04% |
| self-loop conditional | 121 | 0.002% |

Two conclusions:

- **A quarter of all dispatches end at a delayed conditional with a delay slot
  the emitter could continue through.** Continuing the fall-through (and
  letting the taken edge exit as today) removes the second dispatch that the
  *not-taken* path currently spends re-entering at the delay slot, and it also
  stops compiling the delay slot twice (today it is translated inline on the
  taken path *and* again as its own block at `pc+2`).
- The other big classes need much more: 37% are calls and returns, which need
  either call inlining or linked returns, and 13.5% are BRA/BSR, which need the
  target's code duplicated and therefore a multi-span validation. Both are
  larger redesigns than the delayed-conditional case.

## Next

Implement the delayed-conditional continuation: commit the taken target early
(so a guard inside the delay slot leaves the interpreter able to re-execute the
branch), compile the delay instruction inline, test T, exit to the target on
the taken edge, and continue the fall-through in the same block. Because the
delay slot immediately follows the branch, the block's source stays contiguous,
so neither the snapshot nor the record layout changes.

## Revert

Revert this commit: the end-reason accounting in `sh3_drc_ppc.h`, the
`block_ends` counter in `sh3_drc_work_profile.h`, the dispatcher line and the
`drc_work_block_ends` report line in `sh4.cpp`.
