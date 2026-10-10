# CV1000: sizing the block link before building one

Baseline `9234e60c`. Diagnostics only (`SALVIA_CV1K_LINK_PROBE`, default 0);
the emulation is untouched.

## Why

The console A/B of `2026-10-10-cv1k-fusion-cap-console.md` closed the last way
of cutting entries. What is left in the frame is the per-entry work: 44,288
entries per frame, ~6.7 ms of pre-entry and ~5.6 ms of generated code, out of a
16.3 ms core frame. The pre-entry span is ~480 cycles per entry with only ~10
words of comparison in it, so it is cache lines.

Two things had to be measured before writing a link (the change two earlier
attempts died in), and both are cheap counters:

1. how many entries directly follow a block that ended at its own sequential
   completion -- the population the simplest link variant can serve;
2. how much of the `lookup[]` set array the workload actually touches, because
   that is the line such a link would stop reading (the set is 32,768 x 20 B =
   640 KiB, randomly addressed).

## Measurement (60 frames, host harness)

| reading | value |
| --- | ---: |
| entries observed (sampled specialization) | 5,418,967 |
| of those, successors of a sequential completion | 1,024,259 (**18%**) |
| lookup sets | 32,768 (655,360 bytes) |
| lookup sets touched | 6,113 (**122,260 bytes**) |

`STATE 0cf251c3d512ddbb` and the per-frame hash file are unchanged.

## What it settles

- **The lookup-set line is not the miss.** 122 KiB of set array is touched over
  the whole run, which is L2-resident on a Xenon (1 MiB L2). A link that only
  avoids the set lookup would save an L1 hit, ~10-20 cycles per entry, under 1%
  of the frame -- not worth the machinery. This also explains from the other
  side why the earlier link attempt that skipped the probe moved nothing.
- **The population of the sequential-only variant is 18%**, because the block
  length histogram (`2026-10-08-cv1k-phase-split-result.md`) says most blocks
  end at a branch, not at the window or the fusion cap. That variant is dead on
  population as well as on saving.
- **What actually misses** is the `Block` record (34k records spread over
  131,072 slots, 2.6 MiB of touched lines, against a 1 MiB L2) and the source
  bytes. So a link is only worth building if it avoids the *successor's record
  read* -- that means it has to carry the target's code address and everything
  needed to validate it, rather than pointing at a record to check.

## The design that follows

A per-record link: `link_pc`, `link_entry`, `link_sector`, filled by the
dispatcher's slow path (which already knows both the block it just entered and
the block it came from, since the chain loop keeps the previous entry) and read
by the predecessor's epilogue as four checks:

| check | what it catches | cost |
| --- | --- | --- |
| `next_pc == link_pc` | a different exit of the same block | register compare |
| the two write stamps for the target's lines are clean | the guest wrote the code | 1-2 bytes from the landed stamp table |
| `sector_gen[link_sector]` unchanged | the arena ring reused that sector (the only way code is freed) | 32-entry array, 1 line |
| the target page is the one baked in at fill time | a fetch-map change | hot 16 KiB fetch map |

The soundness argument is the property the existing design already relies on:
validation compares *bytes*, not code identity, and the arena never frees code
except by sector reuse -- so a live code address that is no longer the record's
current translation is still a translation of the same unchanged bytes, and is
equivalent. The sector generation is the one thing that says the code is gone,
and it bumps only when the ring wraps a 1 MiB sector, not on the ~17 recompiles
per frame (all of which are slot conflicts, `rebuild_causes source=0`).

Expected saving per linked transition: the successor's record line (the missing
one) plus the source comparison, ~200-300 cycles of the ~480 the pre-entry costs.

## Next

Stage A: add the link fields and have the dispatcher's slow path fill them, then
have the host run count how often the four checks *would* pass, compared against
what the dispatcher actually did on those entries -- the same audit shape that
cleared the write stamps. No behavior change, host-verifiable. Stage B acts on
it, and then a console A/B decides, with the same protocol as this round.

## Revert

Revert this commit: `lookup_touched`, the `link_*` counters and their report
line, `SALVIA_CV1K_LINK_PROBE`, the fixture additions, and this document.
