# CV1000: sizing a per-store write stamp for validation skips and block links

Baseline for this round: `23a40cf3`. Diagnostics-only change plus this design;
no behavior change.

## Where the next lever is

`2026-10-09-cv1k-delayed-conditional-fallthrough.md` measured two coefficients
on the console: a removed dispatcher entry is worth **67 ns (215 cycles)**, and
generated-code volume is nearly free (-2.1% frame time for +61% words). The last
round also showed what the entry-reduction route costs when taken by *inlining*:
the fused fall-through duplicated the continuation into every predecessor, which
the arena had to absorb (now fixed by the 32 MiB arena). What is left is the
route the rejected link experiment aimed at: remove the dispatcher round trip
without duplicating code, by jumping straight into an already-validated
successor.

A link cannot skip the dispatcher's *validation* unless something cheaper can
say "these bytes have not been written since this block was compiled". The
per-page code epoch
(`2026-10-09-cv1k-code-page-generation.md`) had complete hook coverage --
`epoch_missed = 0` over 80.9M validations -- but was useless because 64 KiB
pages are written 62.6% of the time, so the skip almost never fired.

## The cost side, measured

Any finer-grained signal has to be maintained on the store path, so the first
question is how many guest stores there are. `drc_work_stores` counts, per
sampled entry, how many guest stores the block it ran will perform (every
generated guest store registers exactly one code-write guard, so the
compile-time count is exact):

```
ddpdfk, 60 frames, 4,725,573 entries: per_entry=1 entries=4725573
generated=7,870,421   -> 1.67 store sites per entry, ~131,600 stores/frame
hash file and STATE identical to the build without the counter
```

That is the number that decides the design: at ~131,600 stores/frame, a two to
three word marking sequence costs **0.1-0.3 ms/frame**, against a validation
skip that the attribution sizes at up to 490 cycles per entry
(~1-3 ms/frame) and links worth 215 cycles per entry removed.

## Mechanism

1. **Line stamps.** One byte per 64-byte line of the emulated window,
   16 MiB/64 = 256 KiB, 0 = clean. A store marks the line that holds its first
   byte. Because SH3 stores are size-aligned and 64 is a multiple of every
   access size, **an aligned store can never cross a 64-byte line**, so one
   `stb` is sufficient and no store can be missed. Generated stores add an
   index computation and the store (2-3 words: load the table base, `rlwinm`,
   `stb`); the interpreter's `WB`/`WW`/`WL` add one call, which also covers the
   DMAC and the cheat path, exactly the hook set the epoch round proved
   complete.
2. **Dispatcher fast path.** For a tag hit, read the stamps of the block's first
   and last line (a block is at most 33 halfwords, so at most two lines). If
   both are clean, the snapshot comparison cannot have changed and is skipped;
   otherwise compare as today and, when it matches, clear both stamps. The
   `check_read_map` test stays. A block's own store to its own bytes still
   traps on the existing code-write guard before it runs, which is what keeps
   the "clean" state honest for the block that is executing.
3. **Links (after the skip is proven).** The same stamps license a link: a
   successor record holds the target pc, its generated entry pointer and the
   stamp bytes observed when the dispatcher validated it. An epilogue compares
   that record against the live stamps and `bctr`s into the entry when they
   match, otherwise falls through to the normal exit. Because the record is
   keyed by the *successor*, dropping or recompiling that successor clears it
   with no inverse map, and a recompiled-but-identical block is still valid
   (same source bytes, same code).

## Proof obligations and staging

This is the third attempt at store-side invalidation in this tree; the previous
two are why it is staged rather than written in one go.

Stage A (next): table + both hook sets + the fast path, but **keep the
comparison** and count three things: entries where the stamps were clean (the
skip that would have happened), entries where the stamps were clean *and* the
comparison disagreed (a hole in the hook coverage -- must be 0, the epoch
round's `epoch_missed`), and the store-side word growth. This is the same
cross-check that cleared the epoch mechanism before it was relied on, and it
answers the one open question: what fraction of entries have their own two
lines untouched since compilation. If that fraction is low, the whole route
stops here and no link work is written.

Stage B: rely on the stamps for the skip, verify on the host that `STATE`,
per-frame hashes and the whole suite are unchanged, then console A/B the skip
alone.

Stage C: the linked successor record, static successors (window fall-through,
BRA/BSR) first, then register-indirect targets, each with its own host
verification and its own console A/B. Expected ceiling: if half of the 47,014
entries per frame link, 215 cycles each is ~1.3 ms/frame, minus 0.1-0.3 ms of
marking.

## What this round changed

`drc_work_stores per_entry=<n> entries=<n> generated=<n>` is reported from the
sampled dispatch path, fed by a per-slot byte array written in
`arena_note_slot()` from the compile-time guard count (`slot_stores`, 128 KiB,
allocated and cleared with `slot_sector`). It is diagnostics only: the sampled
specialization reads it, the counters are not part of the dispatch contract,
and the emulation is bit-identical (same `STATE`, same per-frame hash file,
same `native_calls`, same `drc_work_codegen` phases).

## Revert

Revert this commit: `slot_stores`, `sh3_block_stores`, the `drc_work_stores`
counter and report line, the fixture array, and this document.
