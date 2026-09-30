# EPIC12 tiled atlas upload tests

The fixed-format uploader transforms a cached 128×128, 32-bit source page into
a slot of a 2048-wide tiled atlas. It visits the destination sequentially within
32×32 blocks, using aligned volatile vector stores suitable for Xbox 360
write-combined memory. Source pages may have unaligned cached addresses.

```sh
python3 tests/epic12_gpu_tile/run.py \
  --toolchain-root /path/to/ppc/root --output /path/to/results
```

The host sanitizer and big-endian PowerPC runs check every atlas slot at heights
1024 and 2048, the real 8192-pixel source pitch and compact/odd pitches, all four
word alignments, unchanged source data, preserved prior pages, and guard bytes.
Each simulated vector store additionally verifies destination alignment, page
ownership, and strictly increasing writes within a tile. An independent full-
surface address formula supplies the expected pixel locations.

`epic12_gpu_tile_selftest()` checks native vector instructions once with small
private allocations. The backend's GPU pixel self-test separately validates
actual write-combined texture uploads and sampling, allowing a linear atlas
fallback. The helper leaves texture synchronization and cache maintenance to
the existing `LockRect`/`UnlockRect` calls.
