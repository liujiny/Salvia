# CV1000: uncapped fusion, re-tried on the 32 MiB arena and rejected by the console

Baseline `c23c9876`. `2d78111e` raised `SALVIA_CV1K_FUSE_CAP` from 3 to 33 on
the host sweep below; this round reverts it to 3 and records why. The host case
for re-opening the experiment was sound, and the console found the cost the host
case did not model.

## Why it was re-opened

`2026-10-09-cv1k-fusion-cap.md` capped the delayed-conditional fall-through at 3
after the console A/B in
`2026-10-09-cv1k-delayed-conditional-fallthrough.md` won on the mean (-2.5% of
`core_frame_ms total` for -16.5% of entries) and lost on the tail: one of 52
samples at 25.55 ms against none in the baseline. The cause that review named
was the cost of the absorbed code, and the loudest number in it was arena
pressure -- **99.9%** of a 20 MiB arena, 4 evictions. Two of those four numbers
have since moved: the arena is 32 MiB (`23a40cf3`, specifically for the fused
working set) and the write-through work cut emitted words per block by 30%
(`04245442`).

## Host sweep, current tree

`ddpdfk` from `user-ddpdfk4-core.state`, `dips=00,07,00,00`, `render_cores=2`,
no input, the cap passed as a `-D` so the sources are identical.

| 1800 frames | cap 3 | cap 8 | cap 33 |
| --- | ---: | ---: | ---: |
| `native_calls` (entries) | 75,606,464 | 70,406,323 (-6.9%) | 66,849,642 (-11.6%) |
| arena `peak_words` | 3,887,980 (46.3%) | 4,593,820 (54.8%) | 5,333,584 (63.6%) |
| `recycles` / `evictions` | 0 / 0 | 0 / 0 | 0 / 0 |
| `interpreter_steps` | 1,971,453 | 2,133,335 | 3,057,677 (+55%) |
| `drc_work_dispatch calls` | 2,162,558 | 2,313,196 (+7%) | **3,228,044 (+49%)** |
| `STATE` | `dff81882efc1b24a` | same | same |
| per-frame hash file (md5) | `3f7580a9` | same | same |

The arena objection was answered: 63.6% instead of 99.9%, no recycle and no
sector reuse, and the emulation is bit-identical at every cap. The last row
before the identity rows -- the chained dispatcher invocation count, +49% -- is
the one that decided the round, and it was in this table before the console ran:
a fused block is longer in guest cycles, so it no longer fits the guest cycles
left in the current slice, the dispatcher refuses it, the outer loop interprets
one instruction and re-enters. The sweep recorded the number and the review read
past it.

## Console A/B

`Distro360/fbneo-ab-base-cap3-diag.xex` (`c23c9876`, sha256 `20671377…`) against
`Distro360/fbneo-ab-cand-uncapped-diag.xex` (`2d78111e`, sha256 `bbebb82e…`),
two diagnostics images built with the same script and differing only in that
macro. Same state, no input, `dips=00,07,00,00`, `render_cores=2`, 3-4 pauses
each.

| per frame | base cap 3, frames 11..3528 | cand cap 33, frames 228..3734 |
| --- | ---: | ---: |
| timing samples | 52 | 51 |
| `worker_jobs` (exact) | 0.935 | 0.935 |
| `timer_callbacks` (exact) | 36.06 | 36.07 |
| `entry_samples` (1-in-64) | 692.0 | 613.1 (**-11.4%**) |
| chained dispatch calls (exact) | 1,453.4 | 2,521.2 (**+73.4%**) |
| `drc_dispatch` ms (exact) | 14.222 | 13.749 (**-3.3%**) |
| `dispatch_pre` ms, raw 1-in-64 | 0.1391 | 0.1290 (-7.3%) |
| `blk_entry` ms, raw 1-in-64 | 0.1228 | 0.1176 (-4.2%) |
| `dispatch_post` ms, raw 1-in-64 | 0.0390 | 0.0353 (-9.5%) |
| `interpreter_steps` (1-in-256) | 18,375 | 33,566 (+82.7%) |
| arena `peak_words` | 4,042,812 (48.2%) | 5,460,072 (65.1%) |
| arena `recycles`/`evictions` | 0 / 0 | 0 / 0 |
| `codegen` words/block | 125.9 | 165.6 (+31.5%) |
| `cpu_io` ms (52 samples) | 15.298 | 15.853 (+3.6%) |
| `core_frame_ms total` | 16.288 | 16.865 (+3.5%) |
| worst sample | 24.829 ms (frame 721) | 24.351 ms (frame 1485) |
| frontend `game_and_ui` | 16.617 | 17.162 (+3.3%) |
| frontend `present` | 1.621 | 0.794 |
| frontend `sampled_active_loop` | 18.659 | 18.698 (+0.2%) |

## Reading

- The fusion does what it was built to do: **-11.4% entries** on the console,
  matching the host's -11.6%, and the dispatch phase drops 0.47 ms/frame (its
  three sampled spans drop 7.3%, 4.2% and 9.5%).
- The cost the host sweep saw only as a counter is large enough to eat it:
  **+73% chained dispatcher invocations**, +1,068 per frame, and +82.7% of the
  interpreter steps those bails perform. The dispatch phase's own timer goes
  *down* by 0.47 ms while `cpu_io` goes *up* by 0.56 ms, so the extra work is
  outside the timed dispatch call: one interpreted instruction plus one loop
  pass per bail. That is ~0.96 µs of non-dispatch work per extra invocation.
- The two always-on driver counters match (0.935 jobs/frame, 36.06 callbacks),
  so the windows did comparable external work; but the *frame-time* measures
  are only ~1 sigma apart, and the whole-iteration frontend measure
  (`sampled_active_loop`, +0.2%) is flat while the core's own view (+3.5%) is
  worse. On this evidence there is no demonstrated gain, and one exact counter
  (invocations) says the structural cost is real, so the default goes back to 3.

## What the round is worth keeping

| coefficient | value | source |
| --- | ---: | --- |
| one chained dispatcher invocation, outside the timed phase | ~0.96 µs | this run: +1068 calls vs +1.02 ms of non-dispatch `cpu_io` |
| one removed entry, off the dispatch phase | ~93 ns (~300 cycles) | this run: -11.4% entries vs -0.47 ms |
| arena cost of uncapped absorption | 46.3% -> 63.6% | host, 1800 frames |

The first of those is the new one and it is a gate for anything that changes
block formation: `drc_work_dispatch calls` per frame is exact, host-visible and
cheap to read, and it was in this round's own sweep at +49% for cap 33 before
any console time was spent. A fusion setting that raises it by double digits
needs to save far more than a small cap does, because 1,453 invocations/frame at
~1 µs is already ~1.4 ms of the frame's 16.3 ms.

It also settles the tail question the cap was introduced for: with the 32 MiB
arena, neither window contains a sample at or above 25 ms (peaks 24.83 and
24.35), so uncapped fusion no longer costs the tail -- it costs the mean.

## Limits

- The windows are not the same range: the baseline's covers frames 11..3528 and
  the candidate's 228..3734, so the baseline carries the post-state-load stretch
  that the candidate's *separate* first window absorbed. Whatever that stretch
  costs, it biases the baseline upward, which is the direction that flatters the
  candidate; the exact counters that decide this round (entries, invocations,
  `drc_dispatch`) do not depend on that bias.
- 52 against 51 samples: the per-frame means carry roughly ±0.4 ms each, so the
  +0.56 ms of `cpu_io` and the -0.48 ms of the worst sample are inside one sigma
  of each other. The conclusion is "no demonstrated gain plus a measured
  structural cost", not "worth exactly +3.5% of frame time".
- Host `USER` time is blind to this entirely (325.9 s against 325.8 s over 1800
  frames), which is why the console run existed.

## Revert

This round's commit reverts `2d78111e` (`SALVIA_CV1K_FUSE_CAP` back to 3) and
replaces its document with this one.
