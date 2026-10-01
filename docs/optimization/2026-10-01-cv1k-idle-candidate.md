# CV1000: distinguish watched MOV.L reads from idle-PC matches

Build ID: `cv1k-idle-candidate-20261001-r1`.
Baseline commit: `644b2ccae3c64f1f070f4f979c8eacc467e7cfd7`.
Baseline XEX SHA256: `b9d8656f94aa1f1749e94ed45cd4b52a0de47dc221980c9e9c592585e06dbe3e`.
Branch: `work/reversible-checkpoints`.

## Reason and scope

This is the handoff's first, diagnostic-only checkpoint. Existing logs report
watched MOV.L instructions at 0C1D134C reading 0C002310 in a delay slot. That
instruction address is not necessarily the PC seen by speedhack_read_long:
the interpreter clears m_delay before RL, and Sh3GetPC then sees the committed
branch target. Consequently watched-read counts alone cannot prove the idle
condition matched. Treat the reported CPU bottleneck as a working hypothesis,
not proof that every watched load burns idle cycles.

The CV1000 driver registers pointers to its current hacky_idle_ram and
hacky_idle_pc after installing its read mirror. Sampled observations read these
live values; no game address or PC is hard-coded into the optimization. Mirror
replacement revokes registration. This registration only supplies diagnostic
metadata; it does not alter the emitter, dispatcher, source validator, memory
handler or execution engine. No pointers or counters enter save states.

In the existing counted specialization, watched MOV.L reads now record fetched
PC, the handler-visible PC after delay clearing, opcode, effective address,
driver idle RAM/PC, delay context, fallback origin and hit/match counts. A
separate 16-site table avoids competition with unrelated general fallback
sites. The exact aggregate counter includes all qualifying observations;
site_untracked explicitly reports table capacity loss. Site counts are sampled
observations, not total game counts or globally ranked hot sites.

No per-instruction clocks, new gameplay file writes or new log files are added.
The ordinary Count=false loop does not call the observer. Output is appended
only to the existing pause report in cv1000-gpu.log. No FPS gain is claimed for
this diagnostic build.

## Verification

Run from this checkout:

```sh
python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/salvia-idle-host
python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root --output /tmp/salvia-idle-ppc
```

The first command passes seven production helper/control suites in optimized
and ASan/UBSan modes. The candidate tests cover aliases, the two accepted
handler PC values, slot-PC mismatch, dynamic metadata, bounded site admission,
report formatting and profile reset. Existing fallback observer test stubs were
updated for the new read-only state dependencies.

The PowerPC runner additionally executes the real production SH3 core under
QEMU and checks 4096 sampled observations for zero register/cycle/callback
mutation, live driver PC changes, post-clear handler PC identity and mirror
lifecycle revocation. The normal instruction differential suite remains in the
same runner. Its final result and native build identity are recorded below
once completed. This is not a game replay, Xbox execution or a console FPS test.

Raw baseline copies, test executables/logs, mirror hashes, XEX backup and build
evidence live outside Git under toolchain .work/cpu-idle-candidate-20261001 and
/home/humor/salvia-tests/cv1000-idle-candidate. No ROM, SDK or build artifact is
staged. Only reviewed source and test paths are mirrored to sibling .work/Salvia.

## Hardware observation needed before speedhack fusion

Run DDPDFK from the same state through the specific sprite causing the drop,
then pause. Repeat for DDPSDOJ. Keep CPU clock, render cores, DIP settings and
filter unchanged. In cv1000-gpu.log check this build ID, then read:

- movll_idle_candidates: watched, idle_condition_match, unregistered,
  site_untracked.
- movll_idle_site: pc, handler_pc, opcode, address, idle_ram, idle_pc,
  delay, reason, hits and idle_condition_match.
- Existing cpu_io, partial count, interpreter_steps and GPU/present status.

The next performance commit depends on these specific-scene observations. It
must preserve RL, WaitState, the handler's idle burn, IRQ handling and delay
semantics. A zero match count falsifies the claim that these reads activate
idle charging; it still leaves a watched-handler overhead candidate but
requires a different justification. Do not infer FPS from sample-loop timing
reciprocals or guest cycle counts. GPU batching stays at 128 pages.

## Rollback

Use `git revert <diagnostic-commit>` in this checkout; mirror the reverted paths
to sibling .work/Salvia and rebuild. Reverting Git alone does not replace the
runtime source or XEX. Immediate binary rollback is the verified baseline at
`.work/cpu-idle-candidate-20261001/baseline/fbneo.xex`; restore it as the Xbox
game directory's fbneo.xex. The exact diagnostic commit and XEX hash are stored
in the external final-artifact.json after committing. No automatic push.

## Completed validation and artifact

PowerPC/QEMU execution passed 660,696 differential cases and the additional
4096 diagnostic observations. All seven host suites passed in both modes.
FBNeo core Rebuild, SDL Build and frontend Rebuild completed successfully.
The linked EXE contains the new build ID; the distribution file has an XEX2
header. Reviewed runtime-source hashes match the Git checkout.

- XEX size: 34611200 bytes.
- XEX SHA256: `ff4fbc6cfd510fc40aca9367693a2e815ab35d1d45c4188caa5bea924b4b88d0`.
- Windows output: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.
- Full-game replay and console performance: not performed for this candidate.

The native emitter remains unchanged. This commit confirms diagnostic
correctness and buildability, not the eligibility frequency of DDPDFK's
specific dropped-frame scene. That frequency still requires the diagnostic
build's console logs before a speedhack-fusion performance commit.
