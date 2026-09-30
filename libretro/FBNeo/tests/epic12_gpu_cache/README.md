# EPIC12 source-atlas cache tests

`epic12_gpu_cache.h` retains source pages between GPU batches. It pins all
resident pages needed by a batch before allocating misses, chooses empty slots
first, then replaces the least recently used unpinned page. CPU writes and GPU
output invalidate intersecting pages. The native atlas can use 256 slots or
fall back to 128 slots without changing the per-batch 128-page limit.

```sh
python3 tests/epic12_gpu_cache/run.py \
  --toolchain-root /path/to/ppc/root --output /path/to/results
```

Tests cover page and VRAM edges, empty-slot preference, all-needed pinning,
full atlases, repeated allocation, reset, randomized invalidation and replacement
at capacities 1 through 256, and 32-bit clock wrapping. The independent reference
uses a 64-bit access serial. Both the sanitized host and big-endian PowerPC
runs must produce the same decisions. Pixel sampling from persistent cached
copies is covered by the separate `epic12_gpu` test suite.
