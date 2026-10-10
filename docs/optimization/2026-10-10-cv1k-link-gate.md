# CV1000: what the pre-entry actually costs, and the record that removes it

Baseline `40042b2d`. Diagnostics only (`SALVIA_CV1K_LINK_PROBE`, default 0);
the emulation is untouched at every level.

## Why

`2026-10-09-cv1k-dispatch-split-console.md` left the dispatch phase at 14.587 ms
of a 16.3 ms core frame, 49% of it the pre-entry span, and proposed two routes
out: a store-side signal that lets the dispatcher skip the source comparison,
and a compact hot dispatch record. `2026-10-10-cv1k-link-sizing.md` then built
the first link design on the assumption that the lines that miss per entry are
the `Block` record *and* the source bytes. This round measures where those
cycles actually go before anything is built on them.

## Measurement (host harness, `ddpdfk`, one state, dips 00,07,00,00, cores 2)

Entries are the sampled dispatch specialization; `steps` are the entries that
follow another entry inside the same chain call, which is the population any
successor scheme can serve.

| 60 frames | |
| --- | ---: |
| entries / `steps` | 5,427,999 / 5,318,473 |
| entries whose bytes are outside every writable mapping | **0** |
| prediction lands on the successor pc | 4,883,446 (91.8% of steps) |
| ... and the fetch page and arena residency still hold | 4,883,446 |
| shadow record answers (1024 / 4096 / 32,768 entries) | 91.1% / 95.4% / **97.4%** |
| shadow record answers while the bytes changed | **0** |
| distinct 64-byte source lines ever validated | **1,494 (95,616 bytes)** |
| distinct record lines touched, today's 84-byte record | 9,032 (578,048 bytes) |
| distinct record lines, 16-byte hot/snapshot split | 7,931 (507,584 bytes) |

| 1800 frames | |
| --- | ---: |
| entries / `steps` | 76,529,453 / 74,366,895 |
| prediction lands / all checks hold | 68,302,009 (91.8%) / 67,923,194 (91.4%) |
| prediction held while the record no longer described its bytes | **3** |
| distinct source lines | 3,019 (193,216 bytes) |
| distinct record lines, 84-byte / hot split | 29,662 (1.90 MiB) / 20,035 (1.28 MiB) |
| `drc_work_rebuild_causes` | conflict 29,748, source 0, read_map 0 |

`STATE 0cf251c3d512ddbb` (60 frames) and `dff81882efc1b24a` (1800) and the
per-frame hash files are identical with the probe at level 2 and at level 0.

## What it settles

1. **The "immutable source" shortcut is dead.** Every entry's bytes live in the
   writable RAM window: CV1000 games decompress their program into RAM and run
   from there (`cv1k_fastboot.h`: "The program is decompressed into RAM by the
   game"), so no entry can be linkable for free. Anything that skips a check has
   to answer a store-side question, and that question costs work on every
   *executed* store -- which is exactly the trade the write stamps already lost
   (`2026-10-09-cv1k-write-stamps.md`: +1.6% to mark, +1.7% back).
2. **The source bytes are not a miss.** 1,494 distinct 64-byte lines over 60
   frames, 3,019 over 1800: the whole source footprint is under 200 KiB, so the
   comparison reads a resident line. That is why acting on the stamps recovered
   "nothing": there was nothing to recover. The 484-cycle pre-entry is not the
   comparison and it is not the source bytes.
3. **The record is the miss.** The 84-byte `Block` is at a random slot in an
   11 MiB table, and at 1800 frames the workload touches 29,662 of its lines
   (1.90 MiB) against a 1 MiB L2 that the generated code and the blitter are
   also streaming through. Every entry reads it; almost nothing else about the
   pre-entry misses. A hot/snapshot split alone would shrink that footprint by a
   third and would still read the same two lines per entry, so the win has to
   come from not reading the record at all.
4. **A one-way shadow of the record holds 97% of entries**, and 95% at 4,096
   entries (512 KiB) or 91% at 1,024 (128 KiB) -- the predecessor population is
   skewed enough that a table a fraction of the record's size answers nearly
   every entry. And unlike a link, a shadow carries the snapshot with it, so it
   answers with the *existing* checks rather than a new validity rule.
5. **A link is still possible but needs the store-side signal.** The 3 events in
   1800 frames where the prediction and the arena checks all held while the
   record no longer described its bytes are exactly the cases the write stamps
   would catch and no cheaper check would. The shadow needs no such thing: it
   re-runs the byte comparison, and the counter that would prove it unsafe is 0
   at both lengths.

## The design that follows

A **shadow record**: one copy of a block's dispatch record per set index,
`{pc, source, entry, words, cycles, check_read_map, arena sector, sector epoch,
original[]}`, written by the dispatcher whenever it resolves a record the slow
way and read at the head of the next entry. The checks the fast path performs
are the ones the slow path performs -- tag, fetch page, read map, snapshot
comparison -- just against the shadow, plus one new one: the arena sector the
entry names must not have been handed out again (a 32-entry epoch array bumped
by `arena_reuse_sector`). That is what makes it sound without a store-side
signal: the shadow never claims the bytes are unchanged, it compares them.

Why it should pay, where the link would not: it removes the record read (1.90 MiB
of randomly addressed lines) and the set lookup, and it replaces them with a
read of a 128 KiB-512 KiB table whose entry address is a *constant per block* --
the successor's pc hash is known at compile time, so the block itself can issue
the `dcbt` for its own shadow in its first instructions, about 400 cycles before
the dispatcher would stall on it. Nothing else in the pre-entry is known early
enough to prefetch, which is why the record's miss has survived every other
attack on this phase.

## Staging

- Stage A (this commit): the probe, its counters and this document.
- Stage B: the shadow array, the fast path and the sector epochs. Host-verified
  by `STATE` and the per-frame hash file at 60 and 1800 frames plus the four
  suites; the shadow-hit counter is the acceptance gate.
- Stage C: the compile-time `dcbt`, which needs the emitter's successor to be
  known before the block's prologue is emitted. Console-only evidence: qemu
  models no cache, so no host run can size it.

## Limits

- One game, one state, no input, 1800 frames. The shadow's hit share moves with
  the code's access pattern, and this workload's is a deterministic loop; a
  workload with more distinct blocks per frame would hit less.
- The 3/76.5M `stale` events are a proxy, not the link's actual validity test:
  they say the prediction and the arena checks held while the record's bytes had
  changed since it was compiled, which over-counts a link that would record its
  byte epochs at fill time. They are the honest size of what a link would need
  the store-side signal for, which is why the shadow is the design of record.
- The footprint counters key the source lines by their offset inside the
  registered window, so code fetched through an alias would be counted once.

## Reproduce (host only)

```sh
cd /home/humor/salvia-tests/cv1k-ddpdfk-profile-20261007
for m in 1023 4095 32767; do
  touch /home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh4.cpp  # build.py only sees .cpp mtimes
  LINK_PROBE=2 LINK_MASK=$m python3 build.py
  root=/home/humor/salvia-tests/toolchains/ppc/root
  LD_LIBRARY_PATH=$root/usr/lib/x86_64-linux-gnu CV1K_DIPB=07 CV1K_DIPC=00 CV1K_DIPD=00 \
    $root/usr/bin/qemu-ppc -cpu g4 ./game /home/humor/salvia-tests/cv1000-boot/ddpdfk 1 60 /tmp/lp$m \
    /home/humor/salvia-tests/cv1k-ddpdfk-opt-20261007/user-ddpdfk4-core.state 0 2
done
```

## Revert

Revert this commit: the level-2 blocks and `sh3_src_immutable`/`sh3_link_alive`
in `sh3_drc_dispatch.h`, `LinkProbe`, `lp_rec`/`lp_set`/`lp_shadow`,
`LP_SET_MASK` and the three footprint bitmaps in `sh3_drc_ppc.h`, the counters
and their report lines in `sh3_drc_work_profile.h` and `sh4.cpp`, the probe
levels in `salvia_fbneo_diagnostics.h`, and this document.
