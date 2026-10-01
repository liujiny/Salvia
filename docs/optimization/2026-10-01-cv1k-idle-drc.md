# CV1000: service verified idle MOV.L reads inside DRC dispatch

Build ID: `cv1k-idle-drc-20261001-r1`.
Baseline commit: `2214d1993ba986c3c1d8087e5d3b8a845798b960`.
Baseline XEX SHA256: `ff4fbc6cfd510fc40aca9367693a2e815ab35d1d45c4188caa5bea924b4b88d0`.
Branch: `work/reversible-checkpoints`. One logical performance change; no push.

## New hardware evidence

The supplied log identifies DDPDFK with the IDLE-CANDIDATE build, 4684 frames,
66 timing samples, 16 disjoint workload-counted frames, 102400000 Hz and two
render cores. It is one window; cumulative GPU totals are not added to other
reports. It records 12825 watched MOV.L candidates, 12795 idle-PC matches
(99.77%), no unregistered observations and no lost candidate sites. The
0C1D134C delay-slot site contributes 12775 matches; its handler-visible PC is
0C1D1346. Thirty candidates at different handler PCs do not match.

CPU I/O averages 14.456 ms and drawing synchronization 2.378 ms in the sampled
core timing windows. Async VSync remains active without fallback/timeout;
GPU fallback is zero. These observations support targeting the watched load.
They do not establish a per-scene console FPS, and the upload/worker averages
are not a timing trace of the specific dropped-frame sprite.

## One performance change

A MOV.L watched-address guard can now return an encoded, source-validated
opcode to the C++ DRC dispatcher. The native entry commits its cached guest
registers, prior cycles and pending delay slot exactly as on the original
partial exit. The dispatcher recognizes this tag and services only a matching
board-declared watched address and current idle PC, then continues the existing
native dispatch chain when eligible. Unsupported operations, mismatches,
remapped handlers, revoked mirror metadata and short budgets retain the original
interpreter path.

The service uses the live driver-owned idle RAM/PC pointers from the preceding
diagnostic commit, validates mapping metadata and executes the unchanged
MOVLL helper. That helper updates EA and calls RL, preserving its WaitState and
ReadLong handling. The original speedhack_read_long still calls Sh3GetPC and
Sh3BurnCycles; there is no duplicated direct RAM read or shortened idle charge.
The service follows the original delay clearing/PC/PPC prologue, checks pending
IRQ after the read, and charges the base instruction AFTER the handler/IRQ.

The gain sought is eliminating the outer interpreter transition, slot/opcode
refetch and generic decode. Native block returns and the memory handler remain;
this is not an inlined native idle-loop replacement. Dynamic configuration is
read at runtime. No fixed game address/PC enters generated code. Full source
validation, self-write guards, mirror revocation, cache lifecycle, guest clocks,
blitter delays and serialized state layout remain intact. Generated entries
remain leaf functions and do not acquire a new Xbox calling convention.

The compiler reserves up to three distinct guarded exits per instruction,
including a separate tagged exit. Its capacity/budget checks were updated to
keep the exit array and emitted-code reservation bounded. Compiler-local
metadata grows on this cold compilation path; cache record/state formats do
not change. Runtime service checks add overhead that can offset part of the
saved decode/dispatch cost. A console A/B is necessary to establish net benefit.

## Verification actually performed

```sh
python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/salvia-idle-drc-host
python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root --output /tmp/salvia-idle-drc-ppc
```

- Eight host suites pass in optimized and ASan/UBSan modes. The new service
  suite checks 4107 operand, alias, budget, IRQ and guarded-rejection cases.
- Actual generated PowerPC execution under QEMU passes 673241 differential
  cases. Of these, 12545 exercise the new watched-load path, ordinary/delayed
  reads, all Rm/Rn combinations including self-loads, runtime idle PC changes,
  zero/positive idle burns, direct/generic mirrors, aliases, source edits and
  missing registration. The 4096 diagnostic observations also pass.
- XDK native disassembly confirms the ordinary service calls the original
  MOVLL and pending-IRQ helpers, with base-cycle charging afterwards. It has
  no workload-counter updates. This is compiler evidence, not Xbox execution.
- The same real-game harness runs baseline and candidate from common states
  for 600 frames each in DDPDFK and DDPSDOJ, using two rendering cores and an
  intermediate save/load. Every RGB565 video/stereo-audio hash, final raw
  frame and complete final state/VRAM match exactly.

| Count in the same 600-frame game replay | Baseline | Candidate |
| --- | ---: | ---: |
| DDPDFK interpreter steps | 1815435 | 1197973 |
| DDPDFK partial interpreter exits | 1327697 | 710235 |
| DDPDFK loads serviced by the new path | 0 | 617462 |
| DDPSDOJ interpreter steps | 1828608 | 1275655 |
| DDPSDOJ partial interpreter exits | 1081636 | 528683 |
| DDPSDOJ loads serviced by the new path | 0 | 552953 |

Interpreter steps fall 34.0% / 30.2%, respectively; partial interpreter exits
fall 46.5% / 51.1%. Each reduction equals the serviced-load count, so the loads
and their idle charge were moved, not omitted. All loads still return through
the original handler. Native returns are explicitly retained in the new log.
These are counted QEMU workload facts, NOT FPS improvements.

The private replay harness compiles both baseline and candidate CPU with
Count=true for all frames, so these absolute counts cannot be compared directly
with the console's 1-in-256 samples. The harness links the unchanged shared
renderer/device objects and the current CV1000 driver, and uses its GPU software
model, not Xenos or the Xbox frontend. Its game states and ROMs stay outside
Git. This validation covers these two replay segments, not every CV1000 title
or the user's exact dropped-frame sprite checkpoint.

## Build, identity and records

The existing checkpoint script completed FBNeo core Rebuild, SDL Build and
Salvia FBNeo frontend Rebuild with exit code zero. Reviewed source hashes match
the Windows runtime mirror, the linked EXE contains the build ID, and the
output has an XEX2 header.

- Output: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.
- Size: 34611200 bytes.
- XEX SHA256: `058c22ab7ce471f39ada2432c234746e90bf1e7e64dbd7c18c5f2265a0af55ae`.
- Build file timestamp: `2026-10-01T10:49:05.870686`.
- Raw input/source manifests, backup XEX, host results, build log, bounded
  native disassembly, game comparison and final commit identity:
  toolchain `.work/cpu-idle-drc-20261001/`.
- PPC test and private replay artifacts:
  `/home/humor/salvia-tests/cv1000-idle-drc/`.

Only source, tests and this review belong in the commit. SDKs, ROMs, XEX,
private states, raw logs and generated objects are not staged.

## Console comparison and rollback

The new build adds `drc_movll_service handled=... guest_cycles=... rejected=...`
to the existing pause report. Guest cycles are emulated cycles, not host time.
The existing candidate aggregate also includes serviced loads; their site origin
is unknown because they no longer enter interpreter fallback. A decreasing
partial/interpreter count alone does not prove faster execution.

Compare the same DDPDFK dropped-frame sprite scene with the diagnostic baseline
and this build, and repeat DDPSDOJ. Keep clock, saved state/input, core count,
filter and DIP settings identical. Retain Dynamic Recompiler, Thread Blitter,
Speed Hacks and GPU Blitter On. Check the build ID, handled count, CPU I/O,
partial/interpreter counts, async status and actual console FPS. The candidate's
console performance has not yet been measured. Rendering batch limit stays 128.

For an immediate binary rollback, copy
`E:\Baiduyundownload\salvia-toolchain\.work\cpu-idle-drc-20261001\baseline\fbneo.xex`
to the Xbox game directory as fbneo.xex; its baseline SHA256 is above. For source
rollback run `git revert <this-performance-commit>` in this checkout, mirror the
reverted reviewed paths to sibling `.work/Salvia`, then rebuild. A Git revert
alone cannot replace the existing runtime mirror or XEX. The exact change SHA
and revert command are recorded in external final-artifact.json after commit.
