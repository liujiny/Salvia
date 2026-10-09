# CV1000: per-page code generations -- implemented, measured, rejected

Baseline for this experiment: `5261b91`. The console attribution in
`2026-10-09-cv1k-dispatch-split-console.md` put 7.09 ms/frame (49% of the
dispatch phase, 484 cycles per entry) in the *pre-entry* span, with only 18
bytes of source comparison per entry, so the cost is cache lines rather than
instructions. This round implemented the obvious way to remove one of those
lines -- a per-page code generation that lets the dispatcher skip the source
comparison entirely -- and then measured whether the mechanism's invalidation
is precise enough to be worth anything.

## Mechanism

- `code_page_gen[]`: one generation counter per 64 KiB guest page, indexed
  `(address & AM) >> SH3_SHIFT` so every external alias folds onto one page.
- Every writer that can change bytes a block was compiled from bumps it: the
  emitter appends five words to every generated store (`rlwinm` to the page
  index, load the array base from the state, `lwzx`, `addi`, `stwx`), and the
  interpreter's `WB`/`WW`/`WL` call `code_page_touch()`, which also covers the
  DMAC (it writes through `WL`) and the cheat path (`cheat_write` routes
  through `WB`). State loads (`sh4.cpp:604`), mapping changes and the arena's
  sector reuse already drop the affected records outright.
- `Block` carries `code_gen`, the generation of its own page at compile time.
  It is four bytes appended after the snapshot, so every member offset is
  unchanged; the record goes 84 -> 88 bytes and the 131072-record table
  11 MiB -> 11.5 MiB. `tests/sh3_block_layout/contract.cpp` and the layout
  regression were updated with that rationale.
- This build **keeps the source comparison** and only counts the two cases
  (`drc_work_rebuild_causes epoch_stale=... epoch_missed=...`): entries where
  the generation says the page was written since the block was compiled, and
  entries where the generation still matches while the snapshot does not
  (which would be a missed invalidation -- a write path that is not hooked).

## What the cross-check found

`ddpdfk` from `user-ddpdfk4-core.state`, 60 frames, all suites pass
(`sh3_ppc` 842085 cases with the recorded counts, `sh3_dispatch` 8 suites,
hot-fallback, the layout regression and the `-m32` contract), and `STATE`
`0cf251c3d512ddbb` plus the per-frame hash file are byte-identical: the
mechanism does not change behaviour, only the counters.

| 60 frames | value |
| --- | ---: |
| source validations | 5,702,610 |
| `epoch_stale` (page written since the block was compiled) | 3,396,507 |
| `epoch_missed` (generation clean but the snapshot changed) | **0** |

Over 1,800 frames of the same state the same build reports 80,898,296
validations, `epoch_stale` 50,671,366 (62.6%) and `epoch_missed` still 0, with
`rebuilds` 30,541, `STATE dff81882efc1b24a` and the per-frame hash file
identical to the run without the mechanism.

Two things follow, one reassuring and one fatal:

- The hook coverage is complete: over 5.7M validations the generation never
  said "clean" while the snapshot had changed, across the emitter, `WB`/`WW`/
  `WL`, the DMAC and the cheat path. The mechanism is *safe* as designed.
- It is useless here. 59.6% of entries (62.6% over the longer run) are on a
  page that has been written since the block was compiled, so skipping the
  comparison on a clean generation would only help the other ~40%, while
  costing five extra words per generated store (~6% more emitted code) plus a
  0.5 MiB table. That is not a trade; the comparison is the precise mechanism
  and 64 KiB pages are far too coarse for this game.

The reason is structural and worth keeping: CV1000 programs live in the same
16 MiB RAM window as their data, the compiled working set is ~16 MiB of guest
code, and code and variables therefore share 64 KiB pages. Any
page-granular invalidation -- this one, or a dirty bitmap, or a per-page
timestamp -- is too coarse here. It also explains the earlier emitter-link
result from the other side: the link used the same page+epoch idea, where a
false positive is harmless (it only declines to link), which is exactly why it
was safe there and unusable here.

## Consequence for the next lever

With page-granular invalidation ruled out, the per-entry cost stands at 995
cycles for an average of 6.8 guest cycles of work, and the levers left are the
ones that reduce *entries* or the *lines each entry touches*:

1. **Longer blocks** (block fusion / superblocks): the only lever that reduces
   the 484-cycle pre-entry cost per unit of guest work. The emitter-link
   experiment is the cautionary tale -- it removed dispatcher round trips but
   added guard work to every linked epilogue and came out neutral -- so a
   fusion design has to add less per fused block than it removes per skipped
   entry. Validation caps a block's snapshot at 33 halfwords, so a fused block
   needs its snapshot moved out of the fixed record into its own array, which
   also lets three or four hot entries share one cache line.
2. **Hot-record compaction**: 18 bytes of the 88-byte record are what the
   dispatcher reads for its checks (source, pc, words, cycles, check_read_map,
   entry); the snapshot follows in the same two lines. Reordering so the hot
   bytes fill one line is a layout-contract decision, not a free change.

## Revert

Reverted in the following commit: the generation arrays and hooks, the
`Block::code_gen` field with the 84 -> 88 byte change, the two counters and the
test updates. Only this document stays.
