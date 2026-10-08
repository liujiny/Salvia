# CV1000: write guest registers straight through instead of flushing at exits

Baseline for this round: `a122dd15` (the T and halfword-load round of
`2026-10-08-cv1k-t-and-word-load.md`).

## What changed

The PPC backend cached guest registers in r5..r10 and deferred the write-back:
a guest write only marked the slot dirty, and the stores happened later — at
every guard exit (each exit carried its own dirty-set flush), at the block's
completion, in the folded delay-slot path and on every slot eviction. One
guest write therefore paid a store at *each* exit of the block that could
observe it.

`dirty()` now emits the store immediately, so a slot is a pure read of what
the architectural register file already holds. The consequences:

- the guard exits, the completion, the folded delay-slot path and the native
  loop-back no longer flush anything (`flush()` and its call sites are gone),
- the slot eviction in `reg()` no longer has a dirty snapshot to store,
- `BlockExit` no longer carries a register snapshot, so the exits drop the
  per-exit `memcpy` as well,
- the register file is valid at every point of the block, including the guard
  exits and the service exits, which is what the interpreter fallback and
  `sh3_drc_service_movll` read.

This trades a store on the simulated hot path for the same stores that the
single exit path that actually ran used to do: a block run writes each guest
register once instead of once per exit, and the eviction stores disappear.
It is a static-code-size win with no dynamic penalty — measured below.

## Host measurement

`ddpdfk` from `user-ddpdfk4-core.state`, 60 frames, dips `00,07,00,00`,
`render_cores=2`, 9,323 compiled blocks, `rebuilds=9327` and
`STATE 0cf251c3d512ddbb`; the per-frame video/audio hash file is byte-identical
to the baseline's:

| Emitted words | baseline `3336c491` | `a122dd15` | this change |
| --- | ---: | ---: | ---: |
| body | 209,455 | 206,901 | 249,934 |
| memaddr | 484,218 | 376,502 | 364,735 |
| memguard | 135,965 | 102,374 | 102,374 |
| memaccess | 103,997 | 101,095 | 137,947 |
| complete | 188,189 | 100,954 | 56,685 |
| exit | 566,257 | 384,462 | 287,918 |
| **total** | **1,688,081** | **1,272,288** | **1,199,593** |
| words / block | 181.07 | 136.46 | 128.67 |
| `drc_work_arena peak_words` | 1,702,184 | 1,286,116 | 1,213,656 |

The flush removal is what the `exit`, `complete` and `memaddr` rows lose
(152,587 words); the write-through stores are the `body` and `memaccess` rows
(79,892 words). Net 72,695 words (-5.7%), and 488,488 words (-28.9%)
cumulative from the pre-shrink baseline.

## Verification

- `tests/sh3_ppc/run.py`: `PASS 842085 cases compiled 724270 fallback 117815`,
  the same counts as the recorded baseline. That suite compares the full
  architectural state and RAM after every case, including the guard exits,
  delay-slot exits, watched/device service exits and code-write exits, so it
  is the direct oracle for this change.
- `tests/sh3_dispatch/run_inline.py`: PASS 8 host suites (optimized and
  ASan/UBSan). `tests/sh3_hot_fallback/run.py`: PASS. The `Block` layout
  contract still compiles with `g++ -m32`.
- 60-frame `ddpdfk`: `STATE 0cf251c3d512ddbb` and byte-identical per-frame
  hashes.

## Long run

| | baseline `3336c491` | `a122dd15` | this change |
| --- | ---: | ---: | ---: |
| 1,800 frames `peak_words` | 5,127,536 (97.8%) | 3,885,784 (74.1%) | 3,662,568 (69.9%) |
| 1,800 frames `recycles` | 0 | 0 | 0 |
| 1,800 frames `rebuilds` | 30,541 | 30,541 | 30,541 |
| 3,000 frames `peak_words` | 5,239,368 (99.93%) | 4,604,344 (87.8%) | 4,341,764 (82.8%) |
| 3,000 frames `recycles` | 1 | 0 | 0 |
| 3,000 frames `rebuilds` | 56,553 | 36,948 | 36,948 |
| 3,000 frames `STATE` | d0a3e4101ebe7de5 | d0a3e4101ebe7de5 | d0a3e4101ebe7de5 |

Same state, no input, report taken at exit; every per-frame hash file matches
the baseline's. The baseline's 3,000-frame peak is the arena cap because it
overflowed there, so the meaningful 3,000-frame comparison is against
`a122dd15` (-5.7%).

## Validation limits

Host `qemu-ppc` evidence only; no console frame rate or stall count is claimed.
QEMU executes the generated PPC and the dispatcher natively, so it cannot show
what the smaller arena high-water mark does to a Xenon stall.

## Revert

Revert this commit: `sh3_drc_ppc.h` only.

## Console A/B images and host wall time

Only the diagnostics flavor was built. The image for the state after all four
rounds (`04245442`) is `29d56e8ac2d3cfdb35052fb787c82ad54d5fadbd18324c71cdb30f179026601f`,
34,631,680 bytes, archived as
`xex-archive/fbneo-20261009-0035-codegen-shrink-r4-diag.xex` and deployed as
`Distro360/fbneo.xex` / `fbneo-diag.xex`. The matched `3336c491` baseline image
is unchanged (`3faa500e...`, `Distro360/fbneo-codegen-shrink-base-diag.xex`), so
the same pair still covers the A/B.

Host wall time, 6 interleaved 60-frame runs of the two binaries: baseline
minima 16.50 s, this build 15.81 s (-4.2%), and every pair favoured this build.
The bimodality the earlier rounds saw (14.9 s vs 18.6 s) did not appear in this
batch; the number is still only a host-side sanity check that the extra stores
on the hot path did not cost anything measurable.
