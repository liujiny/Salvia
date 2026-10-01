# CV1000 native SUBC/ADDC — 2026-10-01

## Baseline and reason

- Baseline commit: `91a18f97a81fdef9911c0cb02ff326b24c497424` (the separately recorded DMULS/DMULU optimization).
- Baseline XEX SHA256: `40cf7b8db901fa23f345bb9a8fdcc28609adc83c784ffa836c5fdd20bbe07ec7`.
- Candidate build: `cv1k-carry-native-20261001-r1`; it includes the preceding multiply optimization.
- The complete private DDPDFK audit found 82,396 no-entry SUBC executions in 600 frames, of which 80,040 used the same source and destination register. These represent instruction workload, not sampled host time. The earlier 64-site console table omitted about 91% of fallback sites, so its admitted-site rankings were not interpreted as global hotspots.

## Implementation

SUBC and ADDC now stay in generated PPC code. The generic path computes the intermediate result, compares low 32-bit words for the first borrow/carry, applies incoming T, compares again, and ORs the two conditions into outgoing T. Other SR bits and source registers are preserved; both instructions retain one guest cycle.

For SUBC Rn,Rn, the exact result is `-T` and outgoing borrow is incoming T. The emitter extracts T and negates it, without loading the old destination or changing SR. This works with dirty cached operands and in eligible delay slots. It is an exact algebraic specialization, not a game-PC or RAM-address speedhack.

Comparisons are encoded as `cmplw`, with the L bit clear. They check the low word even when Xenon's 64-bit add/sub leaves nonzero upper GPR bits. No XER CA flag is used. Existing code snapshots, RAM and mapping guards, handlers, wait states, IRQ and delay-slot gates remain in place. No per-instruction clocks, runtime file I/O, extra render threads or changed GPU batch/cache limits were added. Only the SH3 PPC implementation changes.

## New console logs received during work

The newly uploaded log identifies the preceding multiply build, `cv1k-dmul-native-20261001-r1`. Its formal DDPDFK window has 6,817 frames, 96 timing samples and 28 disjoint work-counted frames. GPU/VMX self-tests pass, fallback remains zero and async present reports no timeouts or fallbacks.

| Sampled field | Value |
| --- | ---: |
| CPU/device I/O | 16.941 ms |
| Draw/synchronization | 3.997 ms |
| Core total / sampled peak | 21.092 / 51.934 ms |
| Frontend active loop / sampled peak | 22.533 / 52.707 ms |
| No-entry exits in counted frames | 9,800 |

SUBC/ADDC sites `0C3BF6E0:333A`, `0C3BF6E2:312A`, `0C3BF768:312E` remain in the admitted-site list, supporting the current target. Startup has only one timing sample before GPU initialization and is excluded from normal-play assessment. Scene/input conditions are not controlled matched replays, so no percentage console gain or regression is assigned to the multiply optimization. CPU/device time is not pure SH3 time, and GPU upload/worker peaks can still contribute to stutter. This log predates the current carry candidate and is not its FPS result.

## Tests executed

```text
rtk proxy python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root --output /mnt/e/Baiduyundownload/salvia-toolchain/.work/cpu-carry-native-20261001/ppc-tests
rtk proxy python3 /home/humor/salvia-tests/cv1000-carry-native/game/build.py
rtk proxy python3 /home/humor/salvia-tests/cv1000-carry-native/game/run.py
```

- Real emitted 32-bit big-endian PPC code passed **836,229 differential cases** under QEMU. There are **89,536 new explicit carry cases**: every source/destination pair, both incoming T values, all boundary combinations, an independent wide arithmetic/borrow oracle, dirty SR/destination state, equal operands, carry chains, MOVT and branch consumers, delay slots and short budgets. The preceding 44,768 multiply cases and existing memory/self-modification/service tests also pass.
- DDPDFK and DDPSDOJ each ran 600 frames from established checkpoints, including coin/start, alternating firing and movement, and save/load at offset 240. All four runs exited zero. Each game's nine frame/audio hash, periodic/final raw-frame and mid/final-state artifacts were byte-identical.
- Per-frame guest cycles, GPU/cache work and registered device service counts were identical. The baseline work counters match the preceding multiply candidate exactly. Separate SH3 objects shared the same private all-frame Count=true wrapper and current software shader/AltiVec alpha model; no production counting frequency was increased.

| Work count / 600 frames | DDPDFK baseline | DDPDFK carry | DDPSDOJ baseline | DDPSDOJ carry |
| --- | ---: | ---: | ---: | ---: |
| Interpreter steps | 644,449 | 562,683 | 909,976 | 718,727 |
| No-entry exits | 206,761 | 122,009 | 318,812 | 126,056 |
| Native calls | 19,520,404 | 19,443,772 | 28,700,737 | 28,549,011 |
| Lookups | 19,874,009 | 19,715,041 | 29,274,166 | 28,931,174 |
| Validation words | 164,070,164 | 164,134,596 | 261,649,187 | 261,633,332 |
| Budget exits | 146,844 | 149,260 | 254,617 | 256,107 |
| Partial exits | 235,516 | 235,516 | 265,265 | 265,265 |

This step reduces interpreter executions **12.69% / 21.02%** relative to the multiply baseline. The two independently committed steps together reduce them **22.20% / 29.00%** relative to `7a2f28ad17f67a7cfaf8087d58c826cd105d5839`. Longer compilable blocks slightly increase short-budget exits; exact cycle admission remains required. These are workload improvements, not measured host-time/FPS improvements.

The private replay does not recreate the user's exact contact sprite or run physical Xenos/Xbox frontend. QEMU run durations include translation and all-frame diagnostic overhead and are not used as a console performance benchmark. The PPC execution tests run under a 32-bit ABI; Xenon's upper-half protection is established by the explicit word-compare emission, and physical console gameplay remains the final validation.

## Xbox build and identity

```text
rtk proxy /mnt/c/Windows/System32/cmd.exe /c "E:\Baiduyundownload\salvia-toolchain\.work\cpu-carry-native-20261001\build_fbneo_incremental.cmd E:\Baiduyundownload\salvia-toolchain"
```

PASS, exit **0**: the existing checkpoint script's external `/t:Build` copy built FBNeo and required SDL/frontend linkage; no other emulator core was built. Reviewed source hashes were matched to the Windows sibling before compilation and checked again afterward. The linked EXE's candidate build marker and XEX2 header were verified.

- Candidate XEX: **34,611,200 bytes**.
- Candidate SHA256: `b5e66e19da51f4117d673bb7815f581f892aa525d70b3bf77e84101afacf1a32`.
- Runtime: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.

## Evidence and rollback

External evidence: `E:\Baiduyundownload\salvia-toolchain\.work\cpu-carry-native-20261001`, including the original baseline snapshots/XEX, newly received console logs, hash manifests, PPC results, game comparison and build log. ROMs, saved states, SDKs, logs, binaries and generated objects are not committed.

`final-artifact.json` and runtime `FBNeo-CV1000-CARRY-NATIVE.json` record the full candidate commit and `git revert <sha>` command after commit creation. To remove only this carry optimization, revert this report's commit, mirror its reverted emitter/build-marker/test files to the Windows sibling, verify hashes and rebuild. Alternatively restore the separately saved baseline XEX with SHA256 `40cf7b8db901fa23f345bb9a8fdcc28609adc83c784ffa836c5fdd20bbe07ec7`, which retains multiply support. Git revert alone does not update the sibling or existing executable.

To restore the pre-multiply baseline, first revert this carry commit, then revert `91a18f97a81fdef9911c0cb02ff326b24c497424`, mirror and rebuild, or restore the verified XEX backup under `cpu-dmul-native-20261001`. Shared build-ID/test context may require resolving conflicts if reverting an earlier commit out of order; the production arithmetic implementations are separate.

## Console follow-up

Use the final `cv1k-carry-native-20261001-r1` XEX for the same stage/contact-effect test, then upload the existing logs. A pause immediately before and after a representative contact-heavy segment yields a useful interval. Hold ROM, CPU clock, render cores and filter constant. Neither a stable 50 FPS result nor the exact severe impact-effect cause is claimed from the tests above.
