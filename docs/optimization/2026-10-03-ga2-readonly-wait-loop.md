# GA2: coalesce repeated V60 read-only waiting

Baseline: `6d7c53dffa9b6029f9ae1653e7e9042b3e34fc6b`.
Console feedback on that baseline: approximately 36 FPS with two rendering cores.

## Finding

A host-only 3,000-frame instruction profile found 43,302,692 executions at `0x133505` and 43,302,693 at `0x13350b`. These are `TEST1 #0, 0x2c08[r25]` and `BNE8 -6`: a RAM status-bit polling loop. They account for over eighty percent of V60 instructions in this replay. This count describes the replay, not every gameplay scene or a measured Xbox CPU-time share.

The existing interpreter repeatedly decodes operands and dispatches memory reads for this loop. Removing those repetitions helps Xbox 360's PowerPC interpreter without changing rendering core count.

## Implementation and correctness conditions

- Only GA2 initialization opts in, on Xbox or an explicit host test build. The recognized form is `TEST1 #0..15, disp16[register]; BNE8 -6`. It uses the current register, displacement and instruction bytes; there is no fixed program counter or polling address.
- The normal TEST1 executes first. The shortcut is considered only when its nonzero bit result takes the backward branch. Its flags and operand decoder state have therefore already been updated normally.
- Both instructions must fit in one mapped code page; READ and FETCH backing must agree. The complete four-byte operand must be directly mapped within one page of the driver's permitted main RAM range, `0x200000..0x20ffff`.
- GA2 schedules V60, V25, Z80 and timers serially between execution slices. That RAM has no external writer during a V60 slice; the recognized instructions themselves do not write memory or change base registers.
- Pending IRQ, end-run request, subpage mask, unmapped memory, a page crossing or any different instruction form retains the interpreter path.
- Each additional backward-branch/TEST1 pair consumes the same 16 cycles as the current interpreter. Only complete pairs are removed. Partial pairs execute normally, preserving final cycle overrun, PC, previous PC, flags and decoder state at every slice boundary. Timer updates and other CPU runs occur at the same boundaries.
- Configuration is disabled in V60/V70 initialization and exit, and persists across a same-game reset/load. It is driver configuration, not additional serialized state.

## Validation

1. `python3 libretro/FBNeo/tests/v60_wait_loop/test.py`: PASS with ASan/UBSan. Tests cycle budgets from -8 through 4,095, final PC/previous-PC equivalence, IRQ/end-run rejection, mapping differences, instruction changes, page/range limits and all sixteen accepted bit immediates.
2. Native GA2 replay from the same preexisting state, 6,000 frames with identical scripted input: all per-frame video/audio hashes and the final serialized state matched the baseline. This covers a gameplay segment, game-over and attract scenes, not a full playthrough.
3. Big-endian PowerPC builds under `qemu-ppc`, 1,200 frames from reset: all per-frame video/audio hashes, final state and final raw frame matched. This is a PowerPC correctness check, not an Xbox SDK build or console performance measurement.
4. Sequential 3,000-frame host runs without callback hashing, baseline/candidate/candidate/baseline: 2.5235 / 2.1254 / 2.1258 / 2.5613 ms per frame. Mean frame-time reduction approximately 16.4%. This does not establish Xbox FPS or guarantee 60 FPS.

Raw profiling, test executables, replays and ROM inputs are outside Git, under `/home/humor/salvia-tests/ga2/idle/` and `hotspots/`. No production logging was added.

## Delivery and build status

At the user's request, the Xbox build was **not run**. The four changed production source files were mirrored and hash-checked against sibling `../Salvia`.

Windows source root: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia`.

Existing `Distro360/fbneo.xex` remains the prior approximately 36 FPS build, SHA256 `5db606137ae145792257771f20c5c962abba882536acbe9fe6750e3876a52f17`. There is no new XEX checksum for this source-only change.

## Rollback

Run `git revert <commit containing this document>`, then mirror these paths to sibling `../Salvia`:

- `libretro/FBNeo/src/cpu/v60/v60.cpp`
- `libretro/FBNeo/src/cpu/v60/op12.c`
- `libretro/FBNeo/src/cpu/v60_intf.h`
- `libretro/FBNeo/src/burn/drv/sega/d_segas32.cpp`

Pre-change source copies and hashes are outside Git at `.work/ga2-idle-20261003/`. A subsequent Xbox build and actual console acceptance are still required; do not describe the existing XEX as containing this change.
