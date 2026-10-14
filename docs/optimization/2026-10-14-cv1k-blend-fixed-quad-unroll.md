# CV1000: quad-unroll transparent span testing and all-opaque batch processing in epic12_blend_fixed

Baseline `a668a637`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

In Cave CV1000 emulation on Xbox 360, CPU software blitting (`soft` + `replay`) accounts for ~9.6 ms/frame of blitter worker time (`worker_busy` ~25.5 ms/frame):
- `soft` (GPU-refused draw commands): ~5.6 ms/frame (107,937 calls / 1274 frames).
- `replay` (batches < 8192 px routed to CPU): ~4.0 ms/frame (274,278 batches / 1274 frames).
- Together, over 98.27% of CPU pixels execute `epic12_blend_fixed()` (`fixed_f0_tr1` path).

Sprites in CV1000 bullet-hell titles (e.g. `mmpork`, `espgal2`, `futari`, `ddpdfk`) are composed of rectangular bounding boxes containing:
1. Wide margins of transparent pixels (`bit 29 == 0`) surrounding irregular sprite shapes.
2. Dense solid clusters of opaque pixels (`bit 29 == 1`) in sprite interiors.

Previously, `epic12_blend_fixed()` processed pixels dual-unrolled (2 pixels at a time). For transparent regions, it performed `if (!((pen0 | pen1) & 0x20000000))` advancing 2 pixels per branch. On the Xenon in-order PowerPC core (3.2 GHz, 24-cycle branch misprediction penalty), processing transparent sprite margins and solid interiors 2 pixels at a time still suffered significant branch and loop-stepping overhead.

## What

In [`epic12_fast_blit.h`](file:///home/humor/src/Salvia/libretro/FBNeo/src/burn/devices/epic12_fast_blit.h):
1. **Quad-unrolled transparent span skipping (`Transparent == true`)**:
   - Fetches 4 pixels at once (`pen0`, `pen1`, `pen2`, `pen3`) respecting `FlipX` orientation.
   - Evaluates `any_opaque = (pen0 | pen1 | pen2 | pen3) & 0x20000000`. If zero, advances `out += 4` and `in += FlipX ? -4 : 4` immediately with a single conditional branch, skipping 4 transparent pixels per iteration through sprite margins.
2. **All-opaque fast batching**:
   - When all 4 pixels are opaque (`((pen0 & pen1) & (pen2 & pen3)) & 0x20000000`), performs parallel color extraction, destination masking, and SWAR saturated additions (`epic12_packed_add`) for all 4 pixels with zero internal branches.
   - This allows the compiler to schedule load and ALU instructions across all 4 pixels, maximizing instruction-level parallelism and eliminating load-use stalls on Xenon's dual-issue in-order pipeline.
3. **Branch-folded sub-pair fallback for mixed boundaries**:
   - When only some of the 4 pixels are opaque (sprite edge transitions), evaluates sub-pairs (pen0, pen1) and (pen2, pen3) using the branch-folded pair logic.
4. **Quad-unrolled non-transparent path (`Transparent == false`)**:
   - Evaluates 4 contiguous pixels per iteration, pipelining 4 saturated pixel adds without conditional branches.
5. **Tail handling**:
   - Unrolled 2-pixel loop for remaining pairs, followed by a 1-pixel scalar step for odd remainders, guaranteeing bit-for-bit exactness regardless of width or alignment.

## Verification

- **PPC Blit Test Suite** (`libretro/FBNeo/tests/epic12_blit/run.py`):
  - `PASS 65536 tint/alpha rounding cases and 1000000 packed colour sums`
  - `PASS 5000 image cases: tint, transparency, clipping, flips, source wrap, overlapping VRAM and delay` (100% bit-exact).
- **PPC GPU Test Suite** (`libretro/FBNeo/tests/epic12_gpu/run.py`):
  - `PASS 6291456 component cases on unified and fast/feedback shaders, including additive saturation`
  - `PASS 14400 commands, full VRAM and delay comparison`
  - `PASS cache 128/256 slots, reordering, snapshot lookahead, and alpha clipping`.
