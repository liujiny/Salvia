# CV1000 console A/B: the code prefetch backfires, and why

Two diagnostics images, same state, no input, one pause each. Both runs report
the same shadow counters (520,403 answers of 527,183 entries, 59,243 mispredicted
successors), so the workload is identical:

| | base | candidate |
| --- | --- | --- |
| build tag | `shadow-cand 8bed73ba 20261010-2019 diag` | `codetouch-cand 9c1e0e51 20261010-2109 diag` |
| image SHA256 | `ad61b89f90f6877e9cde86f45ab4ed0ef408c9cd93418560bc02243ee10048d8` | `035600f1bc182c6e6ba5925526ebd121ca7fbffba28f718e1723fe562fd01694` |
| usable window | 2479 frames | 2455 frames |

Per frame, with the probe's three spans corrected for its own tick cost (three
ticks per sampled entry, 38 ns each; the corrected spans then sum to
`drc_dispatch` in both windows):

| | base | candidate | delta |
| --- | ---: | ---: | ---: |
| `core_frame_ms cpu_io` | 15.983 | 18.868 | **+2.885 ms** |
| `core_phase_ms drc_dispatch` | 13.870 | 16.722 | +2.852 ms |
| pre-entry | 4.89 | 5.54 | +0.65 |
| **generated block call** | **8.27** | **10.81** | **+2.54 (+31%)** |
| post-entry | 0.58 | 0.86 | +0.28 |
| frontend `game_and_ui` | 17.385 | 20.246 | +2.861 ms |
| frontend `sampled_active_loop` | 18.267 | 20.934 | +2.667 ms |
| `core_sampled_peak_ms` | 21.144 | 24.811 | +3.667 |

## What went wrong

`dcbt` fills the **data** cache. The generated code is fetched through the
**instruction** cache, and PowerPC has no data-cache-touch variant that can warm
it -- `icbt` is a later-ISA instruction whose presence on the Xenon is not
documented, and an unimplemented instruction there is not a wasted fetch, it is
a trap. So the two touches added per entry could not have helped the fetch they
were aimed at, and each one put 256 bytes of a *code* line into L1D/L2, where it
competes with the guest RAM the block is about to read and write.

The block call rising 31% is that competition, and it is also the direct
evidence that the block's 8.27 ms/frame is an **instruction-fetch** cost rather
than a data one: the intervention that added only data-cache traffic to the code
region slowed the block down by a quarter, without changing a single instruction
the block executes.

## What the pair still establishes

- The accepted shadow record's numbers reproduce: 98% of entries answered,
  pre-entry 4.89 ms/frame against 7.21 for the build without the shadow, and the
  same counters in both windows of this pair.
- The generated code is the largest single piece of the dispatch phase and it is
  *instruction* fetch. That rules out the whole class of data-side remedies for
  it and leaves reducing the code itself: fewer lines fetched per entry.
- A negative result here costs one round and no image: the revert puts the tree
  back to the accepted `8bed73ba` content.

## Next

The hot words of a block are 85 of 167.5, and `memaddr` is 39.8 of them -- per
memory access, roughly one word of address arithmetic and six of guards for one
word of access. The guards test, for every access, that the effective address is
an accepted alias inside the registered window (and aligned for wider
accesses); every access off the same guest register repeats that test with only
the displacement changed. Hoisting it to the base register -- validate the
register's range and alignment once for the block, then let each access reuse it
-- would be sound (a stricter test implies the per-access one) and is the
largest remaining reduction in fetched code, but it has to be measured before it
is built: the numbers to want are how many accesses per block share a base
register with a bounded displacement, and how many guard words that elides.

## Revert

This document records the revert of `9c1e0e51`; reverting that revert restores
the code prefetch and its own document.
