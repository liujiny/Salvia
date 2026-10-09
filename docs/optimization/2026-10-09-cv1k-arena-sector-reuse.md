# CV1000: reuse the code arena one sector at a time instead of flushing it

Baseline for this round: `04245442` (the write-through registers of
`2026-10-09-cv1k-write-through-registers.md`).

## Why

The console A/B in `2026-10-09-cv1k-codegen-shrink-console.md` showed what the
arena does when it fills: the overflow guard discards *every* compiled block
and the guest has to rebuild its whole working set through the interpreter —
17,616 extra compiles in that window, the stall class the frame-time work was
aimed at. Shrinking the emitted code only pushes the overflow later: the
working set still grows about 566 arena words per frame from this state
(3,662,568 words at 1,800 frames, 4,341,764 at 3,000), so it reaches the 20 MiB
cap again a few minutes into a session.

## What changed

The arena is now a **ring of sectors** and a block never straddles a sector
boundary (`SH3_PPC_SECTOR_BYTES`, 1 MiB — `MAX_WORDS` of alignment waste per
sector, under 1%). Entering a sector hands its space out again, so before a
block is compiled into it the table slots that were compiled into it are
dropped, in one bounded pass:

- a `TABLE_SIZE`-byte slot map (`slot_sector`) records which sector each
  block-table slot's code lives in, written by the dispatcher through
  `arena_note_slot()` right after `compile()`;
- reusing a sector scans that 128 KiB map (not the 11 MiB of records), clears
  the lookup tag and zeroes the record of the slots that name it, and counts
  the reuse in a new `arena_evictions`/`arena_evicted_slots` pair in
  `drc_work_arena`;
- a cleared record is just an entry the dispatcher has to compile again, which
  is exactly what a reused sector needs, and because blocks never straddle a
  sector the map is sufficient: no live block can be silently overwritten.

The old all-or-nothing guard is gone: `drc_work_arena recycles` now stays 0
because the arena no longer performs full resets. `slot_sector` is allocated
and freed with the block table and cleared wherever the table is cleared
(`sh3_drc_reset`), and the host dispatch fixture stubs `arena_note_slot()`
because it has no arena.

## Host verification

`ddpdfk` from `user-ddpdfk4-core.state`, 60 frames: `STATE 0cf251c3d512ddbb`
and the per-frame video/audio hash file are byte-identical to
`04245442`; `blocks`, `rebuilds`, `lookups` and every `drc_work_codegen`
phase are identical too, because the arena policy does not change the code
that gets generated, only when it is regenerated. `arena peak_words` rises by
15,928 words, which is the sector alignment padding (1.3% of the 1,199,593
emitted words) — the peak now also includes the skipped sector tails.

Suites: `tests/sh3_ppc/run.py` `PASS 842085 cases compiled 724270 fallback
117815`; `tests/sh3_dispatch/run_inline.py` PASS 8 host suites (optimized and
ASan/UBSan); `tests/sh3_hot_fallback/run.py` PASS; the `Block` layout
contract still compiles with `g++ -m32`.

## Long runs

Same state, no input, report taken at exit; every run's per-frame video/audio
hash file is byte-identical to the others'.

| 6,000 frames | baseline `3336c491` | `04245442` (flush) | this change (ring) |
| --- | ---: | ---: | ---: |
| `drc_work_arena recycles` | 2 | 0 | 0 |
| `drc_work_arena evictions` / `evicted_slots` | - | - | 0 / 0 |
| `drc_work_dispatch rebuilds` | 83,455 | 43,751 | 43,751 |
| compiled blocks | 83,428 | 43,742 | 43,742 |
| `drc_work_arena peak_words` | 5,239,368 (99.93%) | 5,072,584 (96.75%) | 5,148,468 (98.20%) |
| `STATE` | dcddc6e30f936c44 | dcddc6e30f936c44 | dcddc6e30f936c44 |

At 6,000 frames the arena has not yet been reused in either the flush or the
ring build (96.8% and 98.2% full), so the two columns are identical and the
interesting comparison there is against the baseline: the code-size work of the
four earlier rounds has already removed both full flushes and 39,704 rebuilds
(-47.6%).

| 8,000 frames | baseline `3336c491` | `04245442` (flush) | this change (ring) |
| --- | ---: | ---: | ---: |
| `drc_work_arena recycles` | 2 | **1** | **0** |
| `drc_work_arena evictions` / `evicted_slots` | - | - | **3 / 5,845** |
| `drc_work_dispatch rebuilds` | 92,245 | 61,115 | **50,402** |
| compiled blocks | 92,218 | 61,101 | 50,393 |
| `drc_work_arena peak_words` | 5,239,368 | 5,238,884 | 5,238,860 |
| `STATE` | 8f4eeb1794156f21 | 8f4eeb1794156f21 | 8f4eeb1794156f21 |

Between 6,000 and 8,000 frames the arena fills and both policies have to reuse
it. The flush does it once, discarding all 43,742 records and costing 17,364
rebuilds; the ring reuses three 1 MiB sectors, dropping 5,845 records
(1,948 per sector, which is what a 1 MiB sector holds at the measured 515
bytes per block) for 6,651 rebuilds — 2,217 per reuse, 7.8x cheaper per event
than the flush. At 8,000 frames the ring therefore shows 10,713 fewer rebuilds
(-17.5%) than the same code with the flush, and 41,843 fewer (-45.4%) than the
baseline.

The structural point is the shape rather than that total: a session that
reuses the arena now pays ~2,200 recompiles spread over the frames after each
1 MiB reuse instead of ~17,400 in one flush, and the reuse rate is proportional
to the new-code rate rather than to the whole live working set. It also makes
the arena size a smooth knob again — a smaller arena now costs proportionally
more reuses instead of triggering more catastrophic flushes — which is what the
20 MiB knee was really about: `2026-10-07-cv1k-arena-knee.md` records that the
Xenos compositor could not even build its fallback atlas with 32 MiB reserved.

## Arena size is now a smooth knob

Same state, same 8,000 frames, the same build with `SH3_PPC_CACHE_BYTES`
overridden (the block table stays at 131072 entries; every run reports the same
`STATE 8f4eeb1794156f21` and the same per-frame hash file):

| 8,000 frames | 20 MiB (default) | 16 MiB | 12 MiB | 20 MiB, flush build | 20 MiB, pre-shrink baseline |
| --- | ---: | ---: | ---: | ---: | ---: |
| rebuilds | 50,402 | 65,990 | 97,816 | 61,115 | 92,245 |
| rebuilds / frame | 6.3 | 8.2 | 12.2 | 7.6 | 11.5 |
| sector reuses / slots dropped | 3 / 5,845 | 15 / 31,995 | 34 / 71,524 | - | - |
| full resets (`recycles`) | 0 | 0 | 0 | 1 | 2 |
| `peak_words` | 5,238,860 | 4,190,304 | 3,141,936 | 5,238,884 | 5,239,368 |

Shrinking the arena now costs a proportional amount of extra churn instead of
more flush storms: 4 MiB less arena is +15,588 rebuilds over 8,000 frames
(+8.2 per frame, about 0.5% of a frame if a recompile is ~10 us), 8 MiB less is
+47,414 (+12.2 per frame, ~0.7%), and neither configuration performs a single
full reset. Before the ring, `2026-10-07-cv1k-arena-size.md` measured a 16 MiB
arena at 58,511 rebuilds against 43,214 for 20 MiB over 1,800 frames, and
`2026-10-07-cv1k-arena-knee.md` records why the size was pushed up: the Xenos
compositor could not build its fallback atlas with the larger reservation. That
trade is now a smooth curve rather than a cliff, which is the practical result
of this round for the console even though the churn reduction itself is small
per frame.

## Validation limits

Host `qemu-ppc` evidence only. The console A/B that motivated this change is
the one recorded in `2026-10-09-cv1k-codegen-shrink-console.md`; the stall
reduction itself has not been re-measured on hardware, and no console is
available for this round.

These runs also put a number on how much rebuild churn there is per frame: 0.8
to 2.2 rebuilds per frame at 8,000 frames, against 42,458 block entries per
sampled frame on the console. That is the honest limit of this lever: what it
removes is a *burst* (17,364 recompiles in one flush, against 2,217 per sector
reuse), not a large steady cost. If the console stall the earlier documents
attribute to the full reset is real, this change replaces it with something
eight times flatter and proportional to new code; if the stall was really the
dispatcher's cache footprint or the present path, this change does not address
it. Distinguishing those needs the console, and the measurements here cannot.

## Console image

Only the diagnostics flavor was built, as the iteration rule requires. The
image for this commit (`06a89f1c`) is
`543eb7b9c642d90aff11c192e02aafee789832bb4f6c40f65a22b5c9db72aa3a`, 34,631,680
bytes, archived as
`xex-archive/fbneo-20261009-0842-codegen-shrink-ring-diag.xex`, deployed as
`Distro360/fbneo.xex` / `fbneo-diag.xex`, and kept alongside as
`Distro360/fbneo-codegen-shrink-ring-diag.xex`. The matched `3336c491` baseline
image is unchanged, so the same pair covers an A/B when a console is available
again. Pause reports now carry `drc_work_arena ... evictions=... evicted_slots=...`
between `recycles` and `peak_words`.

## Revert

Revert this commit: `sh3_drc_ppc.h`, `sh3_drc_dispatch.h`,
`sh3_drc_work_profile.h`, the `drc_work_arena` line in `sh4.cpp` and the
fixture stub in `tests/sh3_dispatch/test.cpp`.
