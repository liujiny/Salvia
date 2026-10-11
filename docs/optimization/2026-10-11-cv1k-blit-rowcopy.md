# CV1000: copy the batch box a row at a time, with the next row in flight

Baseline `e8406c35`. The console numbers are `Distro360/cv1000-gpu.log`, the
same `mmpork` session `2026-10-11-cv1k-blit-threshold.md` and the census read,
and the transfer it changes is the one the census named as the largest term
that is still a plain scalar copy.

## Why

Steady state, 10,616 sampled batches, 9.6 batches per frame:

| term | ms/batch | ms/frame | state |
| --- | --- | --- | --- |
| `untile_mask` (result texture -> vram) | 0.448 | 4.30 | VMX, tiled order |
| `input_upload` (vram -> input texture) | 0.415 | 3.98 | scalar `memcpy` per row |
| `source_upload` (atlas page swizzle) | 0.355 | 3.40 | VMX |
| replay | | 3.03 | `epic12_blend_fixed` is 98.3% of it |
| command walk and construction | | 6.85 | unmeasured inside the job |

`input_upload` moves 928,246,059 pixels in 11,860 batches -- 78,267 pixels, or
about 313 KB, per batch, 3.0 MB per frame -- and costs 3.98 ms for it. That is
6.0 MB of traffic a frame (read plus write) in 3.98 ms.

The copy was one `memcpy()` per row:

```c
for (int y=0;y<height;++y)
    memcpy((BYTE*)lr.pBits+y*lr.Pitch,vram+(bounds.min_y+y)*8192+bounds.min_x,width*4);
```

Source rows are 32 KB apart, destination rows are the driver's texture pitch
apart, and no row is longer than 2 KB. A `memcpy()` handed one row cannot know
where the next row is, so the hardware prefetcher never sees the line that the
copy will need one row later, and each call re-derives a byte loop sized for a
single long run. The 3.98 ms is what that costs.

## What

`epic12_gpu_region.h` adds `epic12_gpu_copy_rows()` and the input upload calls
it once per batch. Nothing else about the batch changes.

- Both pitches are multiples of 16, so the alignment of every row is the same
  and the split into head, 16-byte body and tail is decided once, outside the
  row loop. The head aligns the **load**, which is the pair the readback path
  already uses: `__lvx` plus either `__stvx` when the two rows share an
  alignment or `__storeunalignedvector` when they do not.
- Each row asks for the row four rows ahead with `__dcbt`, one hint per 128
  bytes of width. `dcbt` cannot fault and cannot change a value, so the hint
  cannot alter the frame; it only tells the prefetcher about a line the same
  copy will read a few microseconds later.
- A pitch that is not a multiple of 16 would move the alignment from row to row,
  so the split cannot be hoisted. That case falls back to a byte loop. No caller
  has one.
- The rectangle is exact in both directions: bytes of a destination row beyond
  `width` are never read or written, and nothing is rounded out to a vector or
  a cache line.

The linear (untiled) atlas upload and the tiled readback are untouched; the
readback is a different shape and already VMX.

## Verification

`tests/epic12_gpu_region` is byte-exact in both directions and runs on the
little-endian host and on the big-endian PowerPC build:

```
python3 tests/epic12_gpu_region/run.py --toolchain-root <ppc-root> --output <out>
```

Both runs: `PASS: 4044 rectangles; 10112328 exact bytes; 274216 aligned /
325704 unaligned vector stores; 60800 prefetch hints, all inside the rectangle`.
Every byte of the destination allocation is compared against the contract, so a
write outside `[0,width)` of a row fails the test rather than being absorbed.
That matters here: an earlier draft copied the alignment head without checking
it against `width`, so a row shorter than the head wrote up to 15 bytes past the
end of the row. That case is now one of the 4,044. The prefetch loop runs
through a stub that fails on any hint outside the rectangle, and the two
production rectangles (2048x512 and 1024x240 of the same shape as the batch
box) are included.

The other five suites are unchanged and still pass: `tests/sh3_ppc` (846,026
cases), `tests/epic12_gpu`, `tests/epic12_blit`, `tests/epic12_gpu_untile`
(6,820 rectangles), `tests/epic12_gpu_cache` (fingerprint `21d3a8b4487bb152`).
Artifacts in `/home/humor/salvia-tests/cv1k-blit-census-20261011/`.

The 60/600/1800-frame `STATE` hashes of the host harness (`0cf251c3d512ddbb`,
`baabc4def027a8a3`, `dff81882efc1b24a`) still hold, but they are not evidence
about this change: the new code is under `_XBOX`, and the host harness builds
with `EPIC12_GPU_TEST`, so the host compiles the mock compositor and never
reaches a texture lock. The host cannot measure or even execute this path. The
compile of the console image is what proves the wiring, and the console is the
judge of the timing.

## Console protocol

The candidate image is `blit-rowcopy c57ccde8 20261011-1506 diag`, 34,635,776
bytes, sha256 `552211323e1355330cbcaf125254bff6c19a69a3d2ae4ca177a4f7e864b88eeb`,
deployed as `Distro360/fbneo-diag.xex` and `Distro360/fbneo.xex` and archived at
`salvia-tests/xex-archive/fbneo-20261011-1506-blit-rowcopy-diag.xex`. The base
is `Distro360/fbneo-blit-crossover-diag.xex`
(`b5dd53ba`, sha256 `429a5c246452f821da220fa845cf6b823749d62b24ad6d5d7377b5631ae7b4eb`),
which is the image `Distro360/cv1000-gpu.log` came from.

One image, one session, no input, the same state as the census, pausing three or
four times against the image the census came from (`fbneo-blit-crossover-diag`,
`b5dd53ba`). Pair the windows by the frame number the report prints.

- `batch_timing ... input_upload=<us>` is the number this change moves.
  `input_pixels` should be unchanged: the same bytes are copied, only faster.
- `upload_bytes` should be unchanged for the same reason.
- If `input_upload` does not fall, this candidate is dead and should be
  reverted: the row copy is not where the 3.98 ms is.
- `worker_busy` and `worker_wait` are the acceptance numbers; a frame-rate
  change that does not move them is noise.

## Revert

Delete `epic12_gpu_region.h` and `tests/epic12_gpu_region/`; restore the
per-row `memcpy()` loop in `epic12_gpu_xbox.h` and drop the include.
