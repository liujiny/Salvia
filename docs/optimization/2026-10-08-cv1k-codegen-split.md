# CV1000: where the generated code actually goes

Baseline: `35787caf` (the emitter block link is reverted; see
`2026-10-08-cv1k-emitter-block-link.md`).

## What was added

`Compiler::emit()` counts every emitted PPC word into a phase, and `compile()`
accumulates the totals; `Sh3WorkReport` prints them as one
`drc_work_codegen` line. The emitter only runs while compiling, so this costs
nothing at frame time and is available on host and console alike. Phases:
`body` (translated SH3 operations), `memaddr` (address computation, alias/map/
range compares), `memguard` (the guard branches themselves), `memaccess` (the
actual load/store and effective-address store), `complete` (flush, charge,
finish/finish_delay for the block's own completion) and `exit` (the guard-exit
bodies emitted by `emit_exits()`).

## Measurement

Host PowerPC harness, `ddpdfk`, 60 frames from `user-ddpdfk4-core.state`,
9,323 compiled blocks, state hash unchanged (`0cf251c3d512ddbb`):

| Phase | words/block | share of all | share of hot path |
| --- | ---: | ---: | ---: |
| body | 22.5 | 12.4% | 18.7% |
| memaddr | 51.9 | 28.7% | 43.1% |
| memguard | 14.6 | 8.1% | 12.1% |
| memaccess | 11.2 | 6.2% | 9.3% |
| complete | 20.2 | 11.2% | 16.8% |
| exit (cold) | 60.7 | 33.6% | - |
| total | 181.1 | 100% | 120.4 |

Two things follow.

- The translated SH3 operations are only 12% of the emitted code. 65% of the
  hot path is memory machinery (address computation, alias/map checks, the
  actual access) and per-block completion.
- A third of everything emitted is cold guard-exit code. It does not run on the
  hot path, but it is interleaved between block bodies, so it costs I-cache
  footprint and, more importantly, arena space: 20 MiB of arena runs at about
  97.5% in long console sessions, and each overflow is a full recompile stall.
  The phase-split result already noted that the user-reported frame rate is
  better explained by repeated stalls than by a constant per-frame cost.

## Why not just shrink the hot path

The workload-matched console A/B of the emitter block link showed that removing
22-25% of the dispatcher's per-block iterations changed nothing measurable, so
the per-block dispatcher cost is largely overlapped by the OoO core. Per-block
instruction count is worth roughly one cycle each (900k extra instructions per
frame cost about 0.3 ms), so cutting the hot path by, say, 20 words/block would
be worth only a few percent. The larger, better-supported lever is the code
size that drives arena recycles.

## Next step

Reduce emitted words per block, starting with the 60.7 cold exit words and the
duplicated 32-bit constant materialisation in `memaddr`, and measure the effect
on the host: `drc_work_codegen`, plus `drc_work_arena peak_words`/`recycles` and
`drc_work_dispatch rebuilds` over a long run, which is the same signal a console
stall shows. One console A/B only once a candidate actually reduces recycles.

## Reproduce (host only)

The host PowerPC harness cross-compiles the real core and runs it under
`qemu-ppc`, so every measurement below needs no console and no XDK:

```sh
# 60-frame ddpdfk run; STATE must stay 0cf251c3d512ddbb, PROFILE carries the counters
cd /home/humor/salvia-tests/cv1k-ddpdfk-profile-20261007
touch /home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh4.cpp   # see note below
python3 build.py
root=/home/humor/salvia-tests/toolchains/ppc/root
LD_LIBRARY_PATH=$root/usr/lib/x86_64-linux-gnu CV1K_DIPB=07 CV1K_DIPC=00 CV1K_DIPD=00 \
  $root/usr/bin/qemu-ppc -cpu g4 ./game /home/humor/salvia-tests/cv1000-boot/ddpdfk 1 60 \
  /tmp/out /home/humor/salvia-tests/cv1k-ddpdfk-opt-20261007/user-ddpdfk4-core.state 0 2

# emitter instruction-level differential suite
cd /home/humor/src/Salvia
python3 libretro/FBNeo/tests/sh3_ppc/run.py \
  --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root --output /tmp/sh3ppc
```

Note: `build.py` decides to rebuild from the `.cpp` timestamp only, so a
header-only edit silently reuses a stale object and reports the old numbers.
Always `touch` the changed translation unit (or its `sh4.cpp`), or the
measurement is invalid — that trap already produced one wrong result once.

Console images are built with `salvia-tests/xex-build/build-xex.sh --tag <tag>`
(diagnostics flavor only while iterating; add `--flavor release` only on an
accepted change, see `AGENTS.md`).
