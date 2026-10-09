# CV1000: split the dispatcher phase so the next console run can attribute it

Diagnostics-only change (`SALVIA_FBNEO_DIAGNOSTICS`, `SALVIA_CV1K_PROBE`);
no emulation code is touched.

## Why

The console A/B logs put `drc_dispatch` at 12.86 ms of a 13.97 ms `cpu_io`
(`2026-10-09-cv1k-codegen-shrink-console.md`), and inside that the 1-in-64
block-entry span scales to about 3.3 ms of generated code
(`2026-10-08-cv1k-phase-split-result.md`). The other ~9 ms has no attribution:
`2026-10-08-cv1k-cpu-io-split.md` ends with exactly that gap — "either the cost
is not the generated code, or it is a cost that the counters do not
attribute" — and the emitter-link experiment then showed that removing 22-25%
of the chained block entries moved the same phase by only 2.4%, so the gap is
not proportional to per-entry dispatcher work either.

Until that gap is attributed, choosing the next lever is guesswork: shrinking
the emitted code further, changing the metadata layout and interleaving the
arena all bet on different explanations of the same missing milliseconds, and
only a console can tell them apart.

## What

Three spans now sit on the *same* 1-in-64 sampled entry, so they can be
compared with each other and with the whole phase:

| Field | Span |
| --- | --- |
| `dispatch_pre` | top of the chain loop to just before the entry call: `MemMapF` fetch, lookup, source validation, and any recompile that lands on a sampled entry |
| `blk_entry` | the generated block call (unchanged) |
| `dispatch_post` | after the call: the idle/device service dispatch and the cycle accounting |

All three use one sample counter (`entry_samples`), so per-frame values are
`ticks/samples`; the printed numbers are raw per-frame ticks, exactly like
`blk_entry`. The full phase is printed beside them as `drc_dispatch` with
`dispatch_calls`, so the per-entry remainder is
`drc_dispatch / (entries per frame)` minus the three spans.

Implementation: `salvia_cv1k_probe.h` extends the counter array to 16 entries,
`sh3_drc_dispatch.h` takes the tick marks, `epic12.cpp` sizes the array and
`d_cv1k.cpp` prints and clears the new fields.

## Verification

The probe is compiled out unless `_XBOX` and `SALVIA_FBNEO_DIAGNOSTICS` are
both set, so host builds do not contain it: the harness 60-frame run still
prints `STATE 0cf251c3d512ddbb` with a byte-identical hash file, and
`tests/sh3_dispatch/run_inline.py` passes 8 suites. The diagnostics XEX built
and linked with the probe in place, which is what actually compiles this code
path.

## Console image

Diagnostics flavor only, as the iteration rule requires. The image for this
commit is `5c5e06500506bd1b9df22acace7160793d5b7ad1a118014368028a4fc8690341`,
34,631,680 bytes, archived as
`xex-archive/fbneo-20261009-0907-dispatch-probe-split-diag.xex`, deployed as
`Distro360/fbneo.xex` / `fbneo-diag.xex` and kept as
`Distro360/fbneo-dispatch-probe-split-diag.xex`. The matched `3336c491`
baseline image is unchanged, so a console A/B can pair this image's
`core_phase_ms` split against the baseline's arena/recycle counters in one run.
The earlier `43611815...` build of the same source (same tag, before this commit
landed) is an archived duplicate; the deployed image is the one above.

## Limits

Each span includes its own `QueryPerformanceCounter` read (~30-50 ns), so a
250-700 ns span is inflated by roughly 5-20%; the values are for comparing the
three spans and the phase, not for absolute per-entry cost. Samples that leave
the loop early (`no_entry`, `short_budget`, about 1.8% of entries) are dropped,
which biases the spans against the cheapest entries. The pre span includes any
recompile that lands on a sampled entry.

## Revert

Revert this commit: `salvia_cv1k_probe.h`, `epic12.cpp`, `sh3_drc_dispatch.h`
and the `core_phase_ms` line in `d_cv1k.cpp`.
