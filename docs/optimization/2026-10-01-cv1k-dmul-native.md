# CV1000 native DMULS/DMULU — 2026-10-01

## Baseline and reason

- Baseline: `7a2f28ad17f67a7cfaf8087d58c826cd105d5839`.
- Baseline XEX SHA256: `f98ea82792eab9e89f608c3a11902fab97712291c512b3a0e7fa3dbcca187ce4`.
- Build: `cv1k-dmul-native-20261001-r1`.
- The uploaded DDPDFK log identified CPU/device work averaging 16.992 ms in mixed gameplay, with normal GPU/VMX paths. The complete external 600-frame fallback audit found 73,731 DMULS and 5,552 DMULU no-entry fallbacks under the original input sequence. These are instruction counts, not host-time samples, and do not establish the exact contact-effect root cause.

## Implementation

The SH3 PPC emitter now translates signed and unsigned 32-bit long multiply using `mulhw`/`mulhwu` for MACH and `mullw` for MACL. Both operands stay locked while accumulator slots are allocated. The operand registers and SR are preserved; both accumulator halves remain available to subsequent instructions in the same block. One extra cycle is charged in addition to the normal base cycle, matching the interpreter's total of two. The same path works in eligible branch delay slots.

The word multiply operations select the low 32 operand bits. The implementation does not use Xenon's 64-bit XER carry flag or assume sign extension of cached registers. Existing source validation, code-write guards, memory handlers, wait states, IRQ and delay-slot eligibility remain intact. Only the SH3 PPC core changes; other emulated CPU implementations are unaffected.

## Tests executed

```text
rtk proxy python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root --output /mnt/e/Baiduyundownload/salvia-toolchain/.work/cpu-dmul-native-20261001/ppc-tests
rtk proxy python3 /home/humor/salvia-tests/cv1000-dmul-native/game/build.py
rtk proxy python3 /home/humor/salvia-tests/cv1000-dmul-native/game/run.py
```

- Real emitted 32-bit big-endian PPC instructions passed **741,061 differential cases** under QEMU, including **44,768 explicit multiply cases**. Every register pair and signed/unsigned boundary combination was checked against both the interpreter and an independent 64-bit mathematical product, including INT32_MIN, negative products, equal operands, zero, accumulator dependencies, dirty accumulators, short budgets and delay slots.
- DDPDFK and DDPSDOJ each ran 600 frames from the established checkpoints, with coin/start, firing and movement, and a save/load at offset 240. Four runs exited zero. Each game's nine frame/audio hash, raw-frame and state artifacts were byte-identical. Per-frame guest cycles, GPU work, cache activity and device service counts were also identical.
- Baseline and candidate used separate SH3 objects with identical private Count=true wrappers; no all-frame counting was added to production. Current AltiVec alpha rendering and the software shader model were shared by both variants.

| Work count / 600 frames | DDPDFK baseline | DDPDFK candidate | DDPSDOJ baseline | DDPSDOJ candidate |
| --- | ---: | ---: | ---: | ---: |
| Interpreter steps | 723,254 | 644,449 | 1,012,237 | 909,976 |
| No-entry exits | 286,044 | 206,761 | 421,840 | 318,812 |
| Native calls | 19,597,422 | 19,520,404 | 28,793,731 | 28,700,737 |
| Lookups | 20,029,518 | 19,874,009 | 29,469,421 | 29,274,166 |
| Budget exits | 146,052 | 146,844 | 253,850 | 254,617 |
| Partial exits | 235,516 | 235,516 | 265,265 | 265,265 |

Interpreter work fell **10.90% / 10.10%**. Longer compilable blocks slightly increase short-budget fallbacks; exact cycle gates remain enforced. These changes in counts are not measured CPU time or FPS improvements. The replay does not reproduce the user's exact impact sprite, physical Xenos or Xbox frontend.

## Xbox build

```text
rtk proxy /mnt/c/Windows/System32/cmd.exe /c "E:\Baiduyundownload\salvia-toolchain\.work\cpu-dmul-native-20261001\build_fbneo_incremental.cmd E:\Baiduyundownload\salvia-toolchain"
```

An external copy of the existing checkpoint script used `/t:Build` for FBNeo and its linked frontend, with the existing SDL build. The actual log records success for all three required stages; exit code **0**. No other emulator core was built. Reviewed source hashes were matched to the Windows sibling before compilation and checked again after it.

- Candidate XEX: **34,611,200 bytes**; XEX2 header checked.
- Candidate SHA256: `40cf7b8db901fa23f345bb9a8fdcc28609adc83c784ffa836c5fdd20bbe07ec7`.
- The linked EXE contains the candidate build marker.
- Runtime: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.

This is a compiled console candidate, not a console FPS result. Static source/emission evidence and QEMU PPC execution do not replace an Xbox gameplay test.

## Evidence and rollback

Raw logs, baseline/candidate XEX, build log, source-hash manifests and test results are outside Git at `E:\Baiduyundownload\salvia-toolchain\.work\cpu-dmul-native-20261001`. SDKs, ROMs, saved states, logs and binaries are not committed.

After creating this independent commit, `final-artifact.json` and runtime `FBNeo-CV1000-DMUL-NATIVE.json` record its full SHA and `git revert <sha>` command. Revert this report's commit, mirror reverted emitter/build-marker/test files to the Windows sibling, verify their hashes, and rebuild. Alternatively restore the external baseline XEX after verifying its baseline hash. Git revert alone does not update the sibling or an existing XEX.
