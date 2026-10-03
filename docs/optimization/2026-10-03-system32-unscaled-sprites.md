# System 32: direct horizontal 1:1 sprite drawing

Baseline: `df3d4ba3fca2a1d3ddc0fbc5a4a9f742b4293c9e`.

## Reason and change

In the 2,000-frame GA2 host sample, 35,531 of 43,603 sprite calls had exactly
1:1 horizontal scaling. They accounted for 233,221,208 of 274,193,942 requested
destination pixels (85.1%). This area estimate includes clipped pixels; it
is not a measurement of visible pixels or GPU work.

The normal renderer runs a zoom accumulator loop for every decoded pixel.
When `hzoom == 0x10000`, it always consumes exactly one source pixel per
destination pixel. Explicit 4bpp/8bpp paths now remove that accumulator loop
and its repeated additions, comparisons and subtractions.

Both paths use the original pixel macros, clipping, palette indirection,
shadow handling, flip direction and word-end transparency markers. Vertical
scaling and source-address advancement are unchanged. Other horizontal
scales keep the original loop. The shared System 32 renderer benefits; the
two preceding CPU waiting optimizations opt in only for GA2.

This trades additional renderer instructions in the binary for fewer executed
instructions on ordinary unscaled sprites. Xbox instruction-cache and actual
frame-rate effects still require a console test.

## Validation

Commands run through `rtk proxy`:

- `python3 libretro/FBNeo/tests/system32_unscaled_sprites/test.py`: PASS under
  ASan/UBSan. **3,000** cases compare the actual function with the saved original:
  2,006 unscaled and 994 scaled; 870 modify visible pixels. Tests vary byte
  order, 4bpp/8bpp, flip, indirect palette, shadow, inclusive/exclusive clipping,
  source ROM/RAM and sprite end markers. Full destination buffers and return
  values match, including untouched transparent pixels.
- GA2 replay, 6,000 frames from the same state and inputs, two rendering workers:
  every frame's video/audio hash and the final saved state match the baseline.
  Hash-file SHA256:
  `34b42788d71703bc17d9b3b8946f0d87263e5eb7dd33d964790fc202d52fa5c6`;
  saved-state SHA256:
  `f956cf6a2b5af163e3d9893c0b54db1e0c3330e99cd9e3e1f4f2fb2e7eb67ca3`.
- Big-endian PowerPC execution under QEMU, 1,200 frames from reset: matching
  per-frame video/audio, final state and raw frame. State SHA256:
  `5c33ee1179595aa6bd87720e46d7e855343638328e593028fe5929aa639790e7`.
- Sequential 3,000-frame host A/B/B/A timings, two rendering workers:
  baseline **2.9993 / 2.8905 ms/frame**, candidate **2.6206 / 2.9565 ms/frame**.
  The mean is about 5.3% lower, but variation is substantial: this is not a
  reliable prediction of the console gain. Do not add percentages from the
  separate optimization runs or translate them into an Xbox FPS estimate.
- `git diff --check -- libretro/FBNeo/src/burn/drv/sega/d_segas32.cpp`: clean.

The submitted ROM in `../Salvia/Distro360/ga2.zip` is byte-identical to the
earlier test input. Raw samples, states and binaries are kept outside Git in
`/home/humor/salvia-tests/ga2/sprite-unity/` and `z80-poll/`.

## Current source-only checkpoints

- [V60 read-only wait loop](2026-10-03-ga2-readonly-wait-loop.md)
- [Remove empty layer rows from mixing](2026-10-03-system32-empty-layer-rows.md)
- [V25 self-jump batching](2026-10-03-ga2-v25-self-jump.md)
- [Z80 sound status polling](2026-10-03-ga2-z80-status-poll.md)
- This unscaled sprite path.

All still await an Xbox build and actual console acceptance. These tests do
not establish 60 FPS or cover every game scene.

## Artifact and rollback

**No Xbox/XEX build**, as requested. The existing `Distro360/fbneo.xex` still
corresponds to `6d7c53dffa9b6029f9ae1653e7e9042b3e34fc6b`, SHA256
`5db606137ae145792257771f20c5c962abba882536acbe9fe6750e3876a52f17`.

Baseline source/identity backup: sibling `../ga2-sprite-unity-20261003/`.
Rollback: revert this document's commit and mirror the reverted
`libretro/FBNeo/src/burn/drv/sega/d_segas32.cpp` into `../Salvia`. Rebuild only
when authorized; reverting Git cannot update the runtime copy or XEX itself.
