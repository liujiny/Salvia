# CV1000: the fusion cap was bounded by a 20 MiB arena, not by the fusion

Baseline for this round: `c23c9876`. One logical change: the default
`SALVIA_CV1K_FUSE_CAP` goes from 3 back to 33 (the whole straight-line run,
which is what `MAX_INSNS` bounds anyway), plus this review.

## Why re-open a rejected experiment

`2026-10-09-cv1k-fusion-cap.md` capped the delayed-conditional fall-through at 3
after the console A/B in
`2026-10-09-cv1k-delayed-conditional-fallthrough.md` won on the mean (-2.5% of
`core_frame_ms total` for -16.5% of entries) and lost on the tail: one of 52
samples at 25.55 ms against none in the baseline. The cause the review named was
the *cost of the absorbed code*:

| per 3,524-frame console window | cap 0 | uncapped |
| --- | ---: | ---: |
| block entries | 5,658,274 | 4,725,573 (-16.5%) |
| generated words | 3.76M | 6.05M (+61%) |
| `interpreter_steps` | 13,082 | 21,781 (+66.5%) |
| arena `peak_words` | 73.2% | **99.9%**, 4 evictions |
| frames >= 25 ms | 0 of 52 | 1 of 52 |

Two of those four numbers have since moved, and both of them are the arena:

- the code arena went from 20 MiB to 32 MiB (`23a40cf3`), specifically for the
  fused working set;
- the write-through-register work (`04245442`) cut the emitted words per block
  by 30%, and the sector ring (`06a89f1c`) bounds what an overflow costs.

So the same experiment is worth re-running: the tail risk was attributed to a
99.9% arena that no longer occurs.

## Sweep, re-run on the current tree

`ddpdfk` from `user-ddpdfk4-core.state`, `dips=00,07,00,00`, `render_cores=2`,
no input, host harness. The cap is a `-D`, so the sources are identical.

| 60 frames | cap 3 | cap 8 | cap 16 | cap 33 |
| --- | ---: | ---: | ---: | ---: |
| `native_calls` (entries) | 5,373,584 | 4,962,194 (-7.7%) | 4,776,540 (-11.1%) | 4,725,573 (-12.1%) |
| `interpreter_steps` | 107,419 | 119,127 | 189,467 | 192,493 |
| arena `peak_words` | 1,286,716 | 1,518,720 | 1,731,544 | 1,790,644 |
| per-frame hash file (md5) | `c42fdb48` | same | same | same |

`STATE` is `0cf251c3d512ddbb` in all four, and the per-frame hash file is
byte-identical, so the cap only changes which blocks exist, never what the
emulation computes.

| 1800 frames | cap 3 | cap 8 | cap 33 |
| --- | ---: | ---: | ---: |
| `native_calls` | 75,606,464 | 70,406,323 (-6.9%) | 66,849,642 (-11.6%) |
| arena `peak_words` | 3,887,980 (46.3%) | 4,593,820 (54.8%) | 5,333,584 (**63.6%**) |
| `arena recycles` / `evictions` | 0 / 0 | 0 / 0 | 0 / 0 |
| `interpreter_steps` | 1,971,453 | 2,133,335 | 3,057,677 |
| `rebuilds` | 29,748 | 30,398 | 30,913 |
| `STATE` | `dff81882efc1b24a` | same | same |
| per-frame hash file (md5) | `3f7580a9` | same | same |

Uncapped absorption now reaches 63.6% of the 32 MiB arena over 1800 frames
instead of 99.9% of 20 MiB, with no recycle and no sector reuse, and the
per-frame hash file is unchanged. The remaining costs are the +55% of
`interpreter_steps` (1,699 per frame against 1,095) and the +3.9% of rebuilds,
both of which are small in absolute terms, plus the 4.7% of entries that cap 33
saves over cap 8 at 9.3 points more arena -- two thousand entries per frame at
the measured 215 cycles each is about 0.13 ms/frame, which is the reason to take
the whole run rather than a middle cap.

## What this changes and what it does not

Default cap 33 restores the fusion the console A/B measured at -2.5% of
`core_frame_ms total`, now that its measured tail cause is gone. It does *not*
claim that tail is gone: qemu cannot see a 25 ms frame, and the tail was the
reason the cap exists. That is what the next console A/B decides, with the
protocol the fusion review already used: same state, no input, 3-4 pauses, read
`core_frame_ms total`, the per-frame driver counters and the count of samples at
or above 25 ms.

## Verification

- `STATE` and the per-frame hash file are identical to cap 3 for 60 and 1800
  frames, and to the pre-fusion build for the same frame counts, so no
  emulation behaviour depends on the cap.
- `tests/sh3_ppc/run.py`: 846026 cases compiled, all pass.
- Host `USER` time is unchanged (325.8 s against 325.9 s over 1800 frames), as
  expected: qemu's own per-instruction overhead hides the entry-count effect,
  which is why the console is the only place the gain can be measured.

## Console images

The pair for the A/B is the diagnostics flavor of this commit against the
diagnostics flavor of its parent `c23c9876`, built with the same script and the
same baseline. The parent differs only in this macro, so the two images isolate
the cap.

## Limits

Host-only evidence. The 63.6% arena is an idle 1800-frame run from one state; a
played session can compile more and reach the ring, which now costs one 1 MiB
sector rather than the whole cache, but that is the reason to read
`drc_work_arena` in the A/B log rather than to trust the host number.

## Revert

Revert this commit: the `SALVIA_CV1K_FUSE_CAP` default and this document.
