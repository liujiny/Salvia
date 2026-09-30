# CV1000 tiled readback test

The transfer helper targets one Xenos format: a 512×512 tiled A8R8G8B8 texture.
It traverses 32×32 blocks and combines a four-pixel copy with the EPIC12 color
mask. It keeps CPU/GPU synchronization in the existing read-only texture lock.

Run with the extracted Debian PowerPC toolchain used by the other FBNeo tests:

```sh
python3 tests/epic12_gpu_untile/run.py \
  --toolchain-root /path/to/ppc/root --output /path/to/results
```

The test checks all 262,144 tiled addresses against an independent full-surface
formula, every width and height from 0 to 512, vector and tile boundaries,
different destination alignments and row pitches, the real 8192-pixel VRAM
pitch, and untouched guard pixels. Both the little-endian sanitized host run
and the big-endian PowerPC run exercise the vector branch. Their partial-store
helper models the Xbox VMX128 store pair because ordinary PowerPC emulators
do not implement those Xbox instructions.

The Xbox build additionally exposes `epic12_gpu_untile_selftest()`. It fills a
tiled buffer using the SDK address API and exercises the real VMX instructions
once, including unaligned and partial rectangles. Its boolean result lets the
backend retain its previous transfer when validation or allocation fails.
No test timing is a measurement of Xbox performance.
