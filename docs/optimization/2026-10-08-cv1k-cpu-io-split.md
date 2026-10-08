# CV1000 Xbox 360: the frame is 95% `cpu_io`, so split that phase

Source checkpoint: `f7e22bc1` plus the diagnostic probe changes below. No
emulation change: every probe is compiled out unless `_XBOX` **and**
`SALVIA_FBNEO_DIAGNOSTICS` are both set.

## What the first pause-only console log showed

The user supplied `Distro360/cv1000-gpu.log` recorded with the previous
diagnostic XEX. It contains two windows, both on `ddpsdoj`
(`dips=00,07,00,00`, `clock_hz=102400000`, `render_cores=2`, GPU compositor
active as `sprite-attributes`).

| Reading | Window 1 (205 frames) | Window 2 (854 frames) |
| --- | ---: | ---: |
| `core_frame_ms` `prep` | 0.003 | 0.003 |
| `core_frame_ms` `cpu_io` | 31.968 | 17.330 |
| `core_frame_ms` `audio_tail_misc` | 0.207 | 0.127 |
| `core_frame_ms` `draw_sync` | 0.725 | 0.735 |
| `core_frame_ms` `total` | 32.905 | 18.196 |
| `core_frame_ms` `sampled_peak` | 86.708 | 24.059 |
| `frontend_frame_ms` `game_and_ui` | 33.610 | 18.539 |
| `frontend_frame_ms` `present` | 2.865 | 0.669 |
| `frontend_frame_ms` `limiter` | 3.779 | 0.602 |

Window 1 has only three timing samples; one of them is the 86.708 ms peak, so
its mean is not a description of a typical frame. Window 2 has fourteen samples
and no frame above the 25 ms slow threshold, so it is the usable steady state:
**17.330 of 18.196 ms is `cpu_io`**.

`cpu_io` is `reviewMarks[1]..reviewMarks[2]` in `d_cv1k.cpp`, i.e. the whole
240-slice `Sh3Run` loop, which also contains the blitter register writes.

The GPU side of the same window is small by comparison:

| Reading | Window 2 value |
| --- | ---: |
| `sampled_batch_cpu_ms` | upload 0.782, alpha_plan 0.231, submit 0.217, readback_wait 0.401, untile_mask 0.478 |
| `worker_wait` | `avg_ms=0.002 peak_ms=0.002` over 767 joins |
| `flushes` | end 881, source_dependency 5591, region 954, source_pages 271, command_limit 192, cpu_upload 359, small_cpu 6253 |
| `batch_timing mode=async position=later` | `peak_total=5.707` |
| `drc_work_arena` | `peak_words=3688168` (14.75 MiB of the 20 MiB arena) |

So the Xenos compositor, the feedback resolves, the atlas uploads and the
worker join are all worth roughly 1-3 ms per frame, while the SH3 run phase is
worth 17 ms. Earlier 128/256-page batching experiments and the source-dependency
page filter were aimed at this 1-3 ms slice; that is why they did not move the
frame rate.

## Why the SH3 counters do not explain `cpu_io`

The same window reports, for two work-sampled frames:

```
drc_work_execution native_calls=64902 native_guest_cycles=577209 interpreter_steps=1927
drc_movll_service handled=2769 guest_cycles=2834433
```

`native_guest_cycles + movll_service_cycles = 3,411,642`, which is exactly two
frames of the 1,706,667-cycle budget, so the accounting is complete: the guest
**executes** 288,604 cycles per frame and **burns** 1,417,216 cycles per frame
in the fast-forwarded idle/device MOV.L service (`Sh3BurnCycles`, two integer
adds per call).

288,604 executed guest cycles over 32,451 native blocks cannot plausibly cost
17 ms on a 3.2 GHz Xenon dynarec. Either the cost is not the generated code, or
it is a cost that the counters do not attribute. The only other work inside the
measured span is the blitter register write (`epic12_write` -> `gfx_exec_write`,
which joins the previous worker job and walks the list for the delay estimate)
and the outer `Sh3Run` bookkeeping.

## The probe added here

`salvia_cv1k_probe.h` (new, `burn/devices`) publishes ten counters and one
`QueryPerformanceCounter` helper. Everyone is compiled out when the diagnostic
switch is off.

| Counter | Measured span | Sampling |
| --- | --- | --- |
| `worker_busy` / `worker_jobs` | `gfx_exec()` inside `run_blitter_cb()` | every job |
| `blit_write` / `blit_calls` | `gfx_exec_write()` from `notify_wait()` through `gfx_create_shadow_copy()` | every call |
| `drc_dispatch` / `dispatch_calls` | the whole `sh3_drc_dispatch<Chained>()` body | every call (240/frame) |
| `blk_entry` / `entry_samples` | `b.entry()` for one generated block | 1 in 64 blocks |
| `movll_service` / `service_samples` | `sh3_drc_service_movll()` | 1 in 64 services |
| `timers` / `timer_callbacks` | `sh4_run_timers()` at the end of every `Sh3Run_timerhack_impl` slice, plus every `timer_exec` invocation | every slice / every callback |

`cv1k_review_report()` prints them as one `core_phase_ms` line next to
`core_frame_ms` and clears them, so each pause window is independent.
The result of that build is in `2026-10-08-cv1k-phase-split-result.md`. The
`drc_dispatch` field read zero and was removed: the CV1000 frame loop calls
`sh3_drc_dispatch_impl` directly, so instrumenting the `sh3_drc_dispatch`
wrapper measured nothing.

`timers` covers the remaining suspect. `Sh3Run_timerhack_impl` calls
`sh4_run_timers(cycles)` once per slice with the *whole* slice budget, including
the cycles the idle service burned, so its cost tracks consumed guest cycles
rather than executed instructions -- the one property the observed `cpu_io`
has. `run_prescale` batches ticks, but it re-enters the loop once per
`timer_exec` callback, so a game that programs a TMU with a very short period
can drive callbacks per frame far beyond the 32k block dispatches.
`timer_callbacks` counts those invocations directly.

## Build and verification status

The first build attempt failed with
`sh4.cpp(3509): error C2365: 'FLOAT' : redefinition`, because the probe header
pulled in `<xtl.h>`, whose `windef.h` typedefs `FLOAT`, while `sh4.cpp` defines
an SH-4 opcode function of that name. The probe header therefore declares the
clock helper only and `epic12.cpp` defines it next to the counters.

The rebuilt diagnostic image succeeded (`build-main-diag.cmd`, exit 0, no
warnings from the changed translation units). Identity of the artifact:

| Property | Value |
| --- | --- |
| Path | `E:\Baiduyundownload\salvia-toolchain\.work\Salvia-main\Distro360\fbneo.xex` |
| Size | 34,631,680 bytes |
| SHA256 | `f7aadb1bcb307cbdb757aa261ca6b1307b983cd3aa78340c0b3b69401ed91875` |
| Built | 2026-10-08 18:37:44 |

`epic12.cpp`, `d_cv1k.cpp` and `sh4.cpp` were all recompiled in that build, and
the intermediate `Release/Xbox 360/libretro.lib` (18:35:33) contains
`core_phase_ms`, `worker_busy`, `timer_callbacks` and `salvia_cv1k_tick`, so the
probe report is linked into the image. The XEX container itself is compressed,
so plain-text search of the XEX is not a usable check.

The sync one-liner is
`/home/humor/salvia-tests/xex-build-20261008/sync-and-build-probe.cmd`; it copies
the six changed sources plus a `SALVIA_FBNEO_DIAGNOSTICS 1` header into the
build tree and then calls the existing `build-main-diag.cmd`.

Nothing here is a console measurement. No frame-rate claim is made. The release
image must keep `SALVIA_FBNEO_DIAGNOSTICS 0`, which compiles every probe out and
leaves the emulation byte-identical.

## Revert

Revert this commit; the probe header is additive and is referenced only from
`#if SALVIA_CV1K_PROBE` blocks. The console XEX is unaffected until the staged
build script is run.
