# CV1000 console A/B: the code-size shrink removes the arena recycle

Two diagnostics images, same state (`user-ddpdfk4-core.state`), no input,
`ddpdfk`, `dips=00,07,00,00`, `render_cores=2`, two pauses each. The windows are
paired by the cumulative `interval_frames` printed in each report, since the
emulation is deterministic and equal cumulative frames mean equal scenes.

| | baseline | this work |
| --- | --- | --- |
| build tag | `codegen-shrink-base 3336c491 20261008-2323 diag` | `codegen-shrink-r4 04245442 20261009-0031 diag` |
| image SHA256 | `3faa500e48cb42244eaed5a9cab105f7329ac8eed1169d53f062ff24d1e35d94` | `29d56e8ac2d3cfdb35052fb787c82ad54d5fadbd18324c71cdb30f179026601f` |
| usable window | `interval_frames=2613`, 37 timing samples | `interval_frames=2633`, 40 timing samples |
| raw log | `xex-archive/console-logs/cv1000-gpu-codegen-shrink-base.log` | `xex-archive/console-logs/cv1000-gpu-codegen-shrink.log` |

The first window of the change image is 48 frames of cold state load and is
excluded; the baseline's first window (1491 frames) is the previous warm-up
window and is not pairable, so the pair used is the second window of each run,
20 frames apart.

## The target signal

| | baseline `3336c491` | `04245442` |
| --- | ---: | ---: |
| `drc_work_arena recycles` | **1** | **0** |
| `drc_work_arena peak_words` | 5,238,788 (99.92% of 5,242,880) | 3,990,300 (76.11%) |
| compiled blocks (cumulative) | 51,838 | 34,222 (-34.0%) |
| `drc_work_dispatch rebuilds` | 83 over 9 sampled frames | 67 over 12 sampled frames |
| `drc_work_codegen` words / block | 167.49 | 117.21 (-30.0%) |

The baseline reaches the arena cap and overflows once inside this window; each
overflow discards every compiled block and forces the working set back through
the interpreter, which is the extra 17,616 compiles in its column. The change
never overflows and finishes the same window at 76% of the arena. This is the
stall signal the code-size work was aimed at, now measured on hardware.

The per-phase codegen totals agree with the host measurement: the exit phase
drops from 55.6 to 28.7 words per block, `complete` from 19.6 to 5.8,
`memguard` from 13.5 to 10.0 and `memaddr` from 48.6 to 36.4, while `body`
(19.0 -> 21.8) and `memaccess` (11.1 -> 14.4) rise by the write-through stores
that replaced the per-exit flushes.

## Timing

Per frame over the whole window:

| | baseline | `04245442` | delta |
| --- | ---: | ---: | ---: |
| `core_frame_ms cpu_io` | 14.379 | 13.966 | -2.9% |
| `core_frame_ms total` | 15.634 | 15.118 | -3.3% |
| `core_phase_ms drc_dispatch` | 13.22 | 12.86 | -2.7% |
| `core_phase_ms blk_entry` | 0.120 | 0.113 | -5.9% |
| `core_phase_ms timers` | 0.104 | 0.100 | -3.5% |
| `core_phase_ms worker_busy` | 5.59 | 5.70 | +2.1% |
| sampled worst frame | 29.487 | 27.841 | -5.6% |
| frames at or above 25 ms | 3 of 37 | 2 of 40 | |

Workload proxies that the window counts in full — not the 1-in-256 sampled work
counters — match within about 1%, and the change does slightly *more* blitter
work:

| window-wide counts | baseline | `04245442` |
| --- | ---: | ---: |
| `worker_jobs` / `blit_calls` | 2,270 | 2,290 (+0.9%) |
| `timer_callbacks` | 96,894 | 97,628 (+0.8%) |

## Limits

The two windows are 20 frames apart and the sampled work counters cover
different frame sets (9 versus 12 sampled frames), so the CPU-side sampled
counters are not workload-matched: `native_calls` per sampled frame reads
35,036 against 42,458 and interpreter steps 740 against 1,729. The window-wide
driver counters above are the comparable workload evidence, and they say the
two windows are within 1% of each other. On that basis the frame times are
consistent with a small gain, but no frame-rate or stall-count number is
claimed from this pair: the deterministic, workload-matched measurement is the
host harness, and the unique thing this run adds is the arena recycle, which is
workload-independent.

## Status

The host-acceptance criteria are met and the console confirms the effect the
host predicted: at the same state and the same cumulative frame region, the
arena no longer overflows, the high-water mark drops from the cap to 76%, and
the cumulative recompile count falls by a third. The image behind the change is
the diagnostics build of `04245442`; the release flavor has not been built, per
the iteration rule.
