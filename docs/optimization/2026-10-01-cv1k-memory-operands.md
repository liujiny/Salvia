# CV1000 SH3 native memory operands — 2026-10-01

## Baseline and scope

- Baseline commit: `28e01aae08314cdd82ad01aecffd074cb3e1dc8b`.
- Baseline XEX SHA256: `ef4564fa5dffb77585e2f8d94a45f7844997fe24bc3b6444f7b749e6e7d54868`.
- Candidate build: `cv1k-memory-operands-20261001-r1`.
- One logical change: eliminate redundant operand work in the existing SH3 PPC native memory emitter.
- Only FBNeo and its required SDL/frontend link were built. ROMs, XEX, raw logs, SDKs and generated objects are external evidence, not tracked files.

## Uploaded console evidence

The new DDPDFK log contains one nonempty pause window of 4,728 frames, with 67 randomly sampled core frames and 16 disjoint work-counted frames. Startup VMX transfer, VMX alpha mask, tiled atlas and GPU pixel tests passed. The pipeline reports `transfer=vmx-fused alpha_masks=vmx-bitpack`, GPU fallback is zero, and async present is active without errors.

| Sampled field | Value |
| --- | ---: |
| CPU and device I/O | 17.286 ms |
| Draw/synchronization | 5.942 ms |
| Total core frame | 23.412 ms |
| Sampled core peak | 47.286 ms |
| Worker wait average / peak, 75 samples | 2.323 / 23.073 ms |
| Native calls in 16 counted frames | 906,866 |
| Interpreter instructions in those frames | 26,253 |
| Partial exits | 5,785 |
| Registered blitter / DMA read fallbacks | 4 / 3 |

`cpu_io` includes SH3 execution and device I/O; it does not isolate SH3 host time from synchronous device work. Renderer uploads remain substantial: cumulative counters show 114,248 pages / 7,487,356,928 bytes. Timing summaries and cumulative GPU counters have different scopes. Do not add cumulative snapshots, derive measured FPS from these times, or treat this recording as a controlled firing/nonfiring comparison. It establishes that the prior GPU path is enabled and that busy/DMA poll service fallbacks are rare, while native CPU work is still extensive. It does not identify the exact impact sprite routine or prove this candidate's console gain.

## Change and correctness constraints

1. A native load requests the old destination register only if it is also an address operand: the base, R0 index, or GBR base. Stores continue to request their source value. A destination already cached and dirty keeps its old dirty snapshot for guard exits; an uncached destination stays in architectural RAM until the actual load commits it.
2. Direct RAM translation retains the complete guest effective address in scratch register r11. Code-write range checks use scratch r0 for their comparison instead of overwriting r11. Once every guard succeeds, EA stores r11 without repeating guest address arithmetic. Generic mappings still reconstruct the original guest address, and constant operands still initialize it explicitly.

No guard is removed. RAM registration, external alias filtering, alignment, watched RAM and device service exits, code-write protection, source validation, handler accesses, wait states, cycles, IRQ and delay-slot behavior remain in place. The changes apply to all users of the SH3 PPC emitter (including eligible CV1000 games); other CPU implementations are unchanged. They do not introduce multi-core scheduling or alter guest CPU speed.

## Emitted instruction evidence

A private probe compiled the common emitter for a 32-bit big-endian PPC ABI and counted the successful direct-RAM path before guard-exit bodies and the block epilogue:

| SH3 opcode / example | Baseline | Candidate |
| --- | ---: | ---: |
| `6212` MOV.L @R1,R2 | 17 | 15 |
| `6112` MOV.L @R1,R1 | 16 | 15 |
| `021E` MOV.L @(R0,R1),R2 | 18 | 16 |
| `5213` displacement MOV.L load | 17 | 15 |
| `D27F` PC-relative MOV.L | 7 | 6 |
| `2132` MOV.L R3,@R1 | 22 | 21 |
| `2136` MOV.L R3,@-R1 | 21 | 21 |
| `6216` MOV.L @R1+,R2 | 16 | 15 |

These counts establish a concrete reduction in emitted work. They do not measure host instruction latency, the game's dynamic instruction mix, or Xbox FPS. No unverified percentage FPS improvement is claimed.

## Tests actually run

### PPC instruction differential tests

```text
rtk proxy python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root --output /mnt/e/Baiduyundownload/salvia-toolchain/.work/cpu-memory-operands-20261001/ppc-tests
```

PASS: **690,661 cases**, including **1,024 new cases** covering cold/dirty destinations, address/destination aliases, R0 indexing, postincrement, all external aliases, internal/misaligned guard rejection, and guarded branch delay slots. Existing coverage includes every translated opcode under mapped/direct RAM, same-block code writes, fetch remapping and source mutation, page boundaries, watched MOV.L and registered device service semantics. The real emitted PPC code was executed under QEMU and compared to the interpreter's full architectural state, memory and handler counts.

### Game A/B replay

```text
rtk proxy python3 /home/humor/salvia-tests/cv1000-memory-operands/game/build.py
rtk proxy python3 /home/humor/salvia-tests/cv1000-memory-operands/game/run.py
```

Separate baseline/candidate SH3 objects were built with identical test-only work counters enabled for all frames. Both used the same current GPU model/AltiVec alpha code and unchanged driver objects. DDPDFK and DDPSDOJ each ran 600 frames with coin/start, alternating fire and horizontal movement; each saved and reloaded at offset 240. All four executions exited zero. Each game's nine artifacts were byte-identical: frame/audio hashes, periodic and final raw frames, mid-run and final states. Complete logs and work counts were identical. This change lowers native host work; it does not lower interpreter/partial counts or skip simulated work.

An initial private fixture link omitted test counter exports and failed. Adding the same external-only exports and counted timer wrapper to both CPU copies corrected the fixture; the four successful executions above used those matched copies.

The replay tests use cached ROMs and an exact software model of shader rendering, not physical Xenos or the Xbox frontend, and do not recreate the user's precisely reported contact-effect scene. QEMU wall time is not console FPS.

### Xbox build and identity

```text
rtk proxy /mnt/c/Windows/System32/cmd.exe /c "E:\Baiduyundownload\salvia-toolchain\.work\cpu-cache-ready-20260930-205053\git-baseline\tools\build_fbneo_checkpoint.cmd E:\Baiduyundownload\salvia-toolchain"
```

PASS: FBNeo core, SDL and linked frontend; script exited **0**. Candidate XEX: **34,611,200 bytes**, SHA256 **`f98ea82792eab9e89f608c3a11902fab97712291c512b3a0e7fa3dbcca187ce4`**.

Source hashes were verified equal to the Windows runtime sibling before compilation. The XEX2 header and linked EXE's candidate build marker were checked after compilation.

## External artifacts and rollback

Evidence: `E:\Baiduyundownload\salvia-toolchain\.work\cpu-memory-operands-20261001`.
Runtime XEX: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.
Private PPC game fixture: `/home/humor/salvia-tests/cv1000-memory-operands/game`.
The external `final-artifact.json` and runtime `FBNeo-CV1000-MEMORY-OPERANDS.json` record the full candidate commit and `git revert <candidate commit>` command after commit creation.

To restore baseline source, revert this report's commit, mirror the reverted SH3 emitter/build marker/test files to the Windows sibling, verify their hashes, then rebuild. To restore only the previous executable, copy the external `baseline-fbneo.xex` after verifying its baseline SHA256. A Git revert alone does not update the Windows sibling or deployed executable.

## Next console check

Test the same DDPDFK stage and enemy: pause once after a short period without firing, then pause again after continuous fire hitting enemies. Keep render-core count, CPU clock and filter unchanged. Distinct pause windows in the existing GPU/audio logs will help compare `cpu_io`, draw synchronization and peaks; cumulative GPU counters must still be treated separately. There is no confirmed Xbox FPS result for this build yet.
