# Fused EPIC12 tile and alpha upload

Run `python3 run.py --toolchain-root /path/to/ppc/root --output /path/to/evidence`.

The host run uses ASan/UBSan and numeric big-endian vector semantics. The PPC run executes real AltiVec loads, permutes, rotates and stores under QEMU g4. Neither measures Xbox performance or executes Xenos.

Checks cover every 128/256-slot atlas position, three aligned source pitches including VRAM pitch 8192, pixel-for-pixel tiled output against an independent full-surface formula, scalar alpha masks/groups, bbox cache invalidation, source immutability, destination canaries, sequential aligned writes, one source load pass, unaligned/disabled rejection before writes, and startup-test failure injection. The established alpha-vector suite separately covers the shared gather helper and crop integration.
