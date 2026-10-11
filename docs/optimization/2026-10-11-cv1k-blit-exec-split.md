# CV1000: split the blitter job's command walk so the console can attribute it

Probe commit `371911ee`. Diagnostics only (`SALVIA_FBNEO_DIAGNOSTICS`,
`SALVIA_CV1K_PROBE`): no emulation code is touched and the emulated frame is
byte-identical.

## Why

`2026-10-11-cv1k-blit-rowcopy.md` changed how the next lever gets chosen. The row
copy cut `input_upload` by a third, `0.415` -> `0.284` ms/batch, and the frame
did not move: `26.423` -> `26.794 ms` of `game_and_ui`, inside the spread one
unchanging build produces by itself. A `ms/frame` ranking *inside* the blitter
job is therefore not a ranking of the frame, and the census that produced that
ranking (`2026-10-11-cv1k-blit-path-census.md`) only covers the flush -- the
Xenos batches and the replay. The other three quarters of the job have never
been measured:

| per frame = per job | base `blit-crossover` | candidate `blit-rowcopy-fix` |
| --- | --- | --- |
| `worker_busy` / `worker_jobs` | 31325.779 / 1238 = **25.303 ms** | 35207.277 / 1447 = **24.332 ms** |
| Xenos flush | 19089.516 / 1238 = 15.42 | 20352.012 / 1447 = 14.06 |
| CPU replay | 3756.634 / 1238 = 3.03 | 4546.030 / 1447 = 3.14 |
| flush, both halves | 18.45 | 17.20 |
| **command walk and construction** | **6.85** | **7.13** |

The flush halves are the only part of the job with a counter on it. Everything
else sits inside `worker_busy` unattributed: the list walk in `gfx_exec()`, the
clip command, the end-of-list flush, the two commands that carry work, and the
queue construction inside every draw. That is 6.9 ms of a 25.3 ms job, and until
it is split the next lever is a coin flip between a list-walk fix, a
construction fix and the software path a refused draw falls into.

## What

Four spans on the worker thread, accumulated on every call rather than sampled,
so dividing a window's value by the window's frame count gives a per-frame cost:

| slots | span |
| --- | --- |
| `[20]/[21]` | ticks/calls of `gfx_draw()`, the whole draw command |
| `[22]/[23]` | ticks/calls of `gfx_upload()` |
| `[36]/[37]` | ticks/calls of `epic12_gpu_submit()`, the queue construction: clip maths, the two source-page passes, the flush decision |
| `[38]/[39]` | ticks/calls of the software rasterizer `gfx_draw()` falls into when the queue refuses the command |

`[36]` and `[38]` are inside `[20]`, so they decompose the draw instead of
adding to it: `submit` is the accepted path's construction, `soft` is the
refused path's rasterizer. What is left of the job after `draw` and `upload` is
the command walk itself -- the list walk, the clip command and the end-of-list
flush.

The spans use a scope guard, `SalviaCv1kSpan`, that accumulates and counts on
scope exit, so a function with several early returns is measured exactly once.
It accumulates nothing when either tick read is zero, which is how the counter
helper reports "not ready yet". The pause report clears the slots with the rest
of the probes, so each value covers the window since the previous pause, exactly
like the batch timers:

    blit_exec_ms window=<frames> draw=<ms> draw_calls=<n> upload=<ms> upload_calls=<n> submit=<ms> submit_calls=<n> soft=<ms> soft_calls=<n>

Implementation: `salvia_cv1k_probe.h` gains the guard and the slot map for
20-39, `epic12.cpp` takes the draw, upload and refused-path spans,
`epic12_gpu.h` takes the submit span, and `d_cv1k.cpp` prints the line.

## Verification

The probe is compiled out unless `_XBOX` and `SALVIA_FBNEO_DIAGNOSTICS` are both
set, so the host build is byte-identical to the tree the row-copy fix passed on.
All six hashes below are the probe-free build's own values, and 60 and 1800
frames were re-run on this exact tree:

| frames | `STATE` | per-frame hash md5 |
| --- | --- | --- |
| 60 | `0cf251c3d512ddbb` | `c42fdb4870f4f997112d88ff6f74ee12` |
| 600 | `baabc4def027a8a3` | `022b01ad0c0b543afd34a685e07173c5` |
| 1800 | `dff81882efc1b24a` | `3f7580a932f3a859ae847bf3f3fe830b` |

Suites, all PASS on this tree, artifacts in
`salvia-tests/cv1k-blit-census-20261011/verify-*`: `sh3_ppc` 846,026 cases
(727,804 compiled / 118,222 fallback); `epic12_gpu`; `epic12_blit` (65,536 tint
cases, 5,000 image cases); `epic12_gpu_cache`, fingerprint `21d3a8b4487bb152`
on the host and on the PPC build; `epic12_gpu_untile`, 6,820 rectangles and
66,356,536 exact pixels in both endiannesses. The diagnostics XEX then compiled
and linked the spans for real, which is the only place this code exists.

## Console image

Diagnostics flavor only, as the iteration rule requires.
`xex-build/build-xex.sh --tag blit-exec-split --baseline 71e6cdda --flavor diag
--force`, printing

    blit-exec-split 371911ee 20261011-2156 diag

34,635,776 bytes, sha256
`d9c5397b4fc2be8873163fee6febbb7129ced06d4599ed24a8b02046d5aa3df4`, archived as
`salvia-tests/xex-archive/fbneo-20261011-2200-blit-exec-split-diag.xex`,
deployed as `Distro360/fbneo.xex`, `fbneo-diag.xex` and
`fbneo-blit-exec-split-diag.xex`. The tag string carries 2156 and the archive
2200 because the stamp is taken at the start of the build and the archive named
at the end; that is the script's normal behaviour.

`--force` was needed and its precondition was checked first: all ten mirrored
files in the runtime tree already matched the previous reviewed commit
`d8c2288d`, file by file, so the flag overwrote a reviewed state rather than an
unreviewed edit. The runtime tree was one build behind the worktree, which is
why the drift check tripped.

The image pairs with `fbneo-blit-rowcopy-fix-diag.xex` (`blit-rowcopy-fix
36763a39 20261011-1827 diag`), which is the same source behaviourally -- the
probe adds counters, not behaviour -- so one run of the probe image is enough.
Keeping the row-copy image beside it lets the same session be read against the
window already in `cv1000-gpu.log`.

## What the console run should decide

Read the new line in the steady window and subtract:

- `upload` is the second command that carries work, so `job - draw - upload` is
  the walk: the list traversal, the clip command and the end-of-list flush.
- `submit` inside `draw` is the construction. If `submit` is most of `draw`, the
  lever is the two source-page passes and the clip maths, not the pixel loops.
- `soft` is the refused path. If it is large, the rasterizer the queue rejects
  is the target, and a threshold change is cheaper than rewriting a pixel loop.
- `draw_calls` against the window's command count says how often the queue
  refused; a small `draw_calls` with a large `soft` is the same statement.

## Limits

- Each span costs two counter reads, so a short span is inflated; the values are
  for the split, not for absolute cost.
- `submit` contains any flush it forces, so compare it with the batch timers
  rather than adding it to them.
- A refused draw appears in `draw` as well as in `soft`; a refused upload is
  only in `upload`.
- One title, no input; the window is whatever the pause produces.

## Revert

Revert `371911ee` and delete this doc: the `SalviaCv1kSpan` guard and the slot
comments in `salvia_cv1k_probe.h`, the four span declarations in `epic12.cpp`
and `epic12_gpu.h`, and the `blit_exec_ms` line in `d_cv1k.cpp`.
