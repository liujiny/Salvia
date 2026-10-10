# CV1000: a shadow dispatch record -- the pre-entry without the record read

Baseline `55794c23`. `SALVIA_CV1K_SHADOW` defaults to 1, `SALVIA_CV1K_SHADOW_TOUCH`
to 1; 0 disables each.

## Why

`2026-10-10-cv1k-link-gate.md` measured where the pre-entry's 484 cycles go:
not the source comparison (1,494 distinct source lines over 60 frames), not the
fetch map, and not the set array (122 KiB touched) -- the 84-byte `Block`
record at a random slot in an 11 MiB table, which every entry reads and almost
nothing else about the pre-entry misses. It also measured that a one-way copy of
that record, read at the set index of the pc being entered, still describes
95% of entries at 4,096 entries and 97% at 32,768, that no entry's bytes are
immutable (CV1000 games decompress their program into RAM), and that the copy's
dangerous direction -- answering while the bytes changed -- is zero because the
copy carries the snapshot and the dispatcher re-runs the comparison on it.

## The mechanism

- `Shadow shadows[CACHE_SETS]`, one entry per set index, tagged with the pc it
  describes: `{pc, source, entry, words, cycles, check_read_map, sector, epoch,
  next_pc, original[]}`, padded to the console's **128-byte** cache line, so a
  whole copy is one line where the record path costs the record plus the 20-byte
  set it has to be looked up in.
- The dispatcher fills it whenever it resolves a record the slow way, i.e. after
  the record and its snapshot describe the bytes at that pc, and reads it at the
  head of the next entry to the same pc.
- The fast path runs **the same checks the slow path runs** -- tag, fetch page,
  read map, and the snapshot comparison against the source -- on the copy, plus
  one new one: `sector_epoch[sector] == epoch`, where `sector_epoch` is bumped by
  `arena_reuse_sector()`. Handing an arena sector out again is the only way
  generated code is ever freed, so that generation is the one thing a copy
  cannot see for itself.
- There is no store-side signal, no epoch table and no invalidation rule to
  audit: **the copy never claims the bytes are unchanged, it compares them**, and
  its `entry` is a translation of exactly the bytes that comparison proved. The
  validation it skips is the *record read*, not the validation.
- `next_pc` is the successor this pc was last seen to run into, written after the
  call. The fast path reads it (same line, already in hand) and issues one
  `dcbt` for `&shadows[hash(next_pc)]`, so the fetch that would stall the next
  entry is issued a whole block execution -- about 400 cycles -- early. Nothing
  else in the pre-entry is known early enough to prefetch, which is why this miss
  has survived every other attack on the phase. A mispredicted pc (8% of hits)
  fetches a line nobody reads: the prefetch decides nothing.

The fast path shares the call and post-entry logic with the record path verbatim
(one `entry`/`cycles`/`words`/`reason`/`stores` set from whichever answered), so
the budget gate, the MOV.L service dispatch, the partial-exit bail and the timer
accounting are the same code either way.

## Verified

`ddpdfk`, `user-ddpdfk4-core.state`, `dips=00,07,00,00`, `render_cores=2`, host
harness (qemu), shadow on against shadow off:

| | 60 frames | 1800 frames | 600 frames |
| --- | --- | --- | --- |
| `STATE` | `0cf251c3d512ddbb` same | `dff81882efc1b24a` same | `baabc4def027a8a3` same |
| per-frame hash md5 | `c42fdb4870f4f997112d88ff6f74ee12` same | `3f7580a932f3a859ae847bf3f3fe830b` same | `022b01ad0c0b543afd34a685e07173c5` same |
| entries answered by the copy | 5,387,521 (99.3%) | 75,773,754 (99.0%) | 30,792,810 |
| record-path lookups | 40,478 | 755,699 | - |
| `native_calls` / `rebuilds` / `arena peak_words` | identical to shadow off | identical (`3,887,980`) | - |
| host `USER` seconds | - | - | 141.54 vs 146.94 (**-3.7%**) |

The console-side effect is not in these numbers: qemu models no cache, so what
the host shows is the instruction side minus the lookup, plus whatever host cache
pressure the 11 MiB record table was creating. The line the change is for -- the
one the prefetch hides -- is console-only evidence, and the A/B for it is the
`2026-10-10-cv1k-link-gate.md` protocol.

Suites: `sh3_ppc` 846,026 cases PASS with the shadow on (direct and fallback
modes, real generated code, including self-modification and remapping), the
dispatch fixture 8 suites in optimized and ASan/UBSan modes PASS, the remaining
host suites PASS.

## The fixture's contract changed, on purpose

The dispatch fixture compares a preserved pre-change dispatcher against the
production one and requires the traces to be identical, including which entries
ran natively and how many times a record was recompiled. A shadow record
legitimately changes both: it answers entries the record path would have
recompiled (that is its purpose), so fewer records are refreshed and the
replacement cursors end up elsewhere. Two things were wrong with the fixture's
model of a block, not with the mechanism:

1. Its `native()` read the *record's* snapshot as the code to run, so a record
   that a shadow answered without refreshing would have run stale bytes -- which
   no generated block ever does: a block runs the translation of the bytes the
   dispatcher validated, and the fixture now reads those bytes.
2. It compared cache bookkeeping. With the shadow the fixture now requires
   `same_emulation()` -- state, memory, mappings, cycle accounting, IRQ and
   enable state, and the executed-instruction trace with its native/interpreted
   split -- plus `compiled` never *rising* against the reference, and it fails if
   the fast path never answered an entry, so the differential cannot pass
   vacuously. With `-DSALVIA_CV1K_SHADOW=0` the original strict comparison is
   used.

## Limits

- One game, one state, no input. The hit share (99%, 8% of the prefetches
  mispredicted) moves with the code's access pattern.
- The console image is not measured here. Whether the prefetch hides the miss it
  is aimed at is a console question, and so is whether the 4 MiB table's own
  footprint hurts more than the 11 MiB record's did; the host cannot see either.
- The fixture's emulation comparison is weaker than the strict one it replaces;
  the game replay (STATE and the per-frame hash files, on both host and console)
  is what carries the correctness claim for the fast path.

## Reproduce (host only)

```sh
cd /home/humor/salvia-tests/cv1k-ddpdfk-profile-20261007
for v in 1 0; do
  touch /home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh4.cpp  # build.py only sees .cpp mtimes
  SHADOW=$v python3 build.py
  root=/home/humor/salvia-tests/toolchains/ppc/root
  /usr/bin/time -f "SHADOW=$v USER %U" \
   env LD_LIBRARY_PATH=$root/usr/lib/x86_64-linux-gnu CV1K_DIPB=07 CV1K_DIPC=00 CV1K_DIPD=00 \
    $root/usr/bin/qemu-ppc -cpu g4 ./game /home/humor/salvia-tests/cv1000-boot/ddpdfk 1 600 /tmp/sh$v \
    /home/humor/salvia-tests/cv1k-ddpdfk-opt-20261007/user-ddpdfk4-core.state 0 2
done
```

## Revert

Revert this commit: `SALVIA_CV1K_SHADOW`, `SALVIA_CV1K_SHADOW_TOUCH`, `struct
Shadow`, `shadow`, `sector_epoch`, `arena_sector_of` and `sh3_touch_line` in
`sh3_drc_ppc.h`, its allocation/reset/exit lines, the `arena_reuse_sector` bump,
the fast path, the fill, `sh3_shadow_touch`, the `next_pc` write and the
`entry`/`cycles`/`words`/`reason`/`stores` locals in `sh3_drc_dispatch.h`, the
`shadow_hits`/`shadow_pred_miss` counters and their report line, the fixture's
shadow stubs, `same_emulation`, the `native()` model and the delivery recipe, and
this document.
