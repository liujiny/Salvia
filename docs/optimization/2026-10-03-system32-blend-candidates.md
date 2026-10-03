# System 32: skip second-pixel searches that cannot contribute

Baseline: `36cdb493075fe9a011b3d28cf244aa71a86702c0`.

## Evidence and change

The current GA2 host sample still spends a substantial part of its time in
pixel mixing. In one 1,000-frame window, 33,296,619 first pixels carried a
blend mask, causing 66,215,294 lower-layer visits, yet none actually blended.
The mask alone does not establish that a visible, eligible lower pixel exists.

When constructing each worker's compact row order, compute whether any lower
non-sprite layer can blend. This flag uses the existing padding byte: the
layer record remains 10 bytes. Clear the local blend mask only if no eligible
lower layer and no lower sprite remain. When the actual sprite pixel is
`0xffff` and no lower non-sprite blend target exists, skip the second search
and use the existing RGB path. This does not change the emulated registers.

A lower sprite can affect shadow even when its group cannot blend. Such
searches are retained. In particular, `0x7fff` is not treated like `0xffff`:
its shadow bit may still darken a background. Palette offsets, color clamps,
real blending, priority and worker partitioning retain their original rules.

Across the same 3,000-frame replay, second-layer visits drop from
**81,119,766 to 1,451,571 (98.2%)**. Actual blends remain **1,451,571**.
This measures this specific search, not all rendering work. First-layer
searches, emulated CPU work and visible effects are not omitted.

An initial static-only guard passed correctness checks but produced no useful
whole-replay speedup (baseline 2.4765/2.5039 vs candidate 2.4641/2.5749 ms).
The retained implementation additionally checks the actual absent sprite.
All profiling lives in host test copies, with no new console logging.

## Validation

Commands were run through `rtk proxy`:

- `python3 libretro/FBNeo/tests/system32_empty_layers/test.py`: PASS with
  ASan/UBSan. Added targeted blend eligibility and lower-sprite guards plus
  2,000 differential cases against the original mixer. Whole/split rows,
  byte order, palette offsets, shadows, flips and sprite groups are covered.
  Entire rows of `0xffff` and `0x7fff` test the different shadow semantics.
- Host GA2 replay: 6,000 frames, live 1/2/3-worker switching. Every frame's
  video/audio hashes and final saved state match the baseline. Hash-file SHA256:
  `34b42788d71703bc17d9b3b8946f0d87263e5eb7dd33d964790fc202d52fa5c6`;
  state SHA256:
  `f956cf6a2b5af163e3d9893c0b54db1e0c3330e99cd9e3e1f4f2fb2e7eb67ca3`.
- Big-endian PowerPC/QEMU: 1,200 frames from reset, matching frame/audio
  hashes, final state and raw frame. State SHA256:
  `5c33ee1179595aa6bd87720e46d7e855343638328e593028fe5929aa639790e7`.
- Sequential host A/B/B/A, 3,000 frames per run, two workers:
  baseline **2.8591 / 2.9273 ms/frame**, candidate **2.7125 / 2.8641 ms/frame**.
  Mean time is about **3.6% lower**, with substantial host variation. This is
  not an Xbox measurement or a prediction of console FPS.

Raw evidence and experimental binaries are outside Git at
`/home/humor/salvia-tests/ga2/mixer-next/`. This is a shared System 32 renderer
optimization; full coverage of all games/scenes still needs console testing.

## Artifact and rollback

No Xbox/XEX build was performed. The existing `Distro360/fbneo.xex` remains
at source checkpoint `6d7c53dffa9b6029f9ae1653e7e9042b3e34fc6b`, SHA256
`5db606137ae145792257771f20c5c962abba882536acbe9fe6750e3876a52f17`.
The earlier V60, V25, Z80, empty-row and unscaled-sprite changes, plus this
change, are still source-only and do not establish 60 FPS.

Backup: sibling `../ga2-blend-candidates-20261003/`.
Rollback: revert the commit containing this document, then mirror the reverted
`libretro/FBNeo/src/burn/drv/sega/d_segas32.cpp` to `../Salvia`. The XEX changes
only after a separately authorized build. Nothing was pushed to GitHub.
