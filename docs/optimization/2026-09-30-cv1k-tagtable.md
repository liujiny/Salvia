# CV1000 mixed-log review and split tag-table candidate

Build ID: `cv1k-tagtable-20260930-r1`.
Git source baseline: `3ad3fcf43bb08c198f21fa4b220011bf5b9f805e` on `work/reversible-checkpoints` (workflow-only commit over `faf87bcf773a61fad927d7c79346f003a728c74b`). Runtime baseline: HOTMETA built 2026-09-29 22:38:44, XEX SHA256 `1bc6a4e57b807bfc8add93f353c1293510c1a1bb3e9c172bdb322cdcd2e48ef4`.

## The input really was appended

Raw input remains unchanged in the configured runtime `Distro360`. A byte-preserved backup is under toolchain `.work/cpu-tagtable-20260930-202605/baseline/` with SHA256 manifest. `log-analysis.json` and `hotmeta-only-gpu.log` there isolate the new run without deleting the mixed source logs.

- `cv1000-gpu.log` lines 1-39 are the earlier lookup4 run. Lines 40-42 restart self-tests at batches=0; lines 43 and 61 identify HOTMETA. New run is lines 40-78, with the final combat window at 61-78.
- `fbneo-audio.log` lines 1-2 match the old run, lines 3-4 are the appended records. They lack their own build IDs, so association relies on sequence and the supplied test context, not an independently authenticated version field.
- `fbneo-state.log` lines 1-8 are old, lines 9-16 append another load with a reset tick origin. The new load ends `ok=1` for 151788652 bytes, with about 20.4 MiB available; it does not establish a memory-performance bottleneck.
- GPU logging explicitly opens append mode in `epic12_gpu_xbox.h`; runtime counters reset on a new session, not when reading/deleting the text file. Core/front-end phase measurements reset their reporting window after pause. Do not sum cumulative GPU snapshots or mix build versions.

| Final combat window | lookup4 | HOTMETA |
| --- | ---: | ---: |
| Frames / samples | 2541 / 43 | 3412 / 55 |
| cpu_io ms | 17.178 | 15.464 |
| core total ms | 18.105 | 16.388 |
| draw_sync ms | 0.775 | 0.776 |
| frontend sampled_active_loop ms | 19.732 | 18.091 |
| core sampled_peak ms | 46.616 | 22.773 |

These are descriptive samples from different-length/input windows, not causal A/B or measured FPS. HOTMETA is retained because the samples are encouraging, not because a stable percentage has been proven. The source of the new phase values is GPU log lines 61-62 and 77-78. Async presentation stays active without fallback/timeout (line 73); GPU fallback is zero (line 69). Audio still reports underruns and stretching: dropped_samples=0 alone does not mean healthy audio.

## One new performance change

Replace interleaved `{ tag[4], next }` sets with one allocation containing `tag[8192][4]` followed by `next[8192]`. Tags have a 16-byte set stride instead of 20; the hot tag region shrinks from an interleaved 160 KiB span to 128 KiB. The 32 KiB cursor tail is needed only for misses. Allocation remains one 163840-byte object. No allocator-alignment guarantee or real cache-miss reduction is inferred from this layout alone.

The first matching tag still wins, including duplicate/zero/high-bit PCs. Only a miss updates the original unsigned round-robin cursor. Compile-time arena recycling, reset, state loading and free/reallocation retain whole-table clearing/lifetime semantics. Allocation failure handling is unchanged. The PPC instruction emitter, HOTMETA Block layout, four-way probe, full source validation, guest timing, GPU batching, threads and sound are unchanged. No new per-frame/per-block diagnostic work was added.

Production files: `sh3_drc_lookup_table.h`, allocation/reset references in `sh3_drc_ppc.h`, and table field references in `sh3_drc_dispatch.h`. Profile header changes only the build ID. The dispatcher regression includes the production table. This is not a retry of rejected GPU dependency or 256-page batching experiments.

## Executed host checks

WSL Ubuntu-24.04 g++ runs are saved in `tests-before/` and `tests-after/` in the backup directory. Candidate optimized and ASan/UBSan builds passed:

- 2000000 operations against an independent old interleaved table, including 453 full clears, duplicate/zero tags, unsigned wrap and reset during compilation; all tags and cursors agree. Free/reallocate starts cleared.
- 12000 dispatch traces x 12 slices, including interpreter guards, mutable code, aliases, mapping changes, IRQ/delay eligibility, short budgets, eviction and all final tags/cursors.
- 228125 independent lookup probes plus 1000000 stateful replacement/reset probes.
- 345929 exact source checks with guarded-page and halfword boundaries.
- 2323712 production Block snapshot/metadata checks.

Example reproduction from repository root on a Linux host:

```sh
g++ -std=c++11 -O3 libretro/FBNeo/tests/sh3_lookup_table/test.cpp -o /tmp/sh3-lookup-table
/tmp/sh3-lookup-table
g++ -std=c++11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer libretro/FBNeo/tests/sh3_lookup_table/test.cpp -o /tmp/sh3-lookup-table-asan
/tmp/sh3-lookup-table-asan
```

These are host data/control-flow tests, not PowerPC execution or a full DDPSDOJ replay. Full-game and new-console performance remain unmeasured.

## Source mirror, build and rollback

The reviewed Git files were mirrored to the runtime copy using an explicit seven-file list after checking the unchanged runtime baseline. `mirrored-files.json` records exact digests. The tracked build command is `tools/build_fbneo_checkpoint.cmd <existing-toolchain-root>` and uses the configured `.work/Salvia` source. Build output is `.work/build-fbneo-checkpoint.log`; custom post-image steps may copy XEX before final validation.

Revert the performance commit with `git revert <performance-commit>`, mirror the reverted changed paths back to `.work/Salvia`, then rebuild and verify. A Git revert alone does not replace the running XEX. The independent binary backup is kept outside Git. This commit must contain only this experiment, its tests/build recipe and this review. Do not push without a separate upload request.

## Native build and final identity

FBNeo core Rebuild, SDL Build and Salvia frontend Rebuild completed with exit code 0. The project post-image step copied an uncompressed XEX because xextool is absent; this was not treated as a failure or as proof of validation.

The native dispatcher at `0x83261a10..0x83261cc0` uses `slwi r9,r11,4` for tag indexing (0x83261aa4), confirming the 16-byte set stride. Replacement cursor memory is accessed only after all four tag comparisons miss (0x83261b04..0x83261b10). The HOTMETA 84-byte block stride and exact halfword source validator remain present. Both native layout assertions and bounded dumpbin disassembly succeeded. Baseline and candidate disassembly are saved with the raw evidence outside Git.

Tradeoff: the compiler computes a cursor address before the hit/miss branch and saves one additional nonvolatile GPR (`__savegprlr_19` versus the prior r20 boundary). Thus a net instruction-count or FPS reduction is not proven by the narrower tag stride. Hardware A/B is still needed; retain the rollback binary.

- Build time on the Runner: 2026-09-30 20:33:37.
- Output: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.
- Build ID: `cv1k-tagtable-20260930-r1`; verified in the native EXE, along with the unchanged phase-log markers.
- Size: 34611200 bytes; XEX2 header.
- SHA256: `c65f7f6015c395d206ddd20cc7b768febed6fac5731400e1300308d1c7e1b996`.
- Build/distribution XEX hashes match. Reviewed Git/runtime source hashes match.
- Binary rollback: toolchain `.work/fbneo-before-tagtable.xex`, SHA256 `1bc6a4e57b807bfc8add93f353c1293510c1a1bb3e9c172bdb322cdcd2e48ef4`.
- Evidence: `.work/cpu-tagtable-20260930-202605/final-artifact.json`, `source-validation.json`, `dispatcher-baseline.asm`, `dispatcher-candidate.asm`, `tests-after/` and `build.log`.
- No new PPC execution differential test, full-game replay or Xbox FPS measurement was performed. Neither building nor disassembly establishes console improvement.

No runtime logger was modified: performance summaries retain the existing pause callback and low-frequency sampling, and append to the original files. Future appended records are distinguishable by this build ID. Keep the input logs intact for reproducibility.
