# CV1000: bounded recovery after a transient forced synchronous display

Build ID: `cv1k-async-recovery-20260930-r1`.
Baseline: `c5037d4892dd7abd7e1dbd87ac21b4d9484931ce` (HOT-FALLBACK), branch `work/reversible-checkpoints`.
This is one reversible display-recovery change. No GitHub push is requested.

## Incoming console evidence

Raw inputs are preserved at toolchain `.work/async-recovery-20260930-223136/baseline/` with a SHA256 manifest. The three supplied logs and original optimization report are not edited. The latest GPU file has 73 lines, all HOT-FALLBACK. Its last pause window is lines 39-73: 3664 frames, 63 timing samples, and 24 separate workload-count frames. Settings: ddpsdoj, 102400000 Hz, 2 render cores, DIP 00,07,00,00, bpp=4.

| Last combat window | Prior WORK-PROFILE | HOT-FALLBACK input |
| --- | ---: | ---: |
| Frames / timing samples | 3037 / 53 | 3664 / 63 |
| cpu_io ms | 15.622 | 15.568 |
| core total ms | 16.527 | 16.557 |
| draw_sync ms | 0.775 | 0.827 |
| frontend sampled loop ms | 18.215 | 18.746 |
| sampled core peak ms | 23.882 | 27.705 |

These are unequal scene/input/sample windows, not a causal A/B. The last decode change has not demonstrated a stable speedup; it remains unchanged as this experiment's direct baseline. It still interprets the same instructions.

The new observation is display fallback. The first report is async-active with no failures (GPU line 33). The last has `presentation=sync-fallback`, `active=0 failed=1 retained=0 activations=1 fallbacks=1 timeouts=0` (lines 67-68). The normal after-Present bucket contains 263 batches, only FOUR timing samples, and readback_wait=4.456 ms; async after-Present is 0.629 ms in a separate cumulative bucket (lines 69-71). This identifies a newly degraded display mode, not a measured whole-frame slowdown or GPU-only execution time. Do not sum these cumulative snapshots or infer FPS from reciprocals.

The old trace does not encode an exact fallback reason or UI visibility. It cannot establish whether the user opened Guide, whether resources were recreated, or whether a runtime event forced synchronization. The relevant confirmed code behavior is that the `forced_sync` branch sets a sticky failed flag, with no recovery when UI subsequently closes. This change repairs that particular recoverable path; it does NOT assume every fallback was a UI event.

The audio log still contains underruns/time stretching. The state log ends load-done ok=1 for 151788652 bytes with 21327872 bytes free. No new large allocation or emulated-memory change is introduced.

## Recovery contract

- Classify forced synchronization separately from resource-create failure, pending-swap timeout, submission failure and reset/fence timeout. The latter faults remain sticky.
- Create one notification listener on an explicit async enable request, using the existing installed XDK declarations. Repeated per-batch enables do not recreate listeners. Close it on disable or video teardown. Listener failure disables automatic recovery, not normal presentation.
- Only while handling the transient forced-sync fault, drain at most 32 XN_SYS_UI notifications per frame. No polling or new clock calls occur on the healthy asynchronous path. UI starts unknown; a close without a witnessed open cannot authorize a retry. A full drain budget returns not-ready rather than deciding from stale queued state.
- Require an observed open/close cycle to remain closed for at least 1000 ms. A new open or unknown state cancels the quiet interval. A close token authorizes at most one attempt, preventing repeated mode flips while no new close has occurred.
- Before attempting resource creation, wait until the previous extra front buffer has been safely retired. Then boundedly fence the latest synchronous Present. Timeout becomes sticky and cannot authorize premature release or async activation.
- Preserve the existing two-buffer policy, allocation headroom check, swap ordering, 100 ms/200-poll timeout bounds and VSync interval. Do not clear all failures periodically.

The simple UI-cycle observer is shared by production and tests. The actual XNotify adapter remains under the existing Present critical section; the DPC callback is unchanged and contains no new calls. The recovery policy may spend bounded time recreating resources once per confirmed close. This is recovery of an existing fast mode, not a claim that the normal async renderer itself is faster.

## Pause diagnostics

The original three files and disjoint low-frequency CPU/workload sampling remain. No live file writes were added. The existing GPU pause report adds:

`present_recovery reason=... forced=... attempts=... last_epoch=... ui_state=... listener=...`

Reasons: 0 none, 1 forced-sync, 2 resource creation, 3 queue timeout, 4 submission failure, 5 reset/recovery fence timeout. UI: 0 closed, 1 open, 2 unknown. Attempts are not successes; inspect `present_state active=1 failed=0` and increased activations to confirm recovery. The original eight-field presentation getter is unchanged; a separate six-field pause getter avoids breaking its callers.

## Executed validation

The unmodified baseline presentation test passed first. Candidate optimized and ASan/UBSan presentation-policy tests passed the old lifecycle, queue, timeout, wrap and 100000-event sequence, plus explicit recovery scenarios. They assert that no queued or displayed resource is destroyed or overwritten.

`run_recovery.py` additionally extracts the real `XBOX_CoreUiRequest` and `XBOX_CoreRecoveryEpoch` from the platform source. Its modeled XNotify functions exercise null/invalid-handle creation failure, no listener, unknown/initial-close states, paired open/close events, duplicate close, a 70-message backlog exceeding the 32-event drain limit, disable and reinitialization. Both builds passed.

The five existing SH3 suites passed optimized and ASan/UBSan configurations without source changes to the CPU algorithms. Across the recorded recovery and CPU runners, 14 test executions passed. These tests do not emulate real XAM, GPU scanout or a full DDPSDOJ run, and they do not establish console FPS or actual Guide notifications. Native build/linking is checked separately.

## Build and rollback

Mirror only the reviewed paths from `.work/github-publish-salvia` to `.work/Salvia`, checking input baseline and post-copy hashes. The build uses the preserved `.work/cpu-cache-ready-20260930-205053/git-baseline/tools/build_fbneo_checkpoint.cmd` with the toolchain root. Custom build targets can copy XEX before final validation, so verify build and distribution copies independently.

Rollback binary: `.work/fbneo-before-async-recovery.xex`, exact HOT-FALLBACK SHA256 `c359029e0028dbea227897dc26f3c8e0fd387142e499658c4f3a7ce1e333b89b`.
A `git revert <this-commit>` in the record checkout must be mirrored to the runtime tree and rebuilt before it changes the runnable binary. No SDKs, keys, raw logs, saved games or compiler outputs are committed.

No SH3 instruction, source validation, memory mapping, GPU batching, shader, audio algorithm, guest clock or frame-skip change is included. The rejected TAGTABLE/256-page/precise-dependency/more-core experiments are not retried.

## Native build and final artifact

FBNeo core Rebuild, SDL Build and Salvia frontend Rebuild completed with exit code 0. The existing post-image step copied an uncompressed XEX; final validation independently compared build/distribution hashes and the embedded build ID.

Bounded native dumpbin inspection succeeded. `XBOX_AsyncRecover` at 0x825960c0..0x82596240 checks transient reason 1, compares the 1000 ms quiet interval, checks resources/retiring, and retains the bounded synchronous fence path. `XBOX_CoreRecoveryEpoch` at 0x82596868..0x82596938 calls the linked XNotifyGetNext import and enforces the 32-event drain bound. The new pause getter and the existing Present wrapper are also linked. These checks confirm the intended native control flow, not real system-UI delivery or a console speedup.

- Build time on the Runner: 2026-09-30 22:42:37.
- Output: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.
- Build ID: `cv1k-async-recovery-20260930-r1`.
- Size: 34611200 bytes, XEX2 header.
- SHA256: `8fc88d5337915c33f6f0374df8a1931e1a62e12b7cd568c3f2fa9ad2a72889bc`.
- Existing SH3 source, instruction emitter, input logs and original report remain byte-identical to the baseline snapshots.
- Evidence: `.work/async-recovery-20260930-223136/final-artifact.json`, `baseline-manifest.json`, `mirrored-files.json`, bounded native disassembly, `tests-recovery/`, `tests-cpu/`, and `build.log`.
- New console recovery/FPS tests, full-game replay and generated-PPC differential tests were not run.

For the hardware check, first observe normal gameplay, then open and close the Xbox system UI and resume the same scene. Pause after normal gameplay has continued to inspect active/failed, recovery attempts and reason. If no opening event was observed, automatic recovery remains disabled by design; inspect ui_state/listener rather than assuming success. This change is not a fix for every possible forced-sync event.

The local performance commit carries the related source, tests and review; its exact SHA and parent are recorded in the external artifact manifest after commit. No push is performed.
