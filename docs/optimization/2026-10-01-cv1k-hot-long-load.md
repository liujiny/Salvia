# CV1000: prioritize the measured longword interpreter fallback

Build ID: `cv1k-hot-long-load-20261001-r1`.
Baseline: `20a98253ff3dfb9c8dac2ad470e69f35e7f98aa0` (FALLBACK-DETAIL).
Branch: `work/reversible-checkpoints`. No automatic GitHub push.

## Input and interpretation

The 116-line GPU input identifies FALLBACK-DETAIL throughout. Lines 1-31 are a 74-frame startup window with one timing sample and zero workload samples; do not use its 86.201 ms core time as combat performance. GPU initialization follows at lines 32-34. The last window is lines 35-116: 3642 frames, 53 timing samples and 14 separate workload-count frames. Settings are ddpsdoj, 102400000 Hz, 2 render cores, DIP 00,07,00,00, bpp=4.

The last timing window reports cpu_io=16.126 ms, draw_sync=0.770 ms, core total=17.037 ms and frontend sampled loop=18.764 ms. Async presentation is active without fallback or timeout. This is a baseline observation, not an A/B speedup or a measured FPS calculation. The first startup window must remain separate. All raw inputs and original optimization report are preserved under toolchain `.work/cpu-hot-long-load-20261001-094828/baseline/` with a SHA256 manifest.

The new detail confirms:

- 37432 interpreted instructions and 21707 partial native exits in 14 counted frames.
- 18544 interpreted instructions have high byte 0x60. Opcodes 6022 and 6012 alone account for 9707 + 7704 = 17411 instructions, or 46.51% of all interpreted instructions by count.
- The reported top 16 exact opcodes contain at least 17921 `MOV.L @Rm,R0` instructions, or 96.64% of the 0x60 family. This is a lower bound because only the top 16 of 256 bins are printed.
- The 0x60 access classification contains 9702 watched-longword and 8240 other-handler observations. The watched site at PC 0C1D134C, opcode 6022, was observed in a delay slot reading 0C002310. Its fetched-instruction PC is not necessarily the PC visible to the memory handler.
- The bounded PC-site table leaves 24285 events untracked (64.88%). Its rankings are only among admitted entries, not global hot-site rankings. Exact-opcode and access-class aggregate counters do not have this site-admission loss.

Guest cycles include handler idle/wait charging, IRQ handling and the base step. They are not host time. The trace does not support bypassing handlers, omitting idle charging, eliminating memory guards or interpreting all partial exits as unnecessary RAM fallbacks. The broad handler class also does not identify which precise native guard fired. The audio log still has underruns/time stretching; dropped_samples=0 is not proof of healthy audio. The state log finishes load-done ok=1 for 151788652 bytes, with 43999232 bytes available.

## One performance change

Within the existing 0x60 interpreter shortcut, test low nibble 2 before the remaining multiway switch. Call the unchanged `MOVLL(opcode)` helper and return for `MOV.L @Rm,R0`. Remove only that now-unreachable switch case. Other 0x60 instructions retain the old handlers; other opcode families retain the generic decoder.

This is narrower than the earlier broad 0x60 shortcut and is justified by the new exact-opcode counts. It targets decoder overhead, not the number of guest instructions or native-to-interpreter exits. It does not add a native instruction implementation, duplicate a memory handler or introduce a fixed-address speedhack.

`MOVLL`, `RL`, WaitState, ReadLong, watched-RAM logic, effective-address publication, same-register ordering, delay context and cycle accounting remain unchanged. No PPC emitter, dispatcher, source validator, allocator, GPU, SDL recovery, audio, clock or frame-skip change is made. No new runtime profiling work or log is introduced. The build header only changes the version label.

The additional conditional could cost other 0x60 cases. The compiler might merge it back into a switch, enlarge surrounding code or alter register allocation; native inspection is required before delivery. Even a reduced decode path does not prove overall Xbox performance improvement.

## Executed host checks

Before changing the production decoder, the expanded existing hot-fallback runner passed in optimized and ASan/UBSan configurations. After the change:

- Hot decoder: all 65536 opcode values across eight state variants (524288 cases), plus 6912 directed MOV.L cases covering all 16 Rm registers, R0 self-loads, external aliases, modeled internal/unaligned addresses, handler-visible PCs and signed cycle budgets. The real 16 instruction helper bodies are extracted from production; all other instruction handlers and memory callbacks are modeled. State, callback traces, exact one-read count and EA match the original generic decoder.
- Six existing suites via `tests/sh3_dispatch/run_inline.py` pass in optimized and ASan/UBSan builds, including full source-validation boundary tests, cache lifetime, lookup semantics, fallback attribution and disjoint sampling.
- The extracted real outer loop passes 12000 seeds x 12 slices in both configurations, preserving ordinary/counted results, memory and cycles.

There are 16 passed candidate host test executions: 2 hot-decoder, 12 core/data suites, and 2 outer-loop checks. The hot-decoder suite keeps its documented `-fno-sanitize=shift-base` exclusion for the unchanged legacy EXTSB/EXTSW signed-shift idioms; other selected checks remain active. The independent core suites do not add that exclusion. These are not generated-PPC execution tests, a full game replay, or console FPS measurements.

Reproduction from a Linux checkout:

```sh
python3 libretro/FBNeo/tests/sh3_hot_fallback/run.py --output /tmp/salvia-hot-long-load
python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/salvia-hot-long-load-core
```

The recorded outer-loop runner additionally takes `--baseline-sh4` pointing at this round's preserved baseline sh4.cpp. Actual argv/results are in `tests-before/`, `tests-hot-after/`, `tests-core-after/` and `tests-outer-after/` outside Git.

## Source mirror, build and rollback

Only `sh3_interpreter_hot.h`, the build-ID header and the expanded test runner are mirrored from Git to sibling `.work/Salvia`; hashes are recorded in `mirrored-files.json`. The build uses `.work/cpu-cache-ready-20260930-205053/git-baseline/tools/build_fbneo_checkpoint.cmd` with the existing toolchain root. Full core/frontend Rebuild avoids stale experimental objects; SDL is checked too.

The direct rollback is `.work/fbneo-before-hot-long-load.xex`, verified FALLBACK-DETAIL SHA256 `f87d67213befae2c12de7d833eabd7497d6bb14880215c9e089174e1369fc10b`. To undo source changes, `git revert <this-commit>` in the record checkout, mirror the reverted paths to `.work/Salvia`, rebuild and verify. A Git revert alone does not change an existing XEX. No SDK, keys, ROMs, raw runtime logs or compiler artifacts are committed.

## Native result and artifact identity

FBNeo core Rebuild, SDL Build and Salvia frontend Rebuild completed with exit code 0. Bounded XDK dumpbin inspection confirms that both ordinary fallback call sites take a direct MOVLL branch for low nibble 2, before the residual jump table. The baseline selected route used a table load, mtctr and bctr; the new route does not. A static path walk from low-nibble extraction through the MOVLL call and outgoing branch is 11 instructions before and 5 after, excluding the unchanged helper body. These are static instruction counts, not host cycles or FPS.

The entire MOVLL helper at 0x83257940..0x832579d8 has the same 38 machine words in both linked images, including its mapped/handler dispatch. The ordinary loop at 0x83266388..0x83266a60 grows from 1728 to 1752 bytes, retains r21..r31 saves and the same 176-byte frame. The counted loop range grows from 2712 to 2728 bytes. Other 0x60 cases pay an extra low-nibble test, so hardware A/B remains necessary despite the dominant long-load count. No net console improvement is inferred from these code facts.

- Build time on the Runner: 2026-10-01 09:54:24.
- Output: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.
- Build ID: `cv1k-hot-long-load-20261001-r1`; independently found in the linked EXE.
- Size: 34611200 bytes; XEX2 header; build and distribution copies match.
- SHA256: `b9d8656f94aa1f1749e94ed45cd4b52a0de47dc221980c9e9c592585e06dbe3e`.
- Rollback SHA256: `f87d67213befae2c12de7d833eabd7497d6bb14880215c9e089174e1369fc10b`.
- Evidence: `candidate-artifact.json`, `native-routing.json`, `native-baseline.json`, `native-candidate.json`, both ordinary/counted/MOVLL disassemblies, host result manifests, `log-analysis.json` and `build.log` in this round's external evidence directory.
- No new generated-PPC execution differential test, full game replay or console FPS measurement was performed.

The local commit will contain only this source change, its expanded regression and this review; exact commit/parent are recorded in `final-artifact.json` after committing. The same existing three logs and pause-time reporting remain. Compare the direct FALLBACK-DETAIL baseline and HOT-LONG-LOAD candidate from the same saved state and inputs. Opcode/fallback counts are not expected to disappear, since the memory operations and handler semantics are deliberately unchanged.
