# CV1000: prefetching the generated code from the shadow's prediction

Baseline `bfb901ec`. `SALVIA_CV1K_SHADOW_TOUCH` covers both fetches; 0 disables
them and leaves everything else in place.

## Why

The shadow dispatch record's console A/B
(`2026-10-10-cv1k-shadow-record-console.md`) left an attribution that only adds
up after the probe's own tick cost is taken out: the probe takes three ticks per
sampled entry, and the span sums overshoot `drc_dispatch` by exactly
`3 x samples x 38 ns`. Correcting for that, per frame:

| | base | shadow | |
| --- | ---: | ---: | --- |
| pre-entry | 7.21 ms | **4.22 ms** | -41% |
| generated block call | 6.15 ms | **7.43 ms** | **+21%** |
| post-entry | 0.81 ms | 0.62 ms | |
| sum | 14.17 | 12.27 | vs `drc_dispatch` 14.21 / 12.21 |

So the generated code is now 61% of the dispatch phase. 7.43 ms over 44,186
entries is 538 cycles per entry, and a block's code is about 117 words -- three
128-byte lines. That is not instruction cost; it is a *streaming* miss: a
block's code is fetched roughly once per frame, so its reuse distance is a whole
frame and nothing caches it. A block's body and its cold exits are laid out
contiguously, so executing it walks line after line straight into that latency.

The shadow already proved the fix works for its own table: a `dcbt` issued one
block execution -- about 500 cycles -- early, from a prediction that is right
92% of the time, took 3.05 ms out of the pre-entry span. The same lever applies
to the code, with a better prediction: a block's code address changes only when
it is recompiled.

## The mechanism

- Each shadow entry gains `next_entry`: the code address the successor of that
  pc ran, last time this pc ran into it. It is written by the dispatcher after
  the call, into the *predecessor's* entry -- the same shape as the existing
  `next_pc`, and a store to a line the dispatcher read last iteration, which an
  in-order core retires without stalling on.
- The fast path reads it out of the copy it already has (same line) and issues
  two `dcbt`s, one for the block's first line and one for the second. Nothing
  checks the address and nothing depends on it: it is a hint, and a stale one
  fetches a line nobody reads.
- The prefetch is aimed at *code that is about to execute*, so it adds no
  working set of its own -- it moves a fetch that was going to happen anyway to
  a point where its latency overlaps a block execution instead of stalling the
  next entry.

## Verified (host harness, `ddpdfk`, one state, no input)

| | 60 frames | 600 frames | 1800 frames |
| --- | --- | --- | --- |
| `STATE` | `0cf251c3d512ddbb` same | `baabc4def027a8a3` same | `dff81882efc1b24a` same |
| per-frame hash md5 | `c42fdb4870f4f997112d88ff6f74ee12` same | `022b01ad0c0b543afd34a685e07173c5` same | `3f7580a932f3a859ae847bf3f3fe830b` same |
| entries answered by the copy | 99.3% | 99.3% | 99.0% |
| **code prediction changed** | **0** | **0** | **0** of 75.8M |
| arena `peak_words` | same | - | 3,887,980 same |

`shadow_code_changed` is the counter that makes the prefetch worth aiming at all:
over 1800 frames the code address predicted for a successor *never* differed
from the one the next entry resolved, against 8% for the pc prediction. All of
that is a line nobody reads, but almost none of it is.

Host `USER` seconds over 600 frames: 146.94 with no shadow, 141.59 with the
shadow and no fetch, 139.55 with both. The host models no cache, so that
difference is instruction count only; the fetch's own effect is console-only
evidence.

Suites: `sh3_ppc` 846,026 cases PASS, the dispatch fixture 10 PASS lines with
`SALVIA_CV1K_SHADOW_TOUCH` at 1 and at 0 (its differential covers the store that
feeds the prefetch), the remaining host suites PASS. The fixture's host branch
of both fetch helpers consumes the address so the load that produces it is not
optimized away; the earlier abort-based host check was removed after it showed
up as a 2.9% host `USER` artifact.

## Limits

- The fetch is a hint. If the console shows no gain, the 538 cycles per entry
  are not dominated by line latency and the next lever is the emitter's hot
  words instead (`drc_work_codegen` is untouched here: 167.5 words per block,
  `memaddr` 39.8 of them, the address-and-guard sequence every access executes).
- One workload, one state. The prediction is perfect *because* recompiles are
  rare in it (`rebuild_causes source=0`); a workload that rewrites code would
  move code addresses and turn some fetches into waste, which is still only
  waste.
- Two `dcbt`s cover two of a block's ~2.7 lines. The rest is left to the
  hardware's sequential fetch, which is a deliberate half-step: the A/B is meant
  to show whether line latency is the cost at all before more traffic is spent.

## Reproduce (host only)

```sh
cd /home/humor/salvia-tests/cv1k-ddpdfk-profile-20261007
for v in 1 0; do
  touch /home/humor/src/Salvia/libretro/FBNeo/src/cpu/sh4/sh4.cpp  # build.py only sees .cpp mtimes
  SHADOW=1 SHADOW_TOUCH=$v python3 build.py
  root=/home/humor/salvia-tests/toolchains/ppc/root
  /usr/bin/time -f "touch=$v USER %U" \
   env LD_LIBRARY_PATH=$root/usr/lib/x86_64-linux-gnu CV1K_DIPB=07 CV1K_DIPC=00 CV1K_DIPD=00 \
    $root/usr/bin/qemu-ppc -cpu g4 ./game /home/humor/salvia-tests/cv1000-boot/ddpdfk 1 600 /tmp/ct$v \
    /home/humor/salvia-tests/cv1k-ddpdfk-opt-20261007/user-ddpdfk4-core.state 0 2
done
```

## Revert

Revert this commit: `Shadow::next_entry` and its comment, `sh3_code_touch`, the
`sh_prev_index`/`sh_have_prev` state, the successor-code store, the `changed`
counter and its report line, the fixture's field, and this document.
