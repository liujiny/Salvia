# CV1000: line-granular write stamps -- a precise signal, and a skip that does not pay

Baseline for this round: `8c38d0bd`. This lands the mechanism and its
cross-check, both off by default; no build changes behaviour unless it asks for
probe level 1 or 2. The skip that the mechanism exists for is measured here and
**not** enabled.

## Why this exists

`2026-10-09-cv1k-dispatch-split-console.md` put 7.09 ms of a 14.587 ms dispatch
phase in the *pre-entry* span (484 cycles per entry) with only 18 bytes of
source comparison per entry, and concluded the cost is cache lines rather than
instructions. Two routes out of that were tried and rejected:

- the emitter block link (`2026-10-08-cv1k-emitter-block-link.md`), killed by a
  workload-matched console A/B;
- the per-64-KiB-page code generation
  (`2026-10-09-cv1k-code-page-generation.md`), whose invalidation signal was
  *complete* (`epoch_missed = 0` over 80.9M validations) but useless, because
  62.6% of validations found their page written since the block was compiled.

This round asks the other half of that question: at **line** granularity, and
measured against the *last validation of the same block* rather than since
compilation, how often would a store-side signal actually be clean -- and what
does maintaining it cost?

## Mechanism

- One byte per 64-byte line of the registered RAM window, 8 MiB, indexed by the
  line's byte offset inside that window. The offset is what the generated store
  already computes for its own host address, so CV1000's external aliases and
  the 8 MiB backing folded into a 16 MiB span all resolve to the one line they
  share; there is no guest address in the index and therefore no alias hole.
- A generated store marks its line inline: five words (`sub` for the window
  offset, one masked `rlwinm` for the line index, `lwz` of the table base from
  the state, `li r11,1`, `stbx`). Emitted only when the driver's RAM window is
  registered, which is also the condition under which the dispatcher will trust
  a clean stamp.
- `WB`/`WW`/`WL` call `stamp_mark()`: that is the interpreter, and also the DMAC
  and the cheat path, the hook set the epoch round proved complete.
- The dispatcher reads the stamps of the two lines the block's bytes occupy (33
  halfwords can never span more than two). Level 1 counts what a clean pair
  would have removed and *still compares*; level 2 lets a clean pair skip the
  comparison, and everything else -- tag, fetch page, source pointer, read-map
  test -- is unchanged. A validation that passes clears the pair.
- `sh3_drc_reset()` clears the table: every record is gone when it runs, so
  nothing can be skipped on a stale stamp.

## The gate, measured

`ddpdfk` from `user-ddpdfk4-core.state`, `dips=00,07,00,00`, `render_cores=2`,
no input, host harness. Level 0 is the unmodified build, so the three levels
differ only by the mechanism.

| 60 frames | level 0 | level 1 | level 2 |
| --- | ---: | ---: | ---: |
| `STATE` | `0cf251c3d512ddbb` | same | same |
| per-frame hash file (md5) | `c42fdb4870f4f997112d88ff6f74ee12` | same | same |
| validations audited | - | 5,418,967 | 5,418,967 |
| `clean` (skip would have fired) | - | 5,418,966 | 5,418,966 |
| `dirty` | - | 1 | 1 |
| **`missed`** (clean but changed) | - | **0** | **0** |
| `memaccess` words | 145,883 | 223,953 | 223,953 |
| arena `peak_words` | 1,286,716 | 1,368,928 | 1,368,928 |

| 1800 frames | level 0 | level 1 | level 2 |
| --- | ---: | ---: | ---: |
| `STATE` | `dff81882efc1b24a` | same | same |
| per-frame hash file (md5) | `3f7580a932f3a859ae847bf3f3fe830b` | same | same |
| validations audited | - | 76,499,705 | 76,499,705 |
| `clean` | - | 76,497,992 (99.9978%) | 76,497,992 |
| `dirty` | - | 1,713 | 1,713 |
| **`missed`** | - | **0** | **0** |
| `memaccess` words (blocks) | 463,468 (29,739) | 709,518 | 709,518 |
| arena `peak_words` | 3,887,980 (46.3%) | 4,138,384 (49.3%) | 4,138,384 |
| `rebuild_causes` | conflict 29,748, **source 0**, map 0 | same | same |
| host `USER` seconds | 325.80 | 330.95 | 331.23 |

Two readings.

1. **The signal is precise.** 99.9978% of validations are on a block whose two
   lines have not been written since that block was last validated, and the
   dangerous direction -- stamps clean while the comparison says the bytes
   changed -- is 0 over 76.5M validations, at line granularity, where the epoch
   round only managed 37% clean at page granularity. The store set is simply
   tiny: a temporary scan of the table after a 60-frame run found 7,849 of the
   window's 262,144 lines written at all (3.0%), and `rebuild_causes source=0`
   over 1800 frames says the guest never even re-uploads code after boot.
2. **The skip does not pay for it.** The marks cost +1.6% of host `USER` time
   (level 1 against level 0) and letting the hot path act on them recovers
   nothing (+1.7%). The mark is one extra cache-line touch per *executed* guest
   store (~131,600 per frame, and the written lines are thousands of lines
   spread over the RAM, so it misses L1); what it removes is ~10 words of
   already-warm snapshot and source bytes per entry (~42,500 entries per frame).
   The two are the same order of magnitude and the marking side is the larger.

So level 2 stays off. The mechanism stays in the tree at level 0 because its
counters (level 1) are the gate any future store-side invalidation has to pass,
and because what the stamps *can* pay for is not a comparison but a whole
pre-entry span: a link that enters an already validated successor would remove
the 484-cycle pre-entry cost, which is the same order as the ~67-215 cycles of
measured per-entry cost, against the ~0.2 ms/frame the marks add. That work is
not in this commit and is not sized yet; it needs its own design, because links
have hazards the stamps do not cover (arena sector reuse and slot overwrite
invalidate code a link would have jumped into, so a link has to be scoped to
blocks that die together, or verify the successor's record before jumping).

## Verification

`STATE` and the per-frame hash file are identical at all three levels for 60 and
1800 frames, so levels 1 and 2 change nothing observable about the emulation.
The PPC differential suite, the dispatch, hot-fallback and layout suites pass
with the default level 0. The default costs nothing at all: at level 0 the
whole mechanism -- table, hook, mark and dispatcher logic -- is compiled out, so
release images are unchanged in code and in the codegen phases.

## Limits

- The host harness runs the emulated PPC through qemu, whose own overhead per
  emulated instruction dominates wall time, so the `USER` deltas above measure
  the mechanism's *cache* cost well and its *instruction* savings poorly. The
  conclusion drawn from them is "no host evidence of a win, and the instruction
  count is a wash at best", not "the console would lose by exactly this much".
  On that evidence a console A/B of level 2 is not worth a run.
- 76.5M audited validations cover one game, one state, no input, 1800 frames.
  The hook set is the epoch round's, which was also exercised on the console,
  but any *use* of the stamps (level 2, or a future link) needs its own audit on
  the workload it is meant for.
- The `stamp_mark` index folds any address outside the window into the table, so
  a write to an unrelated page can only make the dispatcher compare more often.
  That direction is deliberate: marking must never be missed, over-marking is
  free to be wrong.

## Reproduce (host only)

The harness build flag is the level (`STAMP_PROBE=0|1|2`); after a header edit
the harness still needs its `.cpp` touched, or `build.py` reuses the old object.

```sh
cd /home/humor/salvia-tests/cv1k-ddpdfk-profile-20261007
for v in 0 1 2; do
  touch /home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh4.cpp
  STAMP_PROBE=$v python3 build.py
  root=/home/humor/salvia-tests/toolchains/ppc/root
  LD_LIBRARY_PATH=$root/usr/lib/x86_64-linux-gnu CV1K_DIPB=07 CV1K_DIPC=00 CV1K_DIPD=00 \
    $root/usr/bin/qemu-ppc -cpu g4 ./game /home/humor/salvia-tests/cv1000-boot/ddpdfk 1 1800 \
    /tmp/L$v /home/humor/salvia-tests/cv1k-ddpdfk-opt-20261007/user-ddpdfk4-core.state 0 2
done
```

## Revert

Revert this commit: the `code_stamp` table, `STAMP_*`, `stamp_mark`,
`Compiler::mark_store` and its call site, `Sh3PpcState::code_stamp`, the
`sh3_drc_stamp_mark` wrapper and its `WB`/`WW`/`WL` calls, `sh3_stamp_*` and the
dispatcher's level 1/2 blocks, the `stamp_*` work counters and their report
line, the `SALVIA_CV1K_STAMP_PROBE` knob in `salvia_fbneo_diagnostics.h`, the
`STAMP_PROBE` flag in the harness `build.py`, and this document.
