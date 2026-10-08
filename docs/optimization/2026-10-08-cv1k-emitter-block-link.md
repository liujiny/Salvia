# CV1000 Xbox 360: linking SH3 blocks in the generated epilogue

Baseline: `ab9407703df85570fa9056d6413ac9297b96771c`. This is the emitter half
the phase-split result (`2026-10-08-cv1k-phase-split-result.md`) left open after
the dispatcher half (`bb2455d6`) measured slower and was reverted.

## What changed

Three pieces, one contract:

1. **Compiled-page bitmap and code epoch.** `compile()` marks the 64 KiB page
   that holds a new block (`(pc & AM) >> 16`, a 4096-byte bitmap, so every
   external alias folds onto the interpreter's page). A single `sh3_code_epoch`
   is the invalidation token.
2. **Store-side revocation.** A generated store computes its page number and, if
   that page holds compiled code, bumps the epoch inline. The store still runs
   natively; the block's existing own-bytes range guard still protects the
   executing block, so a store that rewrites a *different* block only revokes
   the links to it. Interpreted RAM writes (`WB`/`WW`/`WL`) and the CV1000
   serial-flash DMA path call the same hook. Every path that drops or replaces
   generated code also bumps the epoch: `sh3_drc_reset` (which state load,
   `Sh3Reset`, mapping changes, `Sh3SetDrcRam`, `Sh3SetDrcReadMirror` and
   `Sh3SetDrcDeviceRead` all reach), the arena recycle in `compile()`, and
   `sh3_drc_exit`.
3. **Emitter link.** A block whose clean completion has a compile-time constant
   successor (the taken edge of a non-delayed `BT`/`BF`, a `BRA`/`BSR` whose
   delay slot folded, or falling off the block end) ends its epilogue by
   comparing its own link slot against the successor, the current epoch and the
   remaining budget, and tail-calls the successor's generated entry with
   `bctr` when they match. The dispatcher fills a block's slot with the record
   it validated for the PC that followed it. Self-loops keep the older in-block
   branch. The `Block` record layout, the arena size and the 128-source-page
   GPU batching limit are untouched; the link slots are a separate 2 MiB array.

The slot is only a cache. The epilogue rechecks the target, the epoch and the
entry, so a slot handed to a different block (after a tail-linked chain) or a
slot left by an evicted block can only cost a missed link, never run the wrong
code.

## Why this is safe against guest code rewriting itself

The dispatcher still revalidates every block it enters. A link skips that for
one successor, and the epoch is what makes the skip equivalent:

- a generated store on a compiled page bumps the epoch (step 2);
- an interpreted store or a DMA write to a compiled page bumps it through the
  `WB`/`WW`/`WL` hook;
- a state load (`DrvScan` with `ACB_WRITE`) and any DRC reset/recycle bump it.

The only writer that could bypass all three is a host-side direct write to guest
RAM, which is not a guest path in this tree. The CV1000 cheat subsystem is not
registered for SH3 memory writes (`Sh3WriteByte` has no definition, and no
`cpu_core_config` exists for the SH3), so register-only cheats never touch a
code page; if a memory cheat path is ever added it must bump the epoch.

## Verification

Host PowerPC differential suite (`tests/sh3_ppc/run.py`, which executes the
emitted PPC under `qemu-ppc -cpu g4` and compares against the interpreter):

```
PASS 842085 cases compiled 724270 fallback 117815
EDGE chained block links / two-block loop / live-link revocation PASS cases=5
```

The new edge case runs a two-block `BRA` loop through the chained dispatcher:
the generated epilogues link, the run must match the interpreter exactly, and a
rewrite of a live link target must be revoked. A standalone check confirmed the
case is not vacuous: with the epoch bump removed, the same rewrite makes the
DRC diverge from the interpreter (26 vs 38 increments), and with the hook it
agrees (38 vs 38).

Other host suites: `tests/sh3_dispatch/run_inline.py` (8 suites, optimized and
ASan/UBSan) and `tests/sh3_hot_fallback/run.py` all pass. The `sh3_dispatch`
fixture now declares the link slot the production dispatcher writes; its
synthetic callback never links, so the control-flow trace is unchanged.

Game regression on the PowerPC harness, `ddpdfk` from
`user-ddpdfk4-core.state`, `dips=00,07,00,00`, `render_cores=2`:

| Run | Result |
| --- | --- |
| 60 / 120 / 300 frames, baseline vs linked | per-frame video+audio hashes identical, final state identical (`0cf251c3d512ddbb` at 60) |
| `jit=1,2,3,4` (DRC on, DRC toggling, interpreter, mid-run state reload) | identical for every mode |
| `ddpsdoj` 60 frames | identical hashes; `native_calls` 23,245,299 -> 16,202,238 (-30%) |

Dispatch counters (`ddpdfk`, 60 frames): `lookups` 5,711,937 -> 4,386,073
(-23%), `native_calls` 5,658,274 -> 4,332,410 (-23%). The drop is the number of
successors that now run from a link instead of a dispatcher round trip.
`drc_work_arena peak_words` rose 1,702,184 -> 1,862,548 (+9.4%) from the added
guard and epilogue instructions.

## Validation limits

The host evidence above is a PowerPC build under QEMU. **No console frame rate
is claimed**, because the host cannot run the XEX.

Wall time on the QEMU host is bimodal (each binary lands near either 14.9 s or
18.6 s depending on machine state), so only the minimum over interleaved runs is
usable: baseline 14.81 s, epoch+guard only 14.86 s, epoch+guard+link 14.97 s for
the same 60-frame workload. That is neutral within the noise, while the
dispatcher counters show a real 23% reduction in dispatcher entries.

QEMU executes the generated PPC and the C++ dispatcher both at native speed, so
it does not reproduce the Xenon's per-dispatch cache-miss stalls that the
phase-split result identified as the frame cost. A neutral host wall time
therefore neither supports nor refutes a console gain; the console number
requires a real run of the XEX below.

## XEX identity

The mirror and build are scripted: `/home/humor/salvia-tests/xex-build/build-xex.sh`.
It mirrors only the reviewed `libretro/FBNeo/src` paths into the runtime tree
(refusing to overwrite runtime drift unless `--force`), builds the FBNeo core
and the Salvia frontend with the packaged VS2010/XDK toolchain over WSL Windows
interop, checks the artifact, archives it with its logs, and deploys it. The run
for this change was

```
build-xex.sh --tag emitter-link --baseline ab940770 --expect-symbol sh3_code_epoch
```

which mirrored the four changed files (each verified to match `ab940770` in the
runtime tree first), built both flavors, and confirmed `sh3_code_epoch` is
present in the linked `libretro.lib`. Both flavors are `XEX2`. The container is
encrypted, so the embedded PPC PE machine type is not file-verifiable here, and
this toolchain package has no `xextool.exe` (the build log warns the XEX is
copied uncompressed). Sizes match the archived known-good release class
(34,598,912) and the prior diagnostics images (34,631,680).

| Image | Size | SHA256 | Archive | Deployed |
| --- | ---: | --- | --- | --- |
| release (`SALVIA_FBNEO_DIAGNOSTICS 0`) | 34,598,912 | `e02d480025504f42f983dfbdbf81b01344cb9a7cc6aac536cf90567dbad67fda` | `xex-archive/fbneo-20261008-emitter-link-release.xex` | `Distro360/fbneo.xex`, `fbneo-release.xex` |
| diagnostics (`SALVIA_FBNEO_DIAGNOSTICS 1`) | 34,631,680 | `ee01a8395d50118f837bb004814d5bd9ab6f6319d9a88f35be88318471578b99` | `xex-archive/fbneo-20261008-emitter-link-diag.xex` | `Distro360/fbneo-diag.xex` |

`Distro360/fbneo.xex` is the primary flavor (release); the diagnostics image is
`Distro360/fbneo-diag.xex`. `Distro360/` is gitignored, so the images are
delivery artifacts, not commits. `salvia-tests/xex-build/artifact-emitter-link.json`
records both images with their build times.

What to measure on the console: frame rate under `ddpdfk`/`ddpsdoj` with the
release image, and `drc_work_dispatch lookups`/`native_calls` plus
`core_phase_ms drc_dispatch` with the diagnostics image, where the link shows up
as fewer dispatcher entries and a smaller dispatch phase.

## Revert

Revert this commit. It touches only `sh3_drc_ppc.h`, `sh3_drc_dispatch.h`,
`sh4.cpp`, `sh4dmac.inc` and the two host tests; no artifact or runtime copy is
part of it.
