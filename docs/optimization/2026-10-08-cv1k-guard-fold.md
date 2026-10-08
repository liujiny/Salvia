# CV1000: fold the alignment guard, cheapen the watched read, share the tail once more

Baseline for this round: `8635834f` (the shared completion tail and the RAM
base register of `2026-10-08-cv1k-codegen-shrink.md`).

## What changed

Three more reductions in the SH3 PPC emitter, all in the direct-RAM path or the
block completion:

1. **The area-select guard and the alignment guard are one compare.** The
   direct path used to emit `((addr >> 29) & 7) != 7` (reject the internal
   area ≥ `e0000000`) and then, for word and long accesses, `(addr & (size-1))
   == 0` as two separate rotate/compare/branch triples. Bits 29..31 of the
   effective address are its area select and bits 0..1 its alignment: one
   `rlwinm` with a five-bit (or four-bit for halfwords) mask places both in a
   single field, where the value is at most 6 exactly when the area bits are
   not all ones and the alignment bits are zero — an alignment bit contributes
   8x to the field, so it clears the window on its own. Word and long accesses
   therefore need one rotate, one `cmpli` and one branch where they needed two
   of each before. Byte accesses keep the area guard alone, as they have no
   alignment requirement. The two guards now sit in a different order, but
   they share one exit, so exactly which pattern fails first is not observable.
2. **The watched-longword check compares directly.** `MOV.L` reads on the
   direct path must hand the watched longword to the handler. The check was
   `imm(0, watch - start); cmpl` — materialising a 32-bit constant to compare
   against a value that is already at most `span-1`. When the offset fits the
   16-bit `cmpli` immediate (CV1000's watch sits 0x2310 into the window) that
   is now a single instruction; the materialising form remains for offsets
   that do not fit.
3. **The folded delay-slot completion shares the tail.** `finish_delay(false)`
   carried its own `charge` + `load pc` + `store ppc` + return. It now loads
   the pc into r0, passes the cost in r12 and the result in r5, and jumps to
   the same shared tail the guard exits and the plain completion use. The
   loop-back variant keeps its inline `charge(cycles)`, because that path reads
   the remaining budget out of r11.

## Host measurement

`ddpdfk` from `user-ddpdfk4-core.state`, 60 frames, dips `00,07,00,00`,
`render_cores=2`, 9,323 compiled blocks, `rebuilds=9327` and
`STATE 0cf251c3d512ddbb` in every run; the per-frame video/audio hash file is
byte-identical to the baseline:

| Emitted words | baseline `3336c491` | `8635834f` | this change |
| --- | ---: | ---: | ---: |
| body | 209,455 | 212,781 | 212,781 |
| memaddr | 484,218 | 428,686 | 376,502 |
| memguard | 135,965 | 135,965 | 102,374 |
| memaccess | 103,997 | 103,997 | 103,997 |
| complete | 188,189 | 152,608 | 100,954 |
| exit | 566,257 | 375,975 | 384,462 |
| **total** | **1,688,081** | **1,410,012** | **1,281,070** |
| words / block | 181.07 | 151.24 | 137.40 |
| `drc_work_arena peak_words` | 1,702,184 | 1,424,320 | 1,294,692 |

This round removes 128,942 words (-9.1%); cumulative from the pre-shrink
baseline that is 407,011 words (-24.1%) and a 24.0% smaller arena high-water
mark. `memguard` falls by 33,591 words because word and long accesses emit one
guard branch fewer; `complete` falls by 51,654 because the delay-slot
completion no longer duplicates the charge/return, while `exit` rises by 8,487
because blocks that only completed through that path now need the shared tail.

## Verification

- `tests/sh3_ppc/run.py`: `PASS 842085 cases compiled 724270 fallback 117815`,
  the same counts as the recorded baseline, including the direct-RAM,
  alias, guarded-delay-slot, code-write and registered-device sections.
- `tests/sh3_dispatch/run_inline.py`: PASS 8 host suites (optimized and
  ASan/UBSan). `tests/sh3_hot_fallback/run.py`: PASS. The `Block` layout
  contract still compiles with `g++ -m32`.
- 60-frame `ddpdfk`: `STATE 0cf251c3d512ddbb` and byte-identical per-frame
  hashes.

## Long run

Same state, no input, report taken at exit. Every run's per-frame video/audio
hash file is byte-identical to the baseline's, and every `STATE` matches.

| | baseline `3336c491` | `8635834f` | this change |
| --- | ---: | ---: | ---: |
| 1,800 frames `peak_words` | 5,127,536 (97.8%) | 4,318,532 (82.4%) | 3,912,044 (74.6%) |
| 1,800 frames `recycles` | 0 | 0 | 0 |
| 1,800 frames `rebuilds` | 30,541 | 30,541 | 30,541 |
| 3,000 frames `peak_words` | 5,239,368 (99.93%) | 5,118,948 (97.6%) | 4,635,120 (88.4%) |
| 3,000 frames `recycles` | 1 | 0 | 0 |
| 3,000 frames `rebuilds` | 56,553 | 36,948 | 36,948 |
| 3,000 frames `STATE` | d0a3e4101ebe7de5 | d0a3e4101ebe7de5 | d0a3e4101ebe7de5 |

The 3,000-frame high-water mark is now 88.4% of the arena, so the overflow guard
that the baseline trips inside this window has 11.6% of the arena as headroom
instead of 0.07%.

## Validation limits

Host `qemu-ppc` evidence only; no console frame rate or stall count is claimed.

## Revert

Revert this commit: `sh3_drc_ppc.h` only.
