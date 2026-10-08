# CV1000 Xbox 360: the frame is the SH3 dispatch path, not the GPU

Source checkpoint: `7f95baea` core plus the `core_phase_ms` probe build
(`e98f808d`). Artifact: 34,631,680 bytes, SHA256
`f7aadb1bcb307cbdb757aa261ca6b1307b983cd3aa78340c0b3b69401ed91875`.

Two console windows were captured with that image: `ddpdfk` loading
`ddpdfk.state4` and `ddpsdoj`. Both use `dips=00,07,00,00`,
`clock_hz=102400000`, `render_cores=2`, compositor `sprite-attributes`.
Every window reports `dominant_cpu_io` for its slow frames.

## Steady-state windows

The first window of each log contains one 97 ms frame (state load / cold DRC)
among one to three samples, so only the long windows are usable.

| Reading | ddpdfk (589 frames, 11 samples) | ddpsdoj (646 frames, 12 samples) |
| --- | ---: | ---: |
| `core_frame_ms` `prep` | 0.003 | 0.004 |
| `core_frame_ms` `cpu_io` | **10.888** | **17.593** |
| `core_frame_ms` `audio_tail_misc` | 0.170 | 0.164 |
| `core_frame_ms` `draw_sync` | 1.744 | 0.733 |
| `core_frame_ms` `total` | 12.805 | 18.494 |
| `core_frame_ms` `sampled_peak` | 29.857 | 25.447 |
| `frontend_frame_ms` `game_and_ui` | 13.137 | 18.890 |
| `frontend_frame_ms` `present` | 4.402 | 0.730 |
| `frontend_frame_ms` `sampled_active_loop` | **17.660** | **19.783** |

`cpu_io` is again about 85-95% of the frame in both games.
The whole-iteration number is `sampled_active_loop`, not `core_frame_ms total`:
the frontend adds `present` on top of the core, so ddpdfk runs at about 17.7 ms
(57 fps) and ddpsdoj at about 19.8 ms (51 fps) in these windows.

## `cpu_io` tracks the dispatch count

The two games differ in workload, which makes the scaling visible:

| | ddpdfk | ddpsdoj | ratio |
| --- | ---: | ---: | ---: |
| `native_calls` / frame | 32,185 | 52,296 | 1.625 |
| `cpu_io` ms / frame | 10.888 | 17.593 | 1.616 |

`cpu_io` is proportional to the number of dispatched blocks at about **1,080
host cycles each**, with the idle service, the timers and the blitter join all
held constant. The dense-scene slowdown is therefore the same cost, just with
more blocks: a scene at roughly 30 fps is dispatching about 2.7 times as many
blocks per frame as the 17.7 ms window above.

That makes the target unambiguous: host cycles per dispatched block. Only about
250 of the 1,080 are the generated code itself, so even a free code generator
would save under a quarter. The remaining 830 cycles are the dispatch
machinery, and about 150-200 of those are the literal instructions of the
dispatcher, which leaves 600 or more as memory stalls.

## What the probes attributed, per frame

Timings are the printed window totals divided by the frame count. The
1-in-64 sampled spans are scaled by 64 for comparison, but their own probe
overhead is not scaled, so those two rows carry an estimate.

| Probe | ddpdfk / frame | ddpsdoj / frame | Scale |
| --- | ---: | ---: | --- |
| `worker_busy` (worker thread) | 6.11 ms | 5.18 ms | 358 jobs / 10.05 ms each |
| `blit_write` (main thread) | 0.223 ms | 0.315 ms | 358 / 545 calls |
| `movll_service` | 0.17 ms | 0.17 ms | 1-in-64 x64 |
| `timers` | 0.095 ms | 0.099 ms | every slice, 240/frame |
| `blk_entry` | 6.41 ms | 9.55 ms | 1-in-64 x64 |

Two conclusions follow directly.

1. **The blitter worker is not the critical path.** It is busy 6.1 ms out of a
   12.8 ms frame, but the emulation thread only ever joins it for 0.223 ms per
   frame (`blit_write`), and the explicit post-frame wait is 1.744/0.733 ms
   (`draw_sync`). Batching, page capacity, resolve bounds and readback work all
   live on that 0.2 ms slice, which is why earlier GPU-side work did not move
   the frame rate.
2. **The idle service and the timers are negligible.** 0.17 ms and 0.095 ms per
   frame rule out the `Sh3BurnCycles` wait loop and `sh4_run_timers` as targets.

`blk_entry` needs care. It samples two `QueryPerformanceCounter` calls around
one generated block, and a QPC pair costs roughly the same order as a block, so
the 1-in-64 total is mostly probe cost. Using the printed `movll_service`
sample (127 ns per sampled service) to bound QPC overhead at about 94 ns per
pair leaves roughly 80 ns of real block work: about **2.6 ms/frame (ddpdfk) and
3.5 ms/frame (ddpsdoj)**, i.e. only a quarter to a third of `cpu_io`.

The remaining roughly 8 ms (ddpdfk) and 14 ms (ddpsdoj) of `cpu_io` is the DRC
dispatch path itself: block lookup, source validation, the block call, the
interpreter fallback and the 240-slice outer loop.

## Why the dispatch path costs that much

`drc_work` for the same windows:

| Reading | ddpdfk | ddpsdoj |
| --- | ---: | ---: |
| `native_calls` / frame | 32,185 | 52,296 |
| `native_guest_cycles` / frame | 312,147 | 509,920 |
| `lookups` / frame | 32,534 | 52,905 |
| `validation_words` / frame | 289,450 | 488,783 |
| `drc_work_arena` peak | 3,193,788 words (12.8 MiB) | 3,249,832 words (13.0 MiB) |

So a dispatched block carries about **9.7 guest cycles** and the emulation
thread spends about **1,082 host cycles per dispatch** (34.8 M cycles over
32,185 blocks), against roughly 250 cycles of actual generated code.

The per-dispatch metadata is spread over three independent large regions, each
randomly addressed, while the Xenon core has a 1 MiB L2 and no L3:

| Structure | Size | Per dispatch |
| --- | ---: | --- |
| `blocks` (131,072 x 84-byte `Block`) | 11 MiB | one record, usually two cache lines |
| `lookup` (32,768 x 20-byte `Lookup`) | 640 KiB | one line |
| generated code arena (20 MiB cap) | 12.8-13.0 MiB used | one or more lines |

Three to four cold lines at roughly 200-300 cycles each is the same order as the
unexplained 800 cycles per dispatch. The generated code itself is not the
problem; the metadata traffic around it is.

## Candidate next steps, in order of expected yield

1. **Link blocks directly.** The generated epilogue already knows the next PC
   for a direct branch. Jumping straight to the successor's entry would remove
   the lookup, the validation and the `Block` record access from the common
   path. This is the only change that removes most of the cold-metadata traffic,
   and it is also the most invasive: the current design deliberately revalidates
   every entry, including successors, so a link needs its own invalidation
   rule for guest self-modifying writes, DMA and cheat writes.
2. **Shrink the `Block` record to one cache line.** Moving the 66-byte
   `original[]` snapshot next to the generated code it validates would cut the
   table from 11 MiB to about 3 MiB and make validation share the lines the
   block is about to fetch. Emulation-neutral and host-verifiable, but the
   expected gain is smaller than linking.
3. **Stop looking at the GPU, the blitter thread, the idle service and the
   timers.** Every one of them is now ruled out by measurement.

## Also observed, separate issue

Both logs start with one roughly 97 ms frame whose `cpu_io` alone is 96 ms, and
the long windows still contain frames of 25-30 ms. A steady 76 fps (ddpdfk) or
54 fps (ddpsdoj) with periodic 25-97 ms stalls is what these windows show, so
the reported 30-ish fps is more consistent with repeated stalls than with a
constant frame cost. The first stall coincides with loading a state, which
invalidates every compiled block and forces a full recompile.

## Verification limits

These are CPU elapsed times from sampled frames and integer
counters, not hardware performance counters. The `drc_dispatch` probe in this
image read zero because the chained entry point calls
`sh3_drc_dispatch_impl` directly instead of through the `sh3_drc_dispatch`
wrapper it instrumented; that probe must be moved before it is trusted. The
`blk_entry` subtraction of probe overhead is an estimate, not a measurement.
No frame-rate claim is made for any scene not present in these logs; in
particular the user reports about 30 fps in `ddpdfk.state4`, while this log
measures 17.7 ms (about 57 fps) on average with 25-30 ms slow frames. The user
reports saving the state during a dense pattern, so the sampled frames are
probably a mixture of that pattern and lighter play.

## Revert

Revert this commit; it adds only a report.

## Follow-up build: chained-dispatch timing

`acb674da` moves the dispatcher clock from one pair per block to one pair per
chained entry, so probe cost no longer rivals the measured span.

Artifact: `Distro360/fbneo.xex`, 34,631,680 bytes, SHA256
`989be71778e92188470ce3fb4547ca1dc671cd8b38e1eab7176ff6f65f6c4b01`, built
2026-10-08 19:22:51 from checkpoint `acb674da`. The block-link experiment that
preceded it is reverted (`a3eec114`, `b8b2921f`) and is not in this image.

What the next console log decides: `drc_dispatch` divided by `dispatch_calls`
gives the true per-entry cost of the dispatcher, clock confound removed.
Compared against what `cpu_io` still shows after `blk_entry`, `movll_service`,
`timers` and `blit_write` are subtracted, it says whether the remaining ~830
cycles per block are the dispatcher itself or the generated code, which are
different optimisation targets.
