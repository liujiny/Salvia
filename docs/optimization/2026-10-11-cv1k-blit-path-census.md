# CV1000: attribute every replayed pixel to the inner loop that drew it

Baseline `71e6cdda`, whose console result is `2026-10-11-cv1k-blit-threshold.md`.
The counters are counting-only: they are compiled out unless
`SALVIA_CV1K_BLIT_PATH_PROBE` is on, the console default is off, and no
emulation state is read or written.

## Why

The blitter job is timed as one number and its CPU replay as another, but the
replay is a mixture of inner loops with very different costs per pixel: an
untinted copy, an exact integer blend, and the table-driven generic loops.
`2026-10-11-cv1k-blit-threshold.md` weighed the replay at 25.9 ns per pixel,
which is an average over that mixture and therefore cannot say which loop is
worth making faster. Optimizing an inner loop that draws 0.2% of the pixels is
free to measure and worth nothing.

So the replay was attributed: every pixel a replayed command draws is charged to
the loop that drew it, and the mixture becomes visible.

## What

- `salvia_cv1k_blit_paths.h` names the twelve loops `epic12_gpu_replay()` can
  select -- `simple_f{0,1}_tr{0,1}`, `fixed_f{0,1}_tr{0,1}`, `tinted_plain`,
  `blend_d1`, `blend_d4`, `UNKNOWN` -- and declares the counters.
  `EPIC12_BLIT_PATH_CHARGE(pixels)` is called **once per row**, before the row
  loop runs, so an early `continue` in the per-pixel body cannot skew the count
  and the probe cannot perturb what it measures.
- `epic12_blit_path_index()` in `epic12_gpu.h` mirrors the dispatch in
  `epic12_gpu_replay()`; `UNKNOWN` is the census's own alarm that the two have
  drifted apart. `epic12_gpu_replay()` sets `salvia_cv1k_blit_path_current`
  around each command and charges the command to its loop before the clip maths
  runs, so a command the source-wrap check then rejects still counts as
  dispatched. `epic12_gpu_flush()` clears the marker after the replay loop.
- `EPIC12_BLIT_PATH_CHARGE` is also called by the two inner loops that are not
  reached through `epic12_gpu_replay()`'s own dispatch: the generic row loop in
  `epic12in.inc` (before `while (bmp<end)`) and `epic12_blend_fixed()` in
  `epic12_fast_blit.h` (before its row loop).
- The host harness prints `BLITPATH <name> px= cmds= px_share= px_per_cmd=` and
  a `TOTAL` line; the console carries no such line, so this census is a host
  measurement.

## What the census says

`ddpdfk`, `user-ddpdfk4-core.state`, dips `07,00,00`, `render_cores=2`, 600
frames, so the split point is the DIP under test: `epic12_gpu_flush()` routes a
whole flush group by `epic12_gpu_pixels < epic12_gpu_threshold`, and
`epic12_gpu_replay()` then walks that group command by command.

`CV1K_DIPD=00` -- the default 8192 split, i.e. only the groups smaller than
8192 pixels reach the replay. `STATE baabc4def027a8a3`, the same hash the
probe-free build produces at 600 frames, so the probe is non-invasive:

| path | px | cmds | px share | px/cmd |
| --- | --- | --- | --- | --- |
| `simple_f0_tr1` | 64 | 2 | 0.03% | 32.0 |
| `simple_f1_tr1` | 256 | 1 | 0.11% | 256.0 |
| `fixed_f0_tr1` | 235,792 | 3,420 | 98.27% | 68.9 |
| `blend_d1` | 3,840 | 6 | 1.60% | 640.0 |
| `TOTAL` | 239,952 | 3,429 | | 70.0 |

`CV1K_DIPD=128` -- All CPU, i.e. `epic12_gpu_threshold = 1u << 24`, so the
replay sees every group the default route would have handed to the Xenos.
`STATE baabc4def027a8a3` again, same hash:

| path | px | cmds | px share | px/cmd |
| --- | --- | --- | --- | --- |
| `simple_f0_tr0` | 27,033,600 | 352 | 4.65% | 76,800.0 |
| `simple_f0_tr1` | 99,620,623 | 166,688 | 17.15% | 597.6 |
| `simple_f1_tr1` | 6,737,216 | 33,954 | 1.16% | 198.4 |
| `fixed_f0_tr0` | 9,826,620 | 7,776 | 1.69% | 1,263.7 |
| `fixed_f0_tr1` | 398,200,298 | 308,885 | 68.55% | 1,289.2 |
| `fixed_f1_tr1` | 24,479,880 | 44,377 | 4.21% | 551.6 |
| `tinted_plain` | 14,109,136 | 30,635 | 2.43% | 460.6 |
| `blend_d1` | 868,320 | 1,872 | 0.15% | 463.8 |
| `TOTAL` | 580,875,693 | 594,539 | | 977.0 |

### The generic table-driven blend is not a target

Over 594,539 dispatched commands -- 968,126 pixels per frame -- the table-driven
generic loops are `blend_d1` 0.15%, `blend_d4` 0.00% and `UNKNOWN` 0.00%.
`UNKNOWN` being zero over that many commands is the mirror dispatch agreeing
with the real one, so the buckets are complete rather than the entry being
unreachable. Making `epic12pixel.inc`'s dmode tables faster would be optimizing
0.2% of the replay. That half of the question is answered and closed.

### The exact integer blend is the replay

`epic12_blend_fixed()` (`epic12_fast_blit.h`) is `fixed_f0_tr1` 68.55% plus
`fixed_f0_tr0` 1.69% plus `fixed_f1_tr1` 4.21% -- 74.45% of the replayed pixels,
and 98.27% of them on the default route. Everything else the replay does is
`simple` copies (23.0%) and tinted copies (2.4%).

Two details worth keeping:

- `simple_f0_tr0` is 27,033,600 px in 352 commands at All CPU, i.e. 76,800 px
  per command -- exactly one 320x240 screen. The untinted opaque path is used
  for whole-screen copies, not for sprites, and it is 4.65% of the replay.
- On the default route the replayed `fixed_f0_tr1` commands average 68.9 px
  against 1,289.2 px at All CPU. The split routes whole flush groups, so what
  stays on the replay is small batches of small sprites; the pixels are
  elsewhere.

## What this does not say

- **The census is `ddpdfk`; the console numbers are `mmpork`.** `mmpork`'s state
  does not boot to a picture on the host, so the title that has console
  measurements cannot be censused this way. The two are not comparable
  numerically: `mmpork` spends 3.03 ms per frame in 139 replays of ~824 pixels
  (~117k px/frame) while `ddpdfk`'s default route replays 400 px/frame. The
  default-route columns above are therefore a statement about `ddpdfk`'s small
  batches, and the All-CPU column is the one that shows the title's real mix.
- The census counts what the replay draws, not where the frame's time is. On the
  console the default job is 25.30 ms, of which the replay is 3.03 ms. The mix
  says which loop to make faster *if* the replay is the target; it does not say
  the replay is the target.
- Pixels a command draws outside its clip rectangle are not charged; the charge
  is the clipped rectangle, which is what the loop actually writes.

## What this leaves

Ranked by the console's own per-frame split of the default job (25.30 ms), with
the per-batch terms from `cv1000-gpu.log`'s steady-state `batch_timing` line
(10616 batches: `source_upload` 0.355, `input_upload` 0.415, `untile_mask`
0.448, `readback_wait` 0.176, `submit` 0.137, `plan` 0.063, `lock` 0.006 ms per
batch, 9.6 batches per frame):

| term | ms/frame | state |
| --- | --- | --- |
| `untile_mask` (result texture -> vram) | 4.30 | VMX already |
| `input_upload` (vram -> input texture) | 3.98 | scalar `memcpy` per row, no prefetch |
| `source_upload` (atlas page swizzle) | 3.40 | VMX already |
| replay | 3.03 | `epic12_blend_fixed` 98.3% of it |
| command walk and construction | 6.85 | unmeasured inside the job |

The replay's ceiling is its own 3.03 ms, so even a perfect `epic12_blend_fixed`
buys ~1.5 ms there. The three copies are 11.7 ms and the two that are not yet
VMX-or-prefetched -- `input_upload` first of all, since it is a plain strided
row copy -- are the larger term on the route this title actually plays. That is
the next candidate.

## Verification

Host, `/home/humor/salvia-tests/cv1k-ddpdfk-profile-20261007/`, built with
`epic12.cpp` touched after the header edits (the build looks at the `.cpp`
timestamps, so a header-only edit measures the previous objects -- this has
already cost one round):

```
root=/home/humor/salvia-tests/toolchains/ppc/root
LD_LIBRARY_PATH=$root/usr/lib/x86_64-linux-gnu CV1K_DIPB=07 CV1K_DIPC=00 CV1K_DIPD=0 \
  $root/usr/bin/qemu-ppc -cpu g4 ./game-dip /home/humor/salvia-tests/cv1000-boot/ddpdfk \
  1 600 /tmp/blitpath600 /home/humor/salvia-tests/cv1k-ddpdfk-opt-20261007/user-ddpdfk4-core.state 0 2
```

`CV1K_DIPD` is decimal: `0` is the default 8192 split, `128` is All CPU. Both
runs print `STATE baabc4def027a8a3`, which is the 600-frame hash of the
probe-free build, and that is the evidence that the probe changes nothing.

| frames | `STATE` | per-frame hash md5 |
| --- | --- | --- |
| 60 | `0cf251c3d512ddbb` same | `c42fdb4870f4f997112d88ff6f74ee12` same |
| 600 | `baabc4def027a8a3` same | `022b01ad0c0b543afd34a685e07173c5` same |
| 1800 | `dff81882efc1b24a` same | `3f7580a932f3a859ae847bf3f3fe830b` same |

Suites, all PASS: `tests/sh3_ppc`; `tests/epic12_gpu`; `tests/epic12_blit`;
`tests/epic12_gpu_cache` (fingerprint `21d3a8b4487bb152` on the host and on the
PPC build); `tests/epic12_gpu_untile` (6,820 rectangles, 66,356,536 exact
pixels, both endiannesses); artifacts in
`/home/humor/salvia-tests/cv1k-blit-census-20261011/`. The console build
compiles the counters out
(`SALVIA_CV1K_BLIT_PATH_PROBE` defaults to 0), so no image changes behaviour.

## Limits

- One title, one state, no input, 600 frames.
- The counters are host-only for now. A console census of `mmpork` -- where the
  console numbers actually come from, and where the default route would be
  exercised -- needs a report line in the pause report and the flag enabled in
  the diagnostics flavor.
- Charging happens once per row, so a command that draws zero rows is counted in
  `cmds` and contributes zero pixels.

## Revert

Revert this commit: delete `salvia_cv1k_blit_paths.h`; remove the
`SALVIA_CV1K_BLIT_PATH_PROBE` switch from `salvia_fbneo_diagnostics.h`, the
counter definitions from `epic12.cpp`, the include, the mirror dispatch, the
marker assignments and the two charges from `epic12_gpu.h`, and the two `CHARGE`
calls from `epic12in.inc` and `epic12_fast_blit.h`.
