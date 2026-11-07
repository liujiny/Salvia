# EPIC12 opaque staging columns

The collector admits at least four consecutive neutral opaque two-pixel columns
whose destinations advance one pixel at a time. Every destination is fully
clipped and all source bounds are disjoint from the combined destination.
The row-major copy preserves the overlapping writes within each row.

`test.cpp` compares 512 generated source/destination layouts against the original
column-major order, including guard pixels, arbitrary pen bits and varying
source coordinates. It checks flags, tint, clipping, source/destination overlap,
VRAM wrapping, command RAM bounds and the 128-command limit.

Run native, sanitized or PPC execution:

```sh
python3 libretro/FBNeo/tests/epic12_staging_columns/run.py
python3 libretro/FBNeo/tests/epic12_staging_columns/run.py --sanitize
python3 libretro/FBNeo/tests/epic12_staging_columns/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root
```

The full `tests/epic12_gpu/run.py` suite also tests integration against original
sprite rasterizers, exact staging page-version increments, consumed command
count and guest pixel delay. These are correctness checks, not Xenon timings.
