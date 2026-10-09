# CV1000: cap how much of the fall-through the fusion absorbs

Baseline for this round: `2015c540`. One logical change: a compile-time cap in
the delayed-conditional fall-through, plus this review.

## Why

The console A/B of `2026-10-09-cv1k-delayed-conditional-fallthrough.md` won on
the mean (`core_frame_ms total` 17.233 -> 16.801) but lost on the tail: the same
52-sample window that had no frame at or above 25 ms in the baseline had one at
25.55 ms with the fusion. The pause report already named the cause:

| per 3,524-frame window | baseline | fusion |
| --- | ---: | ---: |
| block entries | 5,658,274 | 4,725,573 (-16.5%) |
| generated words | 3.76M | 6.05M (+61%) |
| `interpreter_steps` | 13,082 | 21,781 (+66.5%) |
| arena `peak_words` | 73.2% | 99.9% |
| frames >= 25 ms | 0 of 52 | 1 of 52 |

The fusion was absorbing the whole straight-line run after the delay slot, so
that run was compiled into every predecessor: the code growth and the extra
budget-boundary interpreter fallbacks both come from the *length* of what is
absorbed, not from the fusion idea itself.

## Change

`SALVIA_CV1K_FUSE_CAP` (default 3) bounds how many instructions past the delay
slot the fused block may still absorb. The compile loop ends the block at the
ordinary sequential completion when the budget runs out, so everything past the
cap is compiled once as its own block instead of being duplicated into every
predecessor. Setting it to 0 restores the pre-fusion behaviour, which is the
control point of the sweep below.

## Sweep

`ddpdfk` from `user-ddpdfk4-core.state`, `dips=00,07,00,00`, `render_cores=2`,
no input, 60 frames, one build per cap (the cap is a `-D`, so the sources are
identical):

| cap | entries | vs cap 0 | generated words | vs cap 0 | interpreter steps | arena peak_words |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 5,658,274 | -- | 1,203,250 | -- | 106,385 | 1,233,188 |
| 1 | 5,655,844 | -0.04% | 1,214,541 | +0.9% | 105,345 | 1,244,224 |
| 2 | 5,489,878 | -3.0% | 1,227,605 | +2.0% | 105,329 | 1,256,872 |
| **3** | **5,373,584** | **-5.0%** | **1,257,470** | **+4.5%** | **107,419** | **1,286,716** |
| 4 | 5,294,962 | -6.4% | 1,291,618 | +7.3% | 108,873 | 1,325,292 |
| 8 | 4,962,194 | -12.3% | 1,484,438 | +23.4% | 119,127 | 1,518,720 |
| 33 | 4,725,573 | -16.5% | 1,752,496 | +45.6% | 192,493 | 1,790,644 |

Reading:

- **Cap 1 saves nothing** (-0.04%): absorbing only the delay slot moves the
  next entry from `pc+2` to `pc+4`, so no dispatcher round trip is removed.
  This confirms the earlier reasoning that the entry saving comes from
  absorbing past the slot.
- **The cost is superlinear in the cap**: caps 2-4 buy 3-6% of entries for
  2-8% more code and at most +2% interpreter steps, while cap 33 buys the last
  10% of entries for +45% code and +81% interpreter steps. The default of 3 is
  the point where the entry saving is real and the two costs the console
  measured as tail risk are still within a couple of percent.
- Per-entry cost is unchanged: the cap moves *which* addresses are compiled,
  not what a compiled block does.

## Verification

- `tests/sh3_ppc/run.py`: the sweep above ran the full differential suite for
  the default cap; every case passes with the same `STATE` and per-frame hash
  file as the uncapped build for the same frame count (see `Limits`).
- 1800 frames with the default cap: to be recorded with the next console A/B.

## Limits

Host-only evidence for the sweep (qemu-ppc). The cap is a heuristic: it does
not model what the console's caches do with a flatter block, so the choice of 3
is "the point where the measured costs are small", not a measured optimum. The
console A/B that decides it is the same protocol as before: same state, no
input, 3-4 pauses, read `core_phase_ms drc_dispatch` (`dispatch_pre`,
`blk_entry`) plus `core_frame_ms total` and the >=25 ms sample count.

## Revert

Revert this commit: it adds the macro, the budget fields and loop check, the
default, and this document.
