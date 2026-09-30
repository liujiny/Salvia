# EPIC12 GPU batching regression

Run without ROMs or an Xbox SDK:

```sh
python3 tests/epic12_gpu/run.py --toolchain-root /path/to/ppc/toolchain --output /tmp/epic12-gpu
```

The runner builds a big-endian PowerPC executable and runs it under qemu-ppc.
Without `--toolchain-root`, it uses `PPC_CXX` / `QEMU_PPC` or tools on PATH.

- 6,291,456 source/tint/alpha/destination cases compare the floating shader model
  for both unified and specialized fast/feedback shaders against exact integer
  reference formulas, including destination byte 255 after additive saturation.
  Another 4,096 plain cases verify that alpha and destination do not affect them.
- 14,400 commands compare GPU batching with the existing CPU renderer, including
  the entire VRAM image and emulated blitter delay, alternating specialized
  attributes, unified attributes fallback, and uniform compatibility GPU paths.
- Inputs exercise both flips, transparent pixels, clipping, source wrap,
  framebuffer dependencies, in-place overlap, uploads, runtime disabling and
  injected backend failure.
- A 260-page sequence verifies the restored 128-page batch cap: 3 batches with either a 128-slot or 256-slot source cache. The 256-page batch experiment was retired after the user's regression report.
- 640 further commands exercise merged uniform groups, actual snapshot reuse,
  overlap and area limits, mode-4 alpha normalization and vertex-buffer capacity.
- Another 640 commands vary tint, alpha, mode and transparency in each group.
  All three shader paths must match the CPU. Attributes must reduce submissions
  (640 to 42 in this synthetic case), and splitting shaders must keep draw counts
  identical to the unified attribute path.
- The mock samples the generated integer RECTLIST atlas coordinates and decodes
  the packed per-vertex attributes (or the group's uniform parameters on the
  compatibility path). Feedback reads use the actual shared snapshot, so it
  checks the native planner and geometry too. The specialized fast path receives a
  poison destination texture value, while hardware additive blending receives the
  live target separately; feedback shaders receive the actual snapshot.

`epic12_gpu_mock.h` supplies a host backend for ordering tests. Defining
`EPIC12_GPU_ORDER_TEST` uses integer equivalents for longer whole-game runs;
leave it undefined for the shader-arithmetic unit test. The default host renderer
uses the specialized attribute path, including in whole-game ordering runs. This mock does not test
the Xenos API, physical texture cache, GPU timing or console frame rate. The
native backend performs a separate GPU-vs-CPU pixel self-test once on the console.

## Persistent atlas cache

The mock retains actual copies of uploaded 128x128 source pages between batches.
Texture sampling reads those copies, never current VRAM; CPU uploads, GPU target
writes and state resets must invalidate tags before stale data can be reused.
It shares the native LRU helper and reports `CACHE` hits, uploads, invalidated pages
and resets for whole-game runs. A metadata-only shadow of the former 128-slot
replacement policy follows the same writes/resets and reports `legacy128_uploads`,
so comparisons include real source invalidations. The default capacity is 256; define
`EPIC12_GPU_TEST_CACHE_CAPACITY=128` when compiling a whole-game harness to test
the allocation fallback. Per-batch source capacity is capped at 128 in both modes; the 256-slot cache still provides cross-batch residency benefits.

Unit tests alternate both capacities through randomized commands and all shader
paths. A 144-page working set repeated three times must upload 432 pages at capacity
128 and only 144 at capacity 256, exercising the upper atlas rows. Explicit CPU
and GPU writes to resident source pages, plus resets, must force fresh uploads
and match the existing CPU renderer.

## Source alpha cropping, command ordering and feedback snapshot reuse

Uploaded source pages also retain alpha metadata. The default model crops
transparent borders, drops fully transparent commands, and reads back only the
remaining destination union, while keeping original command timing and input
pixel statistics. Opaque draws are unchanged even when source alpha is clear.
`ALPHA` reports kept commands, actual raster rectangle pixels, original readback
area and reused snapshots. All pixels are still sampled from cached texture bytes.

An independent 32-command lookahead can share a feedback snapshot across draw
types without changing the original draw groups. Every prior write must be
disjoint from future feedback destinations; only complete groups may be covered,
and the merged rectangle cannot exceed 1.25 times the separate resolve areas.

Integration tests cover 128/256 capacities, three shader paths, all three optimization
fallback flags, four flips, crops across source-page boundaries, RGB-nonzero
transparent pixels, CPU/GPU alpha changes, fully empty batches, single visible
pixels, and intervening plain/additive writes. Explicit planner checks cover
partial future groups at the lookahead boundary and sparse resolve area limits.

## Independent command ordering

The attribute renderer additionally uses a bounded eight-command lookahead to
bring matching plain/additive/feedback draw types together. A command may pass
another only when their destination rectangles do not intersect. All overlapping
pairs keep their original order, and all source reads still use the immutable
atlas uploaded before the batch. Commands retain every field; cropping happens
first, then ordering, then group and snapshot planning. Original queued commands
and blitter timing are unchanged. Uniform compatibility leaves ordering disabled.
`REORDER` reports batches with a move and total moved commands.

Planner tests check the exact lookahead boundary, touching versus overlapping
edges, every overlapping pair's order through random lists up to 1,024 commands,
complete command permutation, untouched separate inputs, and identical in-place
output. Random full-VRAM tests vary ordering alongside alpha/snapshot fallbacks.
Cache/alpha integration compares all eight optimization-flag combinations against
the original CPU renderer at capacities 128 and 256 on all three shader paths;
uniform compatibility must never invoke the ordering path.
