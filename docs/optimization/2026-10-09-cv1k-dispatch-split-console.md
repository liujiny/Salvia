# CV1000 console: the dispatch phase is metadata stalls, not generated code

Two diagnostics images, the same state, no input, `ddpdfk`,
`dips=00,07,00,00`, `render_cores=2`, one usable window each (the first window
of both runs is the 14-20 frame cold state load and is excluded):

| | baseline | this image |
| --- | --- | --- |
| build tag | `codegen-shrink-base 3336c491 20261008-2323 diag` | `dispatch-probe-split 508870c5 20261009-0904 diag` |
| image | `fbneo-codegen-shrink-base-diag.xex` (`3faa500e...`) | `fbneo.xex` (`5c5e0650...`) |
| window | cumulative 20..3541 (3521 frames, 52 timing samples) | cumulative 14..3523 (3509 frames, 52 timing samples) |

The windows are workload-matched: per frame, `worker_jobs` 0.935 against 0.935,
`timer_callbacks` 36.06 against 36.07, `dispatch_calls` 1447.4 against 1449.8
(+0.2%), sampled `native_guest_cycles` +1.4%, idle-service cycles -0.5%.

## The attribution

The probe's three spans sit on the same 1-in-64 sample (735.9 samples per
frame); scaling each by 64 and subtracting the probe's own tick latency
(47 ns per span) gives, with `drc_dispatch` at 14.587 ms/frame:

| Dispatch phase | per frame | per block entry | share |
| --- | ---: | ---: | ---: |
| pre-entry: `MemMapF` fetch, lookup, source validation, record access, recompile | 7.09 ms | 484 cycles | 49% |
| generated block call (`blk_entry`) | 5.67 ms | 387 cycles | 39% |
| post-entry: service dispatch and cycle accounting | 0.46 ms | 31 cycles | 3% |
| chain loop and untimed remainder | 1.37 ms | 93 cycles | 9% |
| **total** | **14.587 ms** | **995 cycles** | 100% |

The number that matters: the source validation compares 426,563 words per
sampled frame over 46,922 entries — 9.1 words, 18 bytes, per entry — yet the
pre-entry span costs 484 cycles per entry. That is not instruction cost, it is
the cache line traffic around it: the `MemMapF` entry, the 20-byte lookup set,
the 84-byte `Block` record and its 66-byte snapshot, and the source bytes. On a
Xenon with 1 MiB of L2 and an 11 MiB block table behind a 20 MiB arena, three
or four of those lines miss on most entries.

That also explains the two results that had no explanation before: the
emitter-link experiment removed 22-25% of the entries and changed the phase by
2.4%, because removing entries does not shrink the working set the remaining
ones touch; and the code-size work moved the frame time much less than it moved
the code size, because the generated code is only the second-largest piece.

## What this pair also shows

| | baseline | this image |
| --- | ---: | ---: |
| `core_frame_ms cpu_io` | 16.421 | 16.254 (-1.0%) |
| `core_frame_ms total` | 17.442 | 17.268 (-1.0%) |
| `core_phase_ms drc_dispatch` | 14.80 | 14.587 (-1.4%) |
| `blk_entry` per frame, raw 1-in-64 (same sampling) | 0.1350 ms | 0.1233 ms (-8.7%) |
| frames at or above 25 ms | 2 of 52 | 0 of 52 |
| `core_sampled_peak_ms` | 25.996 | 24.039 (-7.5%) |
| `drc_work_arena recycles` | 1 | 0 |
| `drc_work_arena peak_words` | 5,239,012 (99.9%) | 3,837,572 (73.2%) |
| `drc_work_dispatch rebuilds` per sampled frame | 15.2 | 4.7 |
| `drc_work_codegen` words per block | 169.4 | 117.0 (-31%) |

The code-size work plus the ring means a ~3,500 frame session no longer fills
the arena at all (73.2%), so the ring was not exercised here
(`evictions=0`); that is itself the result the arena work was for. The frame
time gain is 1.0% on matched work, and the image carries roughly 0.08 ms/frame
of probe overhead, so the honest reading of the two A/Bs is that the code-size
work is worth 1-3% of frame time, not more — consistent with the attribution
above.

## Next lever, now evidence-backed

1. **Skip the per-entry source validation with a store-side code epoch.**
   `2026-10-08-cv1k-emitter-block-link.md` already built the compiled-page
   bitmap and epoch (a generated store bumps the epoch when it writes a page
   that holds compiled code; interpreted stores, DMA and state loads do the
   same) and reverted it bundled with the block link. Without the link it is
   worth the validation's share of the 484-cycle pre-entry span, minus the
   store-side check — a low-single-digit millisecond per frame, and unlike the
   link it removes work instead of moving it.
2. **A compact hot dispatch record.** The checks need 18 bytes per entry
   (source, pc, words, cycles, check_read_map, entry) while the record is 84
   bytes and carries a 66-byte snapshot, so a hot entry touches two lines.
   Splitting the snapshot into a parallel array would put three or four entries
   on one line, but `Block`'s size and offsets are asserted by
   `tests/sh3_block_layout/contract.cpp` and by the dispatcher tests, so that
   is a contract decision, not a free change.

## Limits

The `x64` scaling and the tick-latency correction put roughly +-1.5 ms of
uncertainty on each row of the attribution; the ordering (pre-entry > generated
code >> post-entry > loop remainder) is robust, the exact split is not. The
probe drops the samples that leave the loop early (`no_entry`,
`short_budget`, ~1.8% of entries), which biases all three spans slightly
upward, and the pre-entry span includes any recompile that lands on a sampled
entry. Both images are diagnostics builds; no release image was built and no
FPS number is claimed or derivable from these sampled means.

## Revert

Revert this commit; it adds only this document.
