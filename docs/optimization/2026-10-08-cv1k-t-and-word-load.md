# CV1000: one-word T updates and sign-extending halfword loads

Baseline for this round: `75098ef7` (the guard fold of
`2026-10-08-cv1k-guard-fold.md`).

## What changed

Two one-word reductions, both of which also remove an executed instruction:

1. **The SH3 `T` flag update is one instruction.** `T` is the low bit of
   `G_SR`. `set_t(-1)` used to clear it (`rlwinm`) and then OR in r0's 0/1
   (`or`), and `set_t(1)` cleared it and then set it (`ori`). Since compare_t
   and the shift emitters leave their result in r0's low bit and nowhere else,
   one `rlwimi sr,r0,0,31,31` replaces the clear-then-OR pair, one `ori`
   replaces clear-then-set, and only the "clear T" case still needs the masked
   move. Every `T`-setting SH3 instruction (comparisons, `TST`/`AND`/`OR`/
   `XOR`, shifts, `SETT`/`CLRT`) is one word shorter.
2. **Halfword loads use `lha`.** Every SH3 word load sign-extends (the
   interpreter reads `(INT32)(INT16) RW(...)`), and PowerPC's load-halfword-
   algebraic is exactly that load, so `lhz` + `extsh` collapses into one `lha`
   with the same register and displacement form. Byte loads keep `lbz` plus
   `extsb`, because PowerPC has no byte-algebraic load.

## Host measurement

`ddpdfk` from `user-ddpdfk4-core.state`, 60 frames, dips `00,07,00,00`,
`render_cores=2`, 9,323 compiled blocks, `rebuilds=9327` and
`STATE 0cf251c3d512ddbb`; the per-frame video/audio hash file is byte-identical
to the baseline's:

| Emitted words | baseline `3336c491` | `75098ef7` | this change |
| --- | ---: | ---: | ---: |
| body | 209,455 | 212,781 | 206,901 |
| memaddr | 484,218 | 376,502 | 376,502 |
| memguard | 135,965 | 102,374 | 102,374 |
| memaccess | 103,997 | 103,997 | 101,095 |
| complete | 188,189 | 100,954 | 100,954 |
| exit | 566,257 | 384,462 | 384,462 |
| **total** | **1,688,081** | **1,281,070** | **1,272,288** |
| words / block | 181.07 | 137.40 | 136.46 |
| `drc_work_arena peak_words` | 1,702,184 | 1,294,692 | 1,286,116 |

This round removes 8,782 words (-0.7%); cumulative from the pre-shrink baseline
that is 415,793 words (-24.6%).

## Verification

- `tests/sh3_ppc/run.py`: `PASS 842085 cases compiled 724270 fallback 117815`,
  the same counts as the recorded baseline. That suite includes the signed
  byte/halfword load sections and every translator that writes `T`.
- 60-frame `ddpdfk`: `STATE 0cf251c3d512ddbb` and byte-identical per-frame
  hashes.

## Long run

Same state, no input, report taken at exit; every per-frame hash file matches
the baseline's and every `STATE` matches.

| | baseline `3336c491` | `75098ef7` | this change |
| --- | ---: | ---: | ---: |
| 1,800 frames `peak_words` | 5,127,536 (97.8%) | 3,912,044 (74.6%) | 3,885,784 (74.1%) |
| 1,800 frames `recycles` | 0 | 0 | 0 |
| 1,800 frames `rebuilds` | 30,541 | 30,541 | 30,541 |
| 3,000 frames `peak_words` | 5,239,368 (99.93%) | 4,635,120 (88.4%) | 4,604,344 (87.8%) |
| 3,000 frames `recycles` | 1 | 0 | 0 |
| 3,000 frames `rebuilds` | 56,553 | 36,948 | 36,948 |
| 3,000 frames `STATE` | d0a3e4101ebe7de5 | d0a3e4101ebe7de5 | d0a3e4101ebe7de5 |

## Validation limits

Host `qemu-ppc` evidence only; no console frame rate or stall count is claimed.

## Revert

Revert this commit: `sh3_drc_ppc.h` only.
