# GA2: inline mapped V60/V70 instruction reads on Xbox

Baseline: `b8fad0241a47d67fcab8e2760920b60d2d2d645b` (clean tracked worktree).
Console feedback at this baseline: GA2 approximately 35 FPS with two or three rendering cores. The System 32 target is 60 Hz, confirmed by FBNeo frame scheduling/libretro timing and MAME's `sega_system32_base` screen configuration. Rendering workers do not execute the V60, V25 or Z80 CPUs concurrently.

## Reason and correction to the previous investigation

`v60mem.c` uses `cpu_readop*` only when `LSB_FIRST && !ALIGN_INTS`. Xbox is big endian, so the executing opcode path instead calls `v60.info.mr8/mr16/mr32`. The dedicated byte-read opcode helpers introduced by `2ce7629a` were therefore inactive in Xbox opcode fetching. Its change to `program_read_word_16le` was active. The earlier host opcode-alignment counters described the desktop direct-fetch path, not measured Xbox alignment exceptions.

This change adds inline mapped instruction reads to the actual Xbox branch. A mapped, within-page operand is assembled from little-endian bytes after one address-mask operation and one page lookup. It removes the indirect bus call and repeated lookup/split accesses for odd instruction operands.

The fast path deliberately uses the existing Xbox READ map (`mem[0]`), preserving prior semantics even where READ and FETCH maps differ. Cross-page operands, subpage address masks and unmapped reads use the original bus function with the original address. Handler widths, order and side effects are unchanged. The map is consulted on every read; no cached code snapshot or invalidation scheme is introduced. CPU cycle counts, IRQ handling, sound scheduling and pixel/color processing are unchanged. Other platforms retain the existing branch unless explicitly built with the host test define.

## Verification

- `python3 libretro/FBNeo/tests/v60_mapped_fetch/test.py`: PASS with ASan/UBSan. Extracts the actual production functions; compares both V60 and V70 buses over all 8,192 addresses, sixteen page-map combinations, three address masks and three operand widths. Tests distinct READ/FETCH backing pages, separately allocated/noncontiguous pages, unmapped byte/word/long callback order, wrapping and subpage masks.
- Native libretro GA2 baseline/candidate, 6,000 frames from the same state with scripted inputs: every video/audio hash and final serialized state matched. Baseline compiled with `ALIGN_INTS` to exercise the Xbox-style generic bus dispatch; both use the previously verified single-core mixer and byte-read memory helper. Candidate additionally defines `FBNEO_V60_MAPPED_FETCH_TEST`.
- Static big-endian PowerPC GA2 builds executed under `qemu-ppc`, 300 frames from reset with identical controls: every video/audio hash, final state and final raw frame matched. This exercises the big-endian dispatch; it is not Xbox SDK or full-game execution.
- Two consecutive 3,000-frame host benchmarks per variant, no callback hashing, order baseline/candidate/candidate/baseline: core milliseconds per frame 2.9152 / 2.6656 / 2.8127 / 2.8991. Mean reduction approximately 5.8%. These are host timings and do not predict console FPS.
- Replay/test scripts and raw evidence live outside Git at `/home/humor/salvia-tests/ga2/fetch/`. No ROM, state, SDK or generated binary is committed.

## Xbox artifact

Xbox SDK build: PASS (exit 0), FBNeo Release library followed by Salvia `Release_finalburn`; no other emulator core was rebuilt. XEX2 magic verified; size 34611200 bytes.

XEX SHA256: `5db606137ae145792257771f20c5c962abba882536acbe9fe6750e3876a52f17`.

Source hashes were verified between Git and sibling build source before and after compilation. Build evidence and rollback copy: `E:\Baiduyundownload\salvia-toolchain\.work\ga2-fetch-20261003\`. Delivery: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.

The production build retains the stable no-diagnostics behavior; no new gameplay logging was added.

## Rollback

`git revert <commit containing this document>`, mirror `libretro/FBNeo/src/cpu/v60/v60mem.c` to sibling `../Salvia`, then rebuild FBNeo and the Release_finalburn executable. Alternatively restore the separately verified pre-change XEX at `E:\Baiduyundownload\salvia-toolchain\.work\ga2-fetch-20261003\fbneo-before.xex` (SHA256 `bf1eeb679e542b69e8b7206160e56698f6d2ed19d042d42532af5c389fca231a`). Reverting Git alone does not replace the deployed XEX.

## Console acceptance

Compare the same enemy-heavy GA2 scenes with the prior approximately 35 FPS observation, using the same shader/scaling and two rendering cores. Verify startup, controls, sound, save/load and exit. Keep three cores as a separate comparison. Stable 60 FPS is unverified until console testing.
