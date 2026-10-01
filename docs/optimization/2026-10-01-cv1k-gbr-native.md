# CV1000 native GBR stack transfers — 2026-10-01

## Baseline and received console evidence

- Baseline commit: `ac9a1178271983257b41dc2bde6a85a8552af518`.
- Baseline XEX SHA256: `b5e66e19da51f4117d673bb7815f581f892aa525d70b3bf77e84101afacf1a32`.
- Candidate build: `cv1k-gbr-native-20261001-r1`, retaining the separately committed multiply and carry implementations.
- Relevant baseline emitter, test and build-ID files matched the Windows sibling before editing. The worktree was clean. Source snapshots, old XEX and uploaded logs were saved outside Git before changes.

The received DDPDFK log identifies the **final carry build**, so it is valid follow-up evidence for the preceding iteration. The 39-frame startup window has no timing/work samples and no GPU batches; normal-play analysis uses only the subsequent window:

| Received window / sampled field | Value |
| --- | ---: |
| Window frames / timing samples / disjoint work frames | 5,002 / 70 / 19 |
| CPU and device I/O | 17.794 ms |
| Draw and synchronization | 4.131 ms |
| Core total / sampled peak | 22.066 / 44.906 ms |
| Frontend active loop / sampled peak | 23.141 / 45.728 ms |
| Interpreter steps | 26,264 |
| No-entry / short-budget / partial exits | 4,036 / 9,338 / 9,646 |
| Untracked fallback sites | 24,235 (92.27%) |
| Interpreter opcode high byte 4F | 1,382 |

CPU/device work remains the largest sampled component. GPU self-tests pass, compositor fallback is zero, async present stays active with zero fallbacks/timeouts. Registered blitter-busy and DMA reads are serviced 8,857 and 4,735 times in the disjoint counted frames, with only 3 and 9 fallback observations. Their real handler paths remain active. However, sampled GPU upload averages 2.612 ms and worker wait peaks at 24.468 ms; these can still contribute to stutter. There is no evidence that every low-FPS event is exclusively SH3 work.

The earlier multiply log used a different mixed interval. Current no-entry counts per counted frame are lower, but partial counts per counted frame are higher. Neither establishes a controlled FPS improvement/regression. The 64-site table misses most sites, so its admitted ranking is not used as a global hotspot ranking. High byte 4F groups multiple instructions; it does not isolate GBR.

Audio has 89 additional underrun callbacks over 5,409 additional callbacks, with no dropped samples. Tempo floor is not a constant-game-FPS measurement. The state log records a successful 151,786,536-byte slot-0 load. Core timing is per window; GPU/audio summaries may be cumulative and were not summed across windows.

## Reason and implementation

The prior complete private 600-frame DDPDFK audit independently counted 20,606 `4F13` and 20,606 `4F17` no-entry executions. These frequent interrupt-stack transfers remained unsupported after native multiply/carry. This provides an isolated next target without treating incomplete console site rankings as complete evidence.

The emitter now supports:

- `STC.L GBR,@-Rn` (`4n13`): decrement stack by four, publish EA, store GBR; **two guest cycles**.
- `LDC.L @Rn+,GBR` (`4n17`): publish EA, load GBR, increment stack by four; **three guest cycles**.

Both reuse the existing checked longword-memory emitter and G_GBR register-cache slot. Extra cycles are added only after successful emission so every earlier guard snapshot retains its original PC, delay state, dirty registers and cycle cost. Native admission still reserves the largest possible path cost. No PC/address is hard-coded.

Only ordinary mapped or explicitly validated direct RAM runs natively. MMIO, watched reads, misalignment and instruction-byte writes retain the existing interpreter/real-handler path, including RL/WL, WaitState and Sh3BurnCycles behavior. EA and stack changes occur after native guards; fallback restores architectural state before interpreting. Full source validation, mapping invalidation, IRQ gates, delay-slot rules and GPU batch limit 128 remain intact. SR-loading, bank-changing and exception-return instructions are outside this change. There are no new timers, gameplay file writes, threads, device services or sampling-frequency changes.

## Tests executed

```text
rtk proxy python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root --output /mnt/e/Baiduyundownload/salvia-toolchain/.work/cpu-gbr-native-20261001/ppc-tests
rtk proxy python3 /home/humor/salvia-tests/cv1000-gbr-native/game/build.py
rtk proxy python3 /home/humor/salvia-tests/cv1000-gbr-native/game/run.py
```

- Real generated big-endian PPC execution passed **842,085 differential cases** under QEMU, including **5,504 explicit GBR cases**. Independent assertions prove native execution at exact 2/3-cycle budgets, stack/EA/GBR results and unchanged SR. All 16 stack registers, boundary GBR values, generic/direct RAM, aliases, internal/handler addresses, dirty GBR/address slots, misalignment, self-modifying stores, folded delays and short budgets are covered. Existing multiply, carry, mapped-memory, source-validation and tagged-handler suites pass.
- DDPDFK and DDPSDOJ each ran 600 frames from established checkpoints with matching coin/start, alternating firing/movement and save/load at offset 240. All four runs exited zero. Each game's **nine frame/audio hash, periodic/final raw-frame and mid/final-state artifacts were byte-identical**.
- Guest-frame cycle records, registered device services and GPU/cache work match exactly. Private baseline/candidate SH3 objects used the same all-frame Count=true diagnostic wrapper and the current private software shader/AltiVec alpha model. The production low-frequency counter scheme is unchanged.

| Work count / 600 frames | DDPDFK baseline | DDPDFK GBR | DDPSDOJ baseline | DDPSDOJ GBR |
| --- | ---: | ---: | ---: | ---: |
| Interpreter steps | 562,683 | 521,471 | 718,727 | 677,515 |
| No-entry exits | 122,009 | 80,797 | 126,056 | 84,844 |
| Native calls | 19,443,772 | 19,402,560 | 28,549,011 | 28,507,799 |
| Lookups | 19,715,041 | 19,632,617 | 28,931,174 | 28,848,750 |
| Validation words | 164,134,596 | 164,334,576 | 261,633,332 | 261,833,075 |
| Budget exits | 149,260 | 149,260 | 256,107 | 256,107 |
| Partial exits | 235,516 | 235,516 | 265,265 | 265,265 |

Both games eliminate **41,212 interpreted instructions** and native-call exits, and **82,424 lookups**. Interpreter counts fall **7.32% / 5.73%** relative to the carry baseline. Longer native spans slightly increase total validated words, about 0.12% / 0.08%; source validation is preserved. MMIO partial exits are unchanged. These are instruction-work changes, not measured Xbox time/FPS gains, and do not prove the exact contact-effect slowdown is solved.

The fixture does not reproduce the exact reported contact sprite or execute physical Xenos/Xbox frontend. QEMU timings include translation and diagnostic overhead and are not console benchmarks. PPC tests use a 32-bit ABI; existing word/address emission rules remain required for Xenon. Actual console performance and game progression remain to be tested.

## Xbox build and identity

```text
rtk proxy /mnt/c/Windows/System32/cmd.exe /c "E:\Baiduyundownload\salvia-toolchain\.work\cpu-gbr-native-20261001\build_fbneo_incremental.cmd E:\Baiduyundownload\salvia-toolchain"
```

PASS, exit **0**: FBNeo core, SDL and required frontend linkage; no other emulator core was built. Reviewed production hashes matched the Windows sibling before compilation and all reviewed hashes were checked afterward. The final test-only self-modification address was corrected to an aligned code address and revalidated; production inputs were unchanged. The linked EXE contains the candidate build marker and not the old carry marker; the XEX2 header was checked.

- Final XEX: **34611200 bytes**.
- SHA256: `2ea8dfe308b7c362febe69ca6aa47dbe65bb706bc6b843ae5e2d239527ac9906`.
- Runtime: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.

## Evidence and rollback

External evidence: `E:\Baiduyundownload\salvia-toolchain\.work\cpu-gbr-native-20261001`, including baseline source/XEX, received logs, analysis, PPC results, game comparison and build identity. The private game fixture is `/home/humor/salvia-tests/cv1000-gbr-native/game`. No ROMs, logs, saved states, SDKs, binaries or generated objects are committed.

After commit creation, external `final-artifact.json` and runtime `FBNeo-CV1000-GBR-NATIVE.json` record the full candidate SHA and exact `git revert <sha>` command. To remove this optimization, revert this report's commit, mirror its reverted emitter/build-marker/test files to the Windows sibling, verify hashes and rebuild. Alternatively restore the separately saved baseline XEX with SHA256 `b5e66e19da51f4117d673bb7815f581f892aa525d70b3bf77e84101afacf1a32`. Git revert alone does not update the sibling or existing XEX.

## Console follow-up

Test the final GBR build in the same contact-heavy stage with the same ROM, clock, render-core setting and filter. Pause to log a window just before the segment, then pause after sustained firing/contact to separate that interval from menus/non-firing gameplay. Upload the existing logs. A stable 50 FPS result is not claimed from these tests.
