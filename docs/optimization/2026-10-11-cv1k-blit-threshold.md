# CV1000: put the blit split point behind a DIP and bucket the replay by size

Baseline `6d8f2769`, the image whose console log is the console result in
`2026-10-11-cv1k-blitter-split.md`. The counters are behind
`SALVIA_CV1K_PROBE` and the DIP entry is in the `_XBOX || EPIC12_GPU_TEST`
group, so a release image carries neither; the default switch value reproduces
the 8192-pixel split exactly, and no emulation code is touched.

## Why

The console split put a frame's blitter job at 25.3 ms: 15.4 ms of 9.25 Xenos
batches at ~1.67 ms each, 3.0 ms of 139 CPU replays of ~824 pixels, and 6.9 ms
of display-list walk and command construction. The measured marginal costs put
the crossover at ~63k pixels per batch -- 1.64 ms of Xenos fixed cost against
26 ns per replayed pixel -- and mmpork's Xenos batches are 36-59k pixels, i.e.
the split point is already within 10% of the crossover, on a quantity whose
run-to-run spread is of the same order.

So "move the split point" cannot be decided from the host, and it cannot be
decided from one console number either, because the answer is a cost per batch
size rather than an average. Two things had to exist before the console could
be asked:

- the split point has to move without a rebuild, or every answer costs an image
  and a session;
- the replayed batches have to be reported in size buckets, because the mean of
  a mixture cannot name a crossover.

## What

- `epic12_gpu_threshold`, default 8192, replaces the constant in
  `epic12_gpu_flush()`. `gfx_exec()` in `epic12.cpp` re-reads it from Dip D bit
  7 (0x80) on every list, so the console can move the split point from the DIP
  menu and the pause report names the setting it ran with (`dips=00,07,00,80`).
- `epic12_gpu_force_bringup` keeps the session's first render call when the
  switch is on. Bringing the compositor up is what switches the core to the
  asynchronous present, so a switch that routed *every* batch to the CPU replay
  would move the present mode as well as the batches, and the comparison would
  be measuring two things. One forced bring-up batch leaves the DIP a pure
  batch-routing switch. It is `#ifdef _XBOX` because the bring-up state
  (`epic12_xenos_validated`) lives in the Xenos header.
- `replay_buckets` reports the CPU replay in four size buckets -- <8192,
  8192-32768, 32768-131072, >=131072 pixels -- as ticks, pixels and batches in
  `salvia_cv1k_probe[24..35]`. The 32768-131072 bucket is the one that answers
  the question, because that is where the batches the default sends to the
  Xenos would land if the threshold moved.
- `epic12_gpu_replay_bucket_px` holds the three boundaries, so a bucket index is
  the number of boundaries the batch passed.
- The new DIP is `{0x03, 0x01, 0x80, ...}` in the `GPU Blitter (Xbox 360)`
  group: "8192 (default)" and "All CPU". Dip D's other bits are already
  Salvia's (0x0f CPU tenths, 0x10 engine, 0x20 compositor off) and the game
  cannot see any of them: the only DIP read the blitter decodes is
  `case 0x50: return *dips;`, which is Dip A.

## Verification

Host, `/home/humor/salvia-tests/cv1k-blit-threshold-20261011/`, with
`epic12.cpp` and `d_cv1k.cpp` recompiled after the header edits (the build
looks at the `.cpp` timestamps, so a header-only edit measures the previous
objects -- this has already cost one round):

`ddpdfk`, `user-ddpdfk4-core.state`, dips `07,00,00`, `render_cores=2`,
`CV1K_DIPD=00` (the switch is off, i.e. the committed behaviour):

| frames | `STATE` | per-frame hash md5 |
| --- | --- | --- |
| 60 | `0cf251c3d512ddbb` same | `c42fdb4870f4f997112d88ff6f74ee12` same |
| 600 | `baabc4def027a8a3` same | `022b01ad0c0b543afd34a685e07173c5` same |
| 1800 | `dff81882efc1b24a` same | `3f7580a932f3a859ae847bf3f3fe830b` same |

Suites, all PASS: `tests/sh3_ppc` `846026 cases compiled 727804 fallback
118222`; `tests/epic12_gpu` (all shader/fallback paths, flips, dirty metadata,
no-op and dependency cases, reorder model, 128 and 256 slots);
`tests/epic12_blit` (65,536 tint/alpha rounding cases, 1,000,000 packed colour
sums, 5,000 image cases); `tests/epic12_gpu_cache` (12,000 batches, 73,645
allocations, 2,400 invalidations, fingerprint `21d3a8b4487bb152` on the host and
on the PPC build); `tests/epic12_gpu_untile` (6,820 rectangles, 66,356,536 exact
pixels, both endiannesses).

The host A/B tool reads `CV1K_DIPB/C/D` from the environment, so both routes can
be exercised without the console; the runs above are the default route, which is
the one the host hashes have always come from, and the switch's own route is
verified by construction rather than by hash because `mmpork`'s state does not
boot to a picture on the host.

## Console image

Diagnostics flavor only, as the iteration rule requires:

```
salvia-tests/xex-build/build-xex.sh --tag blit-crossover --baseline 6d8f2769 --flavor diag
```

`b5dd53ba`, 34,635,776 bytes, sha256
`429a5c246452f821da220fa845cf6b823749d62b24ad6d5d7377b5631ae7b4eb`, archived as
`xex-archive/fbneo-20261011-1409-blit-crossover-diag.xex` and handed off as
`Distro360/fbneo-blit-crossover-diag.xex`. That is one image rather than two
because the two variants this round compares are two DIP settings of the same
bytes, which is the point of moving the split point onto a switch.

The build stamped `blit-crossover b5dd53ba 20261011-1404 diag` into
`salvia_cv1k_buildtag.h`, so the pause report names this image, and the freshly
built runtime `libretro.lib` contains both `replay_buckets` and `GPU Blit
Threshold` -- which is the evidence that the new code was compiled rather than
relinked from the previous objects. Both `.cpp` files were touched after the
header edits for exactly that reason.

`Distro360/` now holds only that image. The previous round's
`fbneo-blitter-split-diag.xex` (the image the console result above came from),
`fbneo-mmpork-prof-diag.xex` and the `fbneo.xex` / `fbneo-diag.xex` duplicates
the build script writes moved to `xex-archive/distro360-20261011/`; nothing was
deleted, so either can be restored if this image misbehaves on the console.

## Console protocol

One image, one session, no input: pause three or four times with the default
(`GPU Blit Threshold` = "8192 (default)", `dips=00,07,00,00`), then set it to
"All CPU" (`dips=00,07,00,80`) and pause three or four more times. Because the
state and the image are the same, the two sets pair up by frame number.

- `replay_buckets ... 32768_131072=<us>/<px>/<batches>` gives the ns/px the CPU
  replay would spend on the batches the default sends to the Xenos. Multiply by
  the px/frame the `blit_flush_ms` line reports for the Xenos path and compare
  with that line's `gpu` ms: if the CPU figure is lower, the threshold should
  move; if the two are within a few percent, the split point is already at the
  crossover and the next lever is elsewhere.
- The `<8192` and `8192_32768` buckets also show whether the replay's cost is
  per batch or per pixel at the small end, which is the other half of the same
  question (139 batches per frame at 21.6 us each).
- `worker_busy` and `worker_wait` are the acceptance numbers; `blit_flush_ms
  gpu` plus `replay` should reproduce them within the remainder.

## Console result

`Distro360/cv1000-gpu.log`: `mmpork`, no input, one session of seven windows
that walk Dip D bit 7 (the split point) and bit 5 (the compositor) without
changing the image or the state.

| window | dips | frames | ms/job | `draw_sync` | `cpu_io` | frame total |
| --- | --- | --- | --- | --- | --- | --- |
| boot | `00,07,00,00` | 119 | 20.01 | 0.772 | 2.633 | 3.629 |
| default | `00,07,00,00` | 1238 | 25.30 | 13.973 | 11.981 | 26.106 |
| All CPU | `00,07,00,80` | 116 | 26.41 | -- | -- | -- |
| All CPU | `00,07,00,80` | 459 | 23.36 | 12.396 | 8.864 | 21.390 |
| All CPU | `00,07,00,80` | 411 | 24.56 | 11.015 | 14.541 | 25.686 |
| All CPU + compositor off | `00,07,00,a0` | 556 | 23.50 | 12.784 | 11.408 | 24.324 |
| All CPU | `00,07,00,80` | 243 | 24.78 | 14.976 | 11.665 | 26.770 |

`ms/job` is `worker_busy / worker_jobs`; the 116-frame window prints no frame
line because it sampled no frame, and the last window is the first one after
the compositor was switched back on, so it is an all-CPU number like the two
before it.

**The split point is not a lever, and the console says so directly.** The
worker job is 23.4-26.4 ms and `draw_sync` is 11.0-15.0 ms in every window:
default, all-CPU and compositor-off are one number to within this title's
window-to-window spread (the baseline windows in
`2026-10-11-cv1k-blitter-split.md` spread 4.6 ms). Turning the Xenos off moves
the pixels to the CPU replay and shortens the job by 1.8 ms; turning the
compositor off moves them to the legacy blitter and shortens it by 1.8 ms as
well. Three rasterizers, one cost.

### The buckets say there is no crossover to look for

Per-pixel marginal cost of the CPU replay, `ms / px` of each bucket, i.e. what
the default route would pay for the batches it currently hands to the Xenos:

| window | `<8192` | `8192-32768` | `32768-131072` | `>=131072` |
| --- | --- | --- | --- | --- |
| 1238 default | 25.9 | -- | -- | -- |
| 459 All CPU | 27.5 | 24.1 | 23.7 | 28.4 |
| 411 All CPU | 25.2 | 25.4 | 24.6 | 28.7 |
| 243 All CPU | 25.7 | 26.9 | 26.7 | 28.7 |

The marginal cost is flat from 8k to 180k pixels. The CPU replay has no
per-batch fixed cost worth naming, so there is no batch size at which it stops
being competitive with the Xenos path: the host estimate of a ~63k-pixel
crossover came from the Xenos side's fixed cost, and this is the CPU side
saying it has none. The Xenos side of the same session is 1.61 ms per batch
(11850 batches, 645.5 M px, 15.42 ms per frame) and `batch_timing` splits that
as upload 0.77, untile/mask 0.45, readback wait 0.18, submit 0.13, plan 0.06 --
i.e. the Xenos route's serial cost is **data marshalling on the Xenon, not
rasterization on the Xenos**, at 7.4 + 4.3 ms per frame. Replaying those same
pixels on the CPU would cost 13.5-16.3 ms per frame, which is why the two
routes measure the same and why the switch cannot buy the 1-2 ms the host
predicted.

### What the OSD's `CPU` number is

The frontend's `CPU: n%` is `GetThreadTimes(GetCurrentThread())`, sampled once
per frame in `Sync::sample_cpu_utilization()` (`src/io/sync.cpp`): the run time
of the **emulation thread** over wall time, so every millisecond it spends
blocked counts as idle. That thread is the one that spends the frame in
`draw_sync`; the 23-26 ms of blitter work runs on the other thread and is
invisible to the number.

It is therefore a restatement of the split rather than a measurement of it:
`cpu_io + prep + audio` is 9.0-14.5 ms of a 21.4-26.8 ms frame, so the gauge
reads 40-55% -- and reads 40-55% whichever engine rasterizes, because the
routing never touches the emulation thread's work or its wait. Four of Xenon's
six hardware threads are idle in this title (emulation on core 0 thread 1, one
blit worker on core 1, core 2 unused); that is the headroom the gauge is
pointing at, and it is not a fault in the frame.

### What this leaves

The job is serial on one worker thread, the frame is `cpu_io + draw_sync`, and
the job is the term to attack. In the default window it is 25.30 ms: 15.42 ms
of Xenos marshalling, 3.03 ms of CPU replay for the small batches, 6.85 ms of
command walk. The all-CPU window is 23.36 ms: 16.35 replay, 7.01 remainder. The
candidates the numbers still support are (a) the Xenos marshalling -- 11.7 ms
per frame of tile swizzle and untile/mask for 1.13 MB of source uploads at a
78% atlas hit rate, a byte permute with an exactness test already in the tree,
(b) the replay's 25.9 ns/px, and (c) the 6.9 ms remainder, which is still
unmeasured inside the job.

## Limits

- The buckets cover only flushes that reach `epic12_gpu_flush()` with commands
  queued. The command construction that never flushes is in the remainder, as
  before.
- "All CPU" is `1u << 24` pixels, not a special case: a batch that large would
  still go to the Xenos. CV1000 batches are three orders of magnitude below it.
- The switch is read per gfx list, so it takes effect on the next list rather
  than at the next boot; the pause report's `dips=` field is the record of which
  setting a window was captured under.
- One title, one state, no input. The crossover moves with the title.

## Revert

Revert this commit: the `epic12_gpu_threshold`, `epic12_gpu_force_bringup` and
bucket code in `epic12_gpu.h`, the two assignments in `epic12.cpp`'s
`gfx_exec()`, the array size in `salvia_cv1k_probe.h`, and the `replay_buckets`
line, the 40-slot clear and the DIP entry in `d_cv1k.cpp`.
