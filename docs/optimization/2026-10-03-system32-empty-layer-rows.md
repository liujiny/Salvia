# System 32: remove known empty layers before mixing pixels

Source baseline: `36ff170d200f9b63584fe88c6497d19c785b5e7e` (GA2 read-only wait-loop optimization). That baseline is also source-only; the existing XEX is still the earlier `6d7c53df` build.

## Change

The mixer previously visited a layer for every pixel even when `get_layer_scanline()` had already identified its entire row as transparent and returned the shared zero row. The worker now builds a compact priority list that omits those text/background/bitmap layer rows before entering the pixel loop. It rebuilds only when the set of nonempty rows changes.

Sprite entries and the opaque background terminator are always retained. Their order, sprite priority grouping, shadow processing, palette lookup and blending equations remain identical. The background terminates searches; entries behind it are never examined by the original pixel loop and are not needed in the compact list.

The list and previous-row mask are local to each worker invocation. There is no shared mutable cache or extra thread synchronization. This removes repeated loads and branches in Xbox 360's pixel loop and also works with one rendering core. The common mixer is used by other System 32 games, so this is not a GA2-only CPU shortcut.

## Validation

- `python3 libretro/FBNeo/tests/system32_empty_layers/test.py`: PASS with ASan/UBSan. Compares the actual changed mixer with a test snapshot of the old loop across 2,000 randomized combinations of row transparency, blend masks/factors, shadows, RGB offsets, palette shifts, sprite groups, horizontal/vertical flips and byte order. Whole-frame and three-way split row calls produce identical pixels.
- GA2 6,000-frame scripted replay with one core, two cores and live 1/2/3-core switching: every frame's video/audio hashes and final serialized state match the previous source baseline. This is not a full-game run.
- Big-endian PowerPC under `qemu-ppc`, 600 additional frames starting from the previous 1,200-frame state: all video/audio hashes, final state and final raw frame match. This checks PowerPC correctness, not Xbox performance.
- Sequential two-worker host benchmarks, 3,000 frames each, baseline/candidate/candidate/baseline: 1.8432 / 1.8119 / 1.8127 / 1.9462 ms per frame. The candidate is faster in these samples, but baseline variation is substantial relative to the gain. No exact console improvement or 60 FPS claim is made.

The test oracle is the pre-change pixel loop, retained only for differential tests. Raw replay files, ROMs, host binaries and scripts are outside Git at `/home/humor/salvia-tests/ga2/empty-layers/`.

## Delivery

The changed driver was mirrored and hash-checked to `E:\Baiduyundownload\salvia-toolchain\.work\Salvia`.

The user requested source changes without an Xbox build. No new XEX was generated. Existing `Distro360/fbneo.xex` SHA256 remains `5db606137ae145792257771f20c5c962abba882536acbe9fe6750e3876a52f17`. Neither this change nor the preceding wait-loop optimization is present in that executable yet.

## Rollback

`git revert <commit containing this document>`, then mirror `libretro/FBNeo/src/burn/drv/sega/d_segas32.cpp` to sibling `../Salvia`. This keeps the preceding GA2 wait-loop optimization. Pre-change driver and source hashes are outside Git at `.work/ga2-empty-layers-20261003/`.

A later Xbox build and real-console comparison in the same GA2 scenes are required before accepting the performance result.
