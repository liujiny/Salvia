# EPIC12 cached alpha metadata

`epic12_gpu_alpha.h` stores the EPIC12 transparency bit (0x20000000) as four
32-bit masks per row of a 128 x 128 source page. Four OR summaries per eight
rows let queries skip most per-row work. Each page also caches 16 exact rectangle
queries in four FIFO sets. One page uses 2304 bytes of alpha metadata plus 128
bytes of query cache; 256 atlas slots use 608 KiB. Source pixels are read only
while the page is uploaded.

## Integration

- Bind one `Epic12GpuAlphaPage` to each physical atlas cache slot.
- Build every new/replaced slot using `build(source, pitchWords)` or call
  `build_row(y, sourceRow)` in order for y=0..127 immediately after row upload.
- Every `build_row` call invalidates the page's query cache in constant time.
  The next query lazily clears its tags. The row builder still requires a full
  y=0..127 sequence; its OR summaries do not support arbitrary single-row
  replacement followed by a whole-group query.
- Resident valid cache tags reuse that metadata. Invalidation only needs to
  invalidate the cache tag; a replacement upload rebuilds all metadata.
- First upload every source page needed by the original commands. Then call
  `epic12_gpu_alpha_crop_batch(input, count, output, alpha, slots, capacity, bounds)`.
- Return values: a nonnegative surviving command count, or -1 for invalid
  geometry/missing source slots. On error use the original commands/fallback;
  output may be partially written. `bounds` changes only for a successful
  nonempty result. A zero count needs no GPU transfer or rendering.
- Preserve the original emulated blitter delay and conservative cache
  invalidation bounds. Cropping changes only the submitted host work.
- The single-command `epic12_gpu_alpha_trim` returns 1 to keep, 0 for entirely
  transparent, or -1 on error. It changes the command only when returning 1.
  Nontransparent commands are always returned unchanged.

Source rectangles already include emulated clipping. Both source and destination
are adjusted together, including all flip combinations. Command order is preserved;
feedback dependencies must be planned from the cropped commands and new bounds.

## Tests

```sh
python3 libretro/FBNeo/tests/epic12_gpu_alpha/run.py --output /tmp/alpha-host
python3 libretro/FBNeo/tests/epic12_gpu_alpha/run.py --sanitize --output /tmp/alpha-asan
python3 libretro/FBNeo/tests/epic12_gpu_alpha/run.py --toolchain-root /path/to/ppc/root --output /tmp/alpha-ppc
```

Tests independently scan original pixels to check 12,000 command bounds and
selected full output buffers. Patterns cover transparent nonzero RGB, opaque,
sparse, rectangular and word/group/page-edge pixels. All flips, cross-page
sources, already-clipped dimensions, VRAM limits, metadata rebuild, padded pitch,
empty batches, destination union, ordering, in-place compression and invalid
slots/overflow guards are covered. Query-cache tests cover repeated nonempty and
empty answers, output preservation, any-row/full-build invalidation, hash
collisions, FIFO eviction, and the maximum packed key.

`EPIC12_GPU_ALPHA_PROFILE` enables counters for query pages, source pixels used
for metadata construction, summary/row words read, trim calls, empty commands,
and `cacheHits`/`cacheMisses`.
It is disabled in production. The synthetic query benchmark reports CPU time
and metadata reads; host or QEMU time is not Xbox frame-rate evidence.

## Optional gameplay trace replay

```sh
python3 libretro/FBNeo/tests/epic12_gpu_alpha/run.py --trace /private/bbox-queries.bin --output /tmp/alpha-trace
```

`trace.cpp` replays exact page metadata and query rectangles, comparing every
cached answer with the uncached scan. It accepts big-endian 32-bit `(key, slot)`
pairs. A key of `0xffffffff` is followed by 576 big-endian words containing the
page's 128 x 4 row masks and 16 x 4 group masks. Other keys pack `x0, y0, x1, y1`
in four seven-bit fields. Slots must be 0..255 and rebuilt before being queried.
Private gameplay traces and ROM-derived data stay outside the repository.

A private 600-frame `ddpsdoj` segment produced 702 page builds and 397,829 queries.
The selected four-way FIFO cache hit 90.018% of queries and reduced row/summary
reads from 4,424,182 to 489,107 (88.94%). Replay matched every uncached bbox; the
instrumented trace capture retained the baseline video/audio and save-state
hashes. A 16-way LRU alternative hit 93.98%,
but required 6.48 key comparisons and 5.48 entry moves per query; the selected
cache used 2.46 comparisons and 0.30 moves. These are workload counts, not Xbox
frame-rate measurements.
