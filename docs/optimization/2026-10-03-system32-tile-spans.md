# System 32: contiguous cached tile-row reads

Baseline: `045e01aafb7f605a441cda870594c34fee46d530`.

## Reason and implementation

After the CPU wait-loop and sprite changes, background generation remains a
significant serial stage. In the current host replay it costs 0.21-0.69 ms per
frame across 1,000-frame windows; cached tile rebuilding itself costs at most
0.005 ms. Most of this stage repeatedly samples already-built tile pixels.

The old rowscroll loop selects one of two 512-pixel pages and masks the source
coordinate for every output pixel. The zoom path repeats that calculation even
at exactly 1:1 horizontal scale.

The new helper splits each visible span at page boundaries. Inner loops then
read consecutive source pixels in a fixed forward/backward direction, with
opaque/transparent mode selected outside the loop. This removes repeated page
selection and per-pixel coordinate masking. It adds no workers, synchronization,
persistent cache or memory allocations, and requires no new CPU intrinsics.

Rowscroll uses this for both directions. Zoom uses it only for exact steps
`0x100000` and `0xfff00000` (positive/negative 1:1 in 20-bit fixed point).
Other scales retain the original loop. Fractional starts, vertical scaling,
row selection, clip extents, two-page wrap, transparent counts and byte order
keep their existing behavior. The helper never reads beyond a source page.

## Validation

Commands were run through `rtk proxy`:

- `python3 libretro/FBNeo/tests/system32_tile_spans/test.py`: PASS under
  ASan/UBSan with sanitizer recovery disabled. Compares the actual helper and
  full zoom/rowscroll functions against the saved original loops:
  - 4,000 independent spans, including zero length, multiple wraps, both
    directions, opaque pen zero and different source/destination offsets;
  - 600 unity-zoom and 1,200 scaled-zoom cases;
  - 1,800 rowscroll cases;
  - full output buffers and transparency flags match, including clipped-out
    rows, wholly transparent source pages, endian conversion and flips.
  Cache bitmaps and clip extents are fixtures; their unchanged generation is
  not reimplemented as part of this test.
- GA2 host replay: 6,000 frames with live 1/2/3-worker switching, matching
  every video/audio hash and the final state. Hash-file SHA256:
  `34b42788d71703bc17d9b3b8946f0d87263e5eb7dd33d964790fc202d52fa5c6`;
  state SHA256:
  `f956cf6a2b5af163e3d9893c0b54db1e0c3330e99cd9e3e1f4f2fb2e7eb67ca3`.
- Big-endian PowerPC/QEMU: 1,200 frames from reset, identical video/audio,
  final state and raw frame compared with the blend-candidate baseline.
  State SHA256:
  `5c33ee1179595aa6bd87720e46d7e855343638328e593028fe5929aa639790e7`.
- Sequential host A/B/B/A, 3,000 frames per run, two workers:
  baseline **3.0993 / 3.1112 ms/frame**;
  candidate **2.5648 / 2.4745 ms/frame**.
  Means: **3.10525 -> 2.51965 ms/frame**, approximately **18.9% lower**.
  This is the whole measured core frame, not just the tile loop. Different
  host/compiler results cannot be converted into Xbox FPS or added to prior
  rounds' percentages.
- The PowerPC GCC object builds successfully. Its specialized zoom/rowscroll
  functions grow from 0x9d8/0x9d0 to 0xbac/0xbf8 bytes; specialization trades
  some code size for simpler inner loops. This is not an XDK build or a
  measurement of Xenon instruction-cache performance.

Evidence is outside Git at `/home/humor/salvia-tests/ga2/mixer-next/`, including
`tile-ppc/`. No ROM, runtime log, object, binary or saved state is committed.
The GA2 replay and randomized fixtures do not cover every System 32 title or
all game scenes. Console validation is still required.

## Artifact and rollback

No Xbox/XEX build was performed, following the current instruction. Existing
`Distro360/fbneo.xex` SHA256 remains:
`5db606137ae145792257771f20c5c962abba882536acbe9fe6750e3876a52f17`.
It still corresponds to `6d7c53dffa9b6029f9ae1653e7e9042b3e34fc6b`; the later
source optimizations have not yet been delivered in an XEX. 60 FPS has not
been established on the console.

Canonical and runtime driver sources are synchronized. Backup for this step:
`../ga2-tile-spans-20261003/`. To roll back, revert the commit containing this
document and mirror the reverted driver into `../Salvia`; rebuild only after
build authorization. This step and the preceding
[blend-candidate optimization](2026-10-03-system32-blend-candidates.md) have
separate commits. Nothing was pushed to GitHub.
