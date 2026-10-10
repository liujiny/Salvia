# CV1000 console A/B: the shadow dispatch record takes the frame under budget

Two diagnostics images, same state (`user-ddpdfk4-core.state`), no input,
`ddpdfk`, `dips=00,07,00,00`, `render_cores=2`, one pause each after the state
load and one at the end. `interval_frames` heads the window it belongs to, so a
window's cumulative range is the running sum of those lengths:

| | base | candidate |
| --- | --- | --- |
| build tag (self-reported in the log) | `shadow-base 55794c23 20261010-2009 diag` | `shadow-cand 8bed73ba 20261010-2019 diag` |
| image SHA256 | `ac54c572e819acf9f32fb6c7b1a4fe1985f691087a83f62f06af1a045bb25269` | `ad61b89f90f6877e9cde86f45ab4ed0ef408c9cd93418560bc02243ee10048d8` |
| first window (cold state load) | 60 frames, 0..60 | 19 frames, 0..19 |
| usable window | **3530 frames, 60..3590** | **3511 frames, 19..3530** |
| raw log | `Distro360/base.log` | `Distro360/fbneo-ab-shadow-cand.log` |

The two windows are the same length to 0.5% but cover different stretches: the
candidate's starts 41 frames into the cold DRC warm-up, which biases *its* spans
upward, and neither run's pauses line up with the other's. The comparison below
is therefore not a workload-matched pair in the sense the earlier A/B documents
use; what makes it usable is that the per-frame driver counters match.

## Workload

| per frame | base | candidate |
| --- | ---: | ---: |
| `worker_jobs` / `blit_calls` | 0.935 | 0.935 |
| `timer_callbacks` | 36.057 | 36.066 |
| probe `entry_samples` (1-in-64) | 690.5 | 692.6 |
| `drc_work_arena peak_words` | 4,043,056 | 4,042,520 |

## The mechanism ran

`drc_work_shadow hits=528,443 entries=535,522 hits_pct=98 pred_miss_pct=8
touch=1`, `drc_work_dispatch lookups=7,079` against the base's `689,836`: 98% of
the window's entries were answered by the copy, matching the host's 99%.

## Timing

| | base | candidate | delta |
| --- | ---: | ---: | ---: |
| `core_frame_ms cpu_io` | 16.309 | 13.394 | **-2.915 ms (-17.9%)** |
| `core_frame_ms total` | 17.318 | 14.408 | -2.910 ms (-16.8%) |
| `core_phase_ms drc_dispatch` | 14.214 | 12.207 | **-2.006 ms (-14.1%)** |
| probe `dispatch_pre` /frame | 0.139 | 0.092 | -33.5% |
| probe `blk_entry` /frame | 0.122 | 0.142 | **+16.4%** |
| probe `dispatch_post` /frame | 0.039 | 0.036 | -7.6% |
| frontend `game_and_ui` | 17.666 | 14.755 | **-2.911 ms (-16.5%)** |
| frontend `present` | 1.174 | 1.240 | +0.066 |
| frontend `limiter` | 0.478 | 1.306 | +0.828 |
| frontend `sampled_active_loop` | 19.318 | 17.300 | -2.018 ms (-10.4%) |
| `core_sampled_peak_ms` | 25.317 | 24.085 | -4.9% |
| frames at or above 25 ms | 1 of 51 | **0 of 51** | |

The probe spans are the three 1-in-64 dispatcher spans, quoted per frame with
their sample counts (690.5 against 692.6 per frame, so the two are directly
comparable) and per sampled entry to take the sampling out of it.

## Reading it

1. **The pre-entry span falls by a third** (-33.5% per frame, 0.201 -> 0.133 us
   per sampled entry) and the *non-sampled* dispatch phase falls 2.006 ms. That
   is the effect the change was built for: the set lookup, the record read and
   the validation are gone for 98% of entries, and what replaced them is one
   line that the block itself fetched a block execution earlier.
2. **The frame is under budget.** `game_and_ui` 17.666 -> 14.755 ms against a
   16.67 ms frame, and the frontend `limiter` -- the time the loop spends *not*
   being late -- rises by 0.83 ms. The base window was over budget on every
   frame; the candidate window has 0.83 ms of headroom on the work alone.
3. **The generated code pays part of it back.** `blk_entry` rises 16.4%, which
   is ~0.02 ms/frame of the sampled span but the same order as the prefetch
   traffic: the fast path fetches one 128-byte line per entry, and the lines
   evict generated code from L1/L2 while the code is the next largest working
   set. Some of the rise is also the candidate window's 41 extra warm-up frames
   (cold DRC, cold caches), so this is where a matched pair or a tuned prefetch
   would earn more, not where the change is wrong.
4. The worst frame drops with everything else: 1 of 51 samples at or above
   25 ms against 0 of 51, `core_sampled_peak_ms` -4.9% and the frontend
   `sampled_peak` -4.8%.

## Limits

- Two runs, one pause each, windows covering 60..3590 against 19..3530. The
  per-frame driver counters match, but the frame ranges do not, so the split
  between the pre-entry gain and the block-entry payback is approximate; the
  headline (`cpu_io`, `drc_dispatch`, `game_and_ui`) is not sampled and is not
  affected by it.
- The console cannot show a `STATE` hash, so the determinism claim rests on the
  host (identical `STATE` and per-frame hash files at 60, 600 and 1800 frames)
  plus the console counters that do match (`entry_samples`, arena peak words).
- `blk_entry`'s rise is not separated from the window bias here. A matched pair,
  or `SALVIA_CV1K_SHADOW_TOUCH=0` against 1 at the same revision, would size it.

## Next

The generated code is now the largest piece of the dispatch phase and the
pre-entry is no longer first: `drc_work_codegen` is unchanged by this commit
(167.5 words per block both windows, exit 30.6, memaddr 39.8), so the two emitters
the original plan named -- a shared tail for the guard exits and one
materialisation of the 32-bit address constants -- are the next lever, and they
shrink the working set the prefetch is competing with at the same time.

## Revert

Revert this document only; the mechanism is `2026-10-10-cv1k-shadow-record.md`.
