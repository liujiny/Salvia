# CV1000: reject split tag table; warm-cache dispatcher guard

Candidate build ID: `cv1k-cache-ready-20260930-r1`.
Branch: `work/reversible-checkpoints`. No automatic GitHub push.

## Independent rollback first

The incoming Git HEAD was `c2ee0a5cb42294ed3e18ec4a1464c920848d3ac8` (TAGTABLE).
A real `git revert --no-edit c2ee0a5cb42294ed3e18ec4a1464c920848d3ac8` produced
`ba307fb0efaa4525773731cab386b646ff217595`. This is the source baseline for the new candidate.
The reverted production source was compared with `3ad3fcf43bb08c198f21fa4b220011bf5b9f805e`
and is identical. The split-table experiment remains in Git history, not in the active build.

Raw evidence is outside Git at toolchain `.work/cpu-cache-ready-20260930-205053/`.
It preserves incoming logs, the TAGTABLE XEX/EXE/MAP, affected source and SHA256 manifests.
The original three logs and `OPTIMIZATION_REVIEW_2026-09-29.md` are not edited.

## Incoming hardware observations

The incoming GPU file has 39 lines, all from TAGTABLE, rather than the prior mixed 78-line log.
Its last pause window (lines 22-39) has 2964 frames and 51 valid samples.
Settings remain ddpsdoj, 102400000 Hz, 2 render cores, DIP 00,07,00,00, bpp=4.
The HOTMETA comparison is preserved in the preceding experiment's input log at
`.work/cpu-tagtable-20260930-202605/baseline/Distro360/cv1000-gpu.log`, lines 61-78.

| Sampled combat phase | HOTMETA | TAGTABLE |
| --- | ---: | ---: |
| Frames / samples | 3412 / 55 | 2964 / 51 |
| cpu_io ms | 15.464 | 17.389 |
| Core total ms | 16.388 | 18.314 |
| draw_sync ms | 0.776 | 0.778 |
| Frontend sampled_active_loop ms | 18.091 | 20.199 |
| Core sampled_peak ms | 22.773 | 44.612 |

The earlier loading/setup windows also do not demonstrate a benefit (core 18.569 vs 18.661 ms).
These are different inputs/durations/random samples, not a causal fixed-replay benchmark.
The conservative decision is to reject the added layout complexity, not to claim a measured
TAGTABLE slowdown percentage. Restore the encouraging HOTMETA baseline before another experiment.
Async display is active with no fallback/timeout; GPU fallback is zero. GPU statistics are
cumulative and cannot be subtracted directly from per-pause core times. Audio still has
underruns/time stretching. The state log reports load-done ok=1 for 151788652 bytes and
21327872 bytes available; no new large allocation is introduced.

## One subsequent candidate: skip a redundant warm allocation-check call

`Sh3Ppc::allocate()` first rejects `failed`, then immediately returns true if `blocks` exists.
The previous dispatcher called this out-of-line helper at every C++ dispatcher entry,
including warm re-entry after interpreter fallback. It was not allocating fresh memory
at every entry; only the helper call/check is redundant.

Replace the entry guard with:

```cpp
if (m_sh4_icount <= 0 || failed || (!blocks && !allocate())) return false;
```

Failure precedence and non-positive budgets are preserved. Cold initialization still invokes
the original allocator and its cleanup/failure handling. Reset keeps the allocated cache;
exit clears the pointer/failure flag and therefore permits normal reallocation. No new
pointer lifetime, executable-memory policy or allocation size is introduced.
The four-way probe, complete halfword snapshot check, HOTMETA Block, PPC emitter,
interpreter, guest cycles, devices, GPU, sound, presentation and pause sampling remain unchanged.
This is an entry-level micro-optimization, not native block linking and not removal of code validation.
Its net Xbox benefit must be measured; shorter source code and host counts do not establish FPS.

## Host checks actually executed

The dispatcher test now models an absent cache pointer and sticky allocation failure instead
of a permanently present array. It still compares against the independent original entry algorithm.
Before changing production code, the lifecycle test observed 64 helper calls for 64 warm entries.
After the candidate, the same 64 warm entries made zero helper calls; cold allocation still occurs.
Both optimized and ASan/UBSan builds passed:

- Zero budget, initial/sticky failure, warm reset, failed+non-null precedence and reallocation.
- 12000 random traces x 12 time slices, with identical states, mappings, memory, callback order,
  compile counts, guest cycles, cache tags and replacement cursors.
- 228125 independent four-way queries and 1000000 stateful replacement/reset probes.
- 345929 exact source-snapshot checks, including guarded-page boundaries and alignment.
- 2323712 production Block metadata/snapshot checks. The compile-only 32-bit layout contract passed.

The synthetic 4096 single-block workload reduced allocation-helper invocations from 4096 to 1.
Do not confuse that workload count with the real game's dispatcher frequency or an FPS ratio.
Tests and logs are in the evidence directory's `tests-before/` and `tests-after/`.
Example reproduction from a Linux checkout:

```sh
g++ -std=c++11 -O3 -DSH3_CACHE_READY_EXPECT_FAST libretro/FBNeo/tests/sh3_dispatch/test.cpp -o /tmp/sh3-dispatch-ready
/tmp/sh3-dispatch-ready
```

No new full-game replay, actual PPC execution differential test or Xbox performance run is claimed.

## Build, source mirroring and binary rollback

Reviewed reverted/candidate source was mirrored explicitly from Git to sibling `.work/Salvia`.
Removed split-table headers/tests were backed up, then removed with fenced structured edits.
The native rebuild uses the byte-preserved checkpoint script from
`.work/cpu-cache-ready-20260930-205053/git-baseline/tools/build_fbneo_checkpoint.cmd`, with the
existing toolchain root as its argument. That script rebuilds FBNeo, checks SDL and rebuilds Salvia.
The script was preserved because reverting TAGTABLE also removes its originally bundled build helper.

Candidate-only binary rollback is `.work/fbneo-before-cache-ready.xex`, verified HOTMETA:
`1bc6a4e57b807bfc8add93f353c1293510c1a1bb3e9c172bdb322cdcd2e48ef4`.
The rejected TAGTABLE binary remains in this round's raw baseline backup.
Revert the subsequent candidate commit to recover HOTMETA source, mirror the reverted paths,
and rebuild; a Git revert alone does not change `.work/Salvia` or the existing XEX.

## Native result

FBNeo core Rebuild, SDL Build and Salvia frontend Rebuild completed with exit code 0.
The native dispatcher is `0x83261a08..0x83261cc8`. At `0x83261a48`, the non-null
cache branch jumps to `0x83261a5c`, bypassing the allocator call at `0x83261a4c`.
The failure flag is checked first at `0x83261a2c..0x83261a34`; cold initialization
and its failure result are retained. The HOTMETA baseline called the same allocator
at `0x83261a28` without a warm bypass. Disassembly and both range descriptions are
saved as `dispatcher-hotmeta.asm` and `dispatcher-candidate.asm` outside Git.

The compiler saves r21..r31 with a 176-byte frame instead of the HOTMETA r20..r31
and 192-byte frame, while the function's code range is slightly larger. These are
machine-code observations, not a measured speedup. Do not replace hardware testing
with an instruction-count claim.

- Build ID verified in EXE: `cv1k-cache-ready-20260930-r1`.
- Build time on the Runner: 2026-09-30 20:59:44.
- Output: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.
- Size: 34611200 bytes; XEX2 header; build and distribution hashes match.
- SHA256: `e233b3ba3f05b42ce57058f9e973349f2286056921981bda7d31db1ae2036812`.
- New production change relative to the independent revert baseline is only the
  dispatcher entry guard; the profile header only changes the build ID.
- Input logs and the original optimization report are preserved.
- No new runtime logs, per-instruction timers, real-game replay or console FPS result.

For the next test, compare this candidate with HOTMETA under the same input and
configuration, not only with rejected TAGTABLE. Otherwise the rollback benefit
could be incorrectly attributed to the new guard. Both rollback and candidate
will have separate Git commits; their exact IDs and artifact digest are in the
external final-artifact record.
