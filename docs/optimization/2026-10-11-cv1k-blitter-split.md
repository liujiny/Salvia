# CV1000: split the blitter flush into the Xenos path and the CPU replay

Baseline `c2ea0dc6`. Diagnostics only: `SALVIA_CV1K_PROBE` is `_XBOX &&
SALVIA_FBNEO_DIAGNOSTICS`, so the probe is not in a host build and not in a
release image, and no emulation code is touched.

## Why

`mmpork` runs at 30-40 fps on the console while neither the SH-3 nor the code
arena is under pressure, and the pause report says where the time goes. A frame
is budgeted 16.67 ms; the blitter job the worker thread runs is about 26 ms, so
the main thread sits in `worker_wait` for 13.5-14.6 ms (peak 20-24 ms) and the
frame ends up 22-29 ms long.

From `Distro360/cv1000-gpu.log` (build `mmpork-prof`, five windows of 37, 1570,
945, 552 and 723 frames) against the `ddpdfk` baseline in `Distro360/base.log`:

| field | mmpork | ddpdfk |
| --- | --- | --- |
| `core_frame_ms total` | 22-29 ms | 17 ms |
| `draw_sync` | 11-16 ms | 0.87 ms |
| `worker_wait` avg / peak | 13.5-14.6 / 20-24 ms | 0.002 ms |
| `cpu_io` | 10-16 ms | ~13 ms |
| `present_state fallbacks=0 timeouts=0` | clean | clean |
| `gpu_fallback` | 0 | 0 |
| `drc_work_arena recycles / evictions` | 0 / 0 | 0 / 0 |
| `drc_work_arena peak_words` | 2.5-2.9 M | 4.57 M |

The frame is therefore not CPU-bound and not arena-bound: it is waiting on the
blitter worker, and the blitter worker is not falling back to anything.

What is specific to mmpork is the shape of its traffic. It submits about 180
batches per frame, of which only 4-9 are large enough (>= 8192 px) to go to the
Xenos; the other ~170 are replayed on the CPU. `flushes region` is about 100
per frame and `flushes source_dependency` about 55 per frame, where ddpdfk has
both at 0. The 8192-pixel threshold that keeps small batches off the GPU was
chosen against ddpdfk-like traffic, and mmpork is the first title whose batches
sit almost entirely below it.

Even so, this path is capped: driving the blitter job to zero would leave the
frame at its `cpu_io` floor of ~16 ms, i.e. 58-60 fps. The split below decides
whether that floor is worth walking toward, and which half of the job to walk
at.

### The counting trap this probe had to avoid

`epic12_gpu_diag_reset()` runs once at core init, so the `flushes ...` line is a
session total while `core_phase_ms` and `sampled_batch_cpu_ms` are reset every
window; dividing one by the other is meaningless. And `sampled_batch_cpu_ms`
only ever covered the batches that went to the Xenos, so the ~170 replayed
batches per frame -- the majority of the job -- had no measurement at all. That
is the hole this change fills.

## What

`epic12_gpu_flush()` now times itself on the probe array and credits the time to
one of two accumulators:

- `[14]/[15]/[19]` -- ticks, batches and submitted pixels of the Xenos path
  (render attempted and succeeded),
- `[16]/[17]/[18]` -- ticks, batches and pixels of the CPU replay path (too
  small for the Xenos, or the render failed).

The first Xenos batch of a session compiles the shaders and runs the pixel
self-test, so it is excluded from `[14]` until `epic12_xenos_validated` is set;
otherwise one window would report the compile time as steady-state work.

`cv1k_review_report()` prints the two spans as one new line and clears all 24
slots:

```
blit_flush_ms window=<frames> gpu=<ms> gpu_batches=<n> gpu_pixels=<n>
              replay=<ms> replay_batches=<n> replay_pixels=<n>
```

`worker_busy` on the `core_phase_ms` line is the whole job, so `worker_busy -
gpu - replay` is the command building and accounting around the two paths.

## Verification

The probe is compiled out unless `_XBOX` and `SALVIA_FBNEO_DIAGNOSTICS` are
both set, so everything below is a no-op for the host binary -- which is the
point, and also the limit: the host cannot exercise the new counters.

`ddpdfk`, `user-ddpdfk4-core.state`, dips `07,00,00`, `render_cores=2`, against
the values every CV1000 console round has used since `2026-10-09`:

| frames | `STATE` | per-frame hash md5 |
| --- | --- | --- |
| 60 | `0cf251c3d512ddbb` same | `c42fdb4870f4f997112d88ff6f74ee12` same |
| 600 | `baabc4def027a8a3` same | `022b01ad0c0b543afd34a685e07173c5` same |
| 1800 | `dff81882efc1b24a` same | `3f7580a932f3a859ae847bf3f3fe830b` same |

Suites: `tests/sh3_ppc/run.py` `PASS 846026 cases compiled 727804 fallback
118222`; `tests/epic12_gpu` (all shader/fallback paths, flips, dirty metadata,
no-op and dependency cases, reorder model), `tests/epic12_gpu_cache` (12,000
batches, 73,645 allocations, 2,400 invalidations, clock wraps), 
`tests/epic12_gpu_untile` (6,820 rectangles, 66,356,536 exact pixels, both
endiannesses) and `tests/epic12_blit` (65,536 tint/alpha rounding cases,
1,000,000 packed colour sums, 5,000 image cases) all PASS. Logs:
`/home/humor/salvia-tests/cv1k-blitter-split-20261011/`.

## Console image

Diagnostics flavor only, as the iteration rule requires:

```
salvia-tests/xex-build/build-xex.sh --tag blitter-split --baseline c2ea0dc6 --flavor diag
```

`6d8f2769`, 34,635,776 bytes, sha256
`121762e6fc463ece632155ad2b89185e7ae4a7cb7195658c7c82d32abd0b3e22`, archived as
`xex-archive/fbneo-20261011-1204-blitter-split-diag.xex`, kept as
`Distro360/fbneo-blitter-split-diag.xex`. The runtime tree's `libretro.lib`
contains the injected tag `blitter-split 6d8f2769 20261011-1200 diag`, so the
pause report will name this image, and `epic12.cpp` and `d_cv1k.cpp` were both
recompiled in this build (the mirror touched the `.cpp` files, not just the
headers they include).

The console A/B is this image against `Distro360/fbneo-mmpork-prof-diag.xex`
(`c2ea0dc6`, sha256 `3fe36ba9b9cf3450f24ac8a74613b5690e577afa9091d58cda7ba9f4de44b328`),
which is the image the `cv1000-gpu.log` above came from. Both are diagnostics
flavors of the same source; the only difference is this probe.

Protocol, `mmpork`, no input, pause three or four times:

1. Run the baseline image and pause 3-4 times; the windows' `worker_busy`,
   `worker_wait` and `core_frame_ms` are the reference.
2. Run this image the same way. `blit_flush_ms` splits `worker_busy` into
   `gpu` (Xenos batches) and `replay` (CPU replay of the small ones); the
   remainder is command building and accounting.
3. If `replay` dominates, the lever is the ~170 small batches per frame -- the
   8192-pixel threshold or batching them up. If `gpu` dominates, the lever is
   why the large batches flush at all (`flushes region` /
   `flushes source_dependency`).

## Console result

`Distro360/blitter-split.log`: mmpork, no input, four pause windows of 206,
1671, 1838 and 996 frames. The 206-frame window is the first one and is left
out -- its Xenos half is 100 batches in 126.6 ms (1.27 ms per batch, i.e. the
session's shader compile and the pixel self-test) and its replay half is 2
batches.

### Parity

`6d8f2769` changes no behaviour, so this image and the `mmpork-prof` baseline
have to measure the same thing, and they do: the worker job came out at 25.28 /
25.49 / 25.87 ms against the baseline's 26.29 / 26.17 / 22.19 / 26.77 ms, and
`worker_wait` at 12.19-13.27 ms against 13.51-14.78 ms. The baseline's own four
windows spread over 4.6 ms, so a one-millisecond difference between the two
images is the run rather than the probe. The A/B's value is that the split
below can be attributed: the two images behave as one.

| window | jobs | worker_busy ms | ms/job | worker_wait avg / peak | cpu_io | draw_sync | core_frame_ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1671 | 1671 | 42246.387 | 25.28 | 13.271 / 18.807 | 13.429 | 13.144 | 26.725 |
| 1838 | 1836 | 46799.480 | 25.49 | 13.271 / 20.650 | 12.537 | 14.881 | 27.588 |
| 996 | 996 | 25765.543 | 25.87 | 12.185 / 20.650 | 17.750 | 11.350 | 29.234 |
| 1570 | 1570 | 41269.522 | 26.29 | 14.576 / 20.369 | 12.239 | 14.912 | 27.302 |
| 945 | 945 | 24735.485 | 26.17 | 14.782 / 23.192 | 13.377 | 15.597 | 29.137 |
| 552 | 516 | 11449.872 | 22.19 | 14.267 / 23.192 | 10.421 | 11.425 | 21.976 |
| 723 | 723 | 19352.247 | 26.77 | 13.505 / 23.698 | 15.749 | 11.276 | 27.210 |

The first three rows are `blitter-split`, the last four the `mmpork-prof`
baseline.

### Where the job goes

| window | gpu ms | batches | pixels | replay ms | batches | pixels |
| --- | --- | --- | --- | --- | --- | --- |
| 1671 | 25794.676 | 15464 | 887.22 M | 4997.066 | 231777 | 190.96 M |
| 1838 | 24044.812 | 13863 | 814.77 M | 8665.128 | 420537 | 332.73 M |
| 996 | 15353.389 | 12370 | 446.39 M | 2747.476 | 121740 | 97.51 M |

Per frame, i.e. divided by the window's job count:

| window | gpu ms | batches | px/batch | ms/batch | replay ms | batches | px/batch | us/batch | ns/px | remainder ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1671 | 15.44 | 9.25 | 57374 | 1.67 | 2.99 | 138.7 | 824 | 21.6 | 26.2 | 6.85 |
| 1838 | 13.10 | 7.55 | 58773 | 1.74 | 4.72 | 229.1 | 791 | 20.6 | 26.1 | 7.67 |
| 996 | 15.42 | 12.42 | 36085 | 1.24 | 2.76 | 122.2 | 801 | 22.6 | 28.2 | 7.69 |

`remainder` is `worker_busy - gpu - replay`: the display-list walk and the
command construction that never reaches `epic12_gpu_flush()`, with all three
terms inside the same timed job.

The Xenos cost per batch is confirmed independently by the `batch_timing` line
of the same window (`position=later`, 235 samples):

| lock | source_upload | plan | input_upload | submit | readback_wait | untile_mask | total |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0.001 | 0.375 | 0.074 | 0.406 | 0.176 | 0.190 | 0.416 | 1.638 ms |

Two facts came out of this that change the plan:

1. **The two paths are at the crossover.** CPU replay works out at 26 ns per
   pixel of marginal cost (each replayed batch is 824 pixels, 21.6 us, and every
   one of them also pays its own clock pair). A Xenos batch pays about 1.64 ms
   before its first pixel, so it takes ~63k pixels to break even, and mmpork's
   Xenos batches are 36-59k pixels. Neither path is obviously wrong for those
   batches, which means the 8192-pixel threshold is not misrouting them: the
   split point already sits near the crossover and moving it is worth 1-2 ms per
   frame at most. What costs is the mixture -- 9.25 Xenos batches per frame at
   1.67 ms each.
2. **Only 18.4 of the 25.3 ms are the two raster paths.** The other 6.9 ms is
   the walk and the command construction around them, over ~148 flushes per
   frame. `region=134445` and `source_dependency=68526` are session totals and
   account for ~121 of those 148, so the two named flush reasons are the shape
   of the traffic rather than an anomaly.

### Frame shape and the target

The main thread's `cpu_io` (12.5-13.4 ms in the two long windows) runs
concurrently with the worker job and then waits: the frame is `cpu_io +
draw_sync`, 26.7-29.2 ms, i.e. 34-37 fps. A 16.67 ms frame can afford about
3 ms of `draw_sync`, so the job has to come down to ~16 ms, which is ~9 ms out
of the current 25.3, and the floor underneath it is `cpu_io` itself. That floor
is not a constant: the 996-frame window ran `cpu_io` at 17.75 ms, so even a
`draw_sync`-free frame would be a 55-60 fps frame there. The room is in the
6.9 ms remainder and the 15.4 ms of Xenos batches, not in the 3.0 ms of replay.

Health is clean in every window: `present_state fallbacks=0 timeouts=0`,
`gpu_fallback=0`, `drc_work_arena recycles=0 evictions=0` with `peak_words`
2.49-3.12 M against the ddpdfk baseline's 4.57 M, and `drc_work_dispatch
rebuilds` 30-40. Nothing here is a fallback or a stall; it is work.

## Limits

- Each accumulator includes its own `QueryPerformanceCounter` read (~30-50 ns),
  and there are now two reads per flush instead of none; over ~180 flushes per
  frame that is well under 20 us, but it is not zero.
- The replay accumulator covers only batches that reach `epic12_gpu_flush()`
  with commands queued. Command building and the queueing that never flushes are
  the `worker_busy - gpu - replay` remainder, which is why that remainder is
  printed next to them rather than assumed to be zero.
- Every flush happens inside the call the worker job times (`run_blitter_cb` ->
  `gfx_exec`), including the CPU-upload flushes, so both accumulators and
  `worker_busy` describe the same call chain and the remainder cannot go
  negative on a consistent run. A negative remainder would mean a flush ran
  outside that call, which is itself a finding.
- One game, one state, no input. The 8192-pixel split point moves with the
  title.

## Revert

Revert this commit: `salvia_cv1k_probe.h`, the `epic12_gpu_flush()` timing in
`epic12_gpu.h`, the array size in `epic12.cpp`, and the `blit_flush_ms` line and
24-slot clear in `d_cv1k.cpp`.
