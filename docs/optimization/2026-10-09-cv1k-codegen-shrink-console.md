# CV1000 console A/B: the code-size shrink removes the arena recycle

Two diagnostics images, same state (`user-ddpdfk4-core.state`), no input,
`ddpdfk`, `dips=00,07,00,00`, `render_cores=2`, two pauses each.

`interval_frames` is the length of the window it heads, not a cumulative frame
number: `cv1k_review_report()` clears the review profile and `Sh3WorkReport()`
clears the work profile after printing, so every `drc_work_*` counter and every
timing mean describes that window alone. A window's cumulative range is the
running sum of those lengths, and that is what has to line up for two windows to
describe the same scene (the emulation is deterministic).

| | baseline | this work |
| --- | --- | --- |
| build tag | `codegen-shrink-base 3336c491 20261008-2323 diag` | `codegen-shrink-r4 04245442 20261009-0031 diag` |
| image SHA256 | `3faa500e48cb42244eaed5a9cab105f7329ac8eed1169d53f062ff24d1e35d94` | `29d56e8ac2d3cfdb35052fb787c82ad54d5fadbd18324c71cdb30f179026601f` |
| first window | 1491 frames, 0..1491 | 48 frames, 0..48 (cold, unusable) |
| usable window | 2613 frames, 1491..4104, 37 timing samples | 2633 frames, 48..2681, 40 timing samples |
| raw log | `xex-archive/console-logs/cv1000-gpu-codegen-shrink-base.log` | `xex-archive/console-logs/cv1000-gpu-codegen-shrink.log` |

The two usable windows are almost the same length but cover different stretches:
the change's starts right after the state load and carries the cold-DRC warm-up,
the baseline's starts 1491 frames in, and neither run's pauses line up with the
other's. This is therefore **not** a workload-matched pair in the sense the
earlier A/B documents use, and the comparison below is stated with that limit.

## The target signal

| per window | baseline `3336c491` | `04245442` |
| --- | ---: | ---: |
| `drc_work_arena recycles` | **1** | **0** |
| `drc_work_arena peak_words` | 5,238,788 (99.92% of 5,242,880) | 3,990,300 (76.11%) |
| compiled blocks (cumulative) | 51,838 | 34,222 (-34.0%) |
| `drc_work_dispatch rebuilds` | 83 over 9 sampled frames | 67 over 12 sampled frames |
| `drc_work_codegen` words / block | 167.49 | 117.21 (-30.0%) |

The baseline reaches the arena cap and overflows once inside its 2613-frame
window; each overflow discards every compiled block and forces the working set
back through the interpreter, which is the extra 17,616 compiles in its column
(`drc_work_codegen` is the one cumulative counter, so those two columns are
comparable). The change runs a slightly longer window from a colder start
without overflowing and ends at 76% of the arena. The same-frame-count version
of this comparison is the host harness (`2026-10-09-cv1k-write-through-registers.md`):
1800 frames 5,127,536 against 3,662,568 words, 3000 frames one recycle against
none.

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

Per frame over each window, the counters that accumulate on *every* frame — not
the 1-in-256 sampled work counters — match to better than 0.5%, and the change's
window does marginally more driver work:

| per frame | baseline | `04245442` |
| --- | ---: | ---: |
| `worker_jobs` / `blit_calls` | 0.869 | 0.870 |
| `timer_callbacks` | 37.11 | 37.08 |
| `dispatch_calls` | 1450.3 | 1449.2 |

## Limits

The windows cover different frame ranges, and the CPU-side work counters are
sampled once per 256 frames, so 9 versus 12 samples in a ~2600-frame window
do not establish that the CPU workload matched: `native_calls` per sampled
frame reads 35,036 against 42,458 and interpreter steps 740 against 1,729.
`cpu_io` is almost entirely the DRC dispatch path, so a 20% CPU-work difference
is by itself the size of the 3% frame-time difference. The per-frame driver
counters above are what does match, and they only show that both windows ran
comparable input/GPU/timer loads. The frame times are therefore consistent with
a small gain and nothing more; the deterministic, workload-matched measurement
is the host harness, and what this run uniquely adds is the per-window arena
recycle, which does not depend on the workload.

## Status

The host-acceptance criteria are met and the console confirms the effect the
host predicted: at the same state, in a window of the same length, the arena no
longer overflows, the high-water mark drops from the cap to 76%, and
the cumulative recompile count falls by a third. The image behind the change is
the diagnostics build of `04245442`; the release flavor has not been built, per
the iteration rule.
