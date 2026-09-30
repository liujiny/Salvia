# EPIC12 fixed-alpha blend tests

Run with the same 32-bit big-endian PowerPC compiler and QEMU used by the SH3
backend tests:

```sh
python3 tests/epic12_blit/run.py --toolchain-root /path/to/toolchain/root --output /tmp/epic12-test
```

The test checks every tint/alpha/source-component combination (65,536 cases),
one million packed RGB saturated additions, and 5,000 complete image operations
against the original EPIC12 functions. Image cases cover both axes of flipping,
transparent and opaque modes, all alpha/tint ranges, clipping, source wrapping,
overlapping source/destination VRAM and the accumulated emulated blitter delay.

No ROMs are required. Synthetic device state and the original renderer are local
to this executable; unused emulator dependencies are removed by the linker.
`tests/sh3_ppc/tchar.h` supplies the minimal portability definitions.

The separate full-game regression uses private ROMs and checks video, audio and
complete final state. QEMU timings do not measure Xbox 360 frame rates.
