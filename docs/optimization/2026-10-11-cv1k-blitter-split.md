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

## Limits

- Each accumulator includes its own `QueryPerformanceCounter` read (~30-50 ns),
  and there are now two reads per flush instead of none; over ~180 flushes per
  frame that is well under 20 us, but it is not zero.
- The replay accumulator covers only batches that reach `epic12_gpu_flush()`
  with commands queued. Command building and the queueing that never flushes are
  the `worker_busy - gpu - replay` remainder, which is why that remainder is
  printed next to them rather than assumed to be zero.
- One game, one state, no input. The 8192-pixel split point moves with the
  title.

## Revert

Revert this commit: `salvia_cv1k_probe.h`, the `epic12_gpu_flush()` timing in
`epic12_gpu.h`, the array size in `epic12.cpp`, and the `blit_flush_ms` line and
24-slot clear in `d_cv1k.cpp`.
