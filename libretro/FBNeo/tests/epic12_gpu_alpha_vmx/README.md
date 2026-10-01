# EPIC12 VMX alpha-mask tests

Run from the repository root:

```sh
rtk proxy python3 libretro/FBNeo/tests/epic12_gpu_alpha_vmx/run.py --toolchain-root /path/to/extracted/ppc/root --output /path/outside/repository
```

The host run uses ASan/UBSan and a numeric big-endian model of the SDK intrinsics. The PPC run executes real AltiVec loads, byte permutes, rotates and element stores under QEMU's `g4` CPU. QEMU's default CPU does not provide the required vector instructions. Neither run executes Xbox VMX128 extensions or the Xenos driver; Windows XDK disassembly and the startup hardware self-test cover those separately.

`test.cpp` checks every alpha-bit position and unrelated bit, random input, all word-store lanes, guarded input/output bounds, VRAM pitch, unaligned scalar fallback, group summaries and cache invalidation. It injects a faulty vector store and verifies that the startup test disables VMX and the scalar builder remains correct.

`integration.cpp` runs the existing full alpha/crop suite with the vector builder enabled, including exact bounds, pixel equality, cross-page sprites, flips, FIFO query-cache collisions and empty batches. Printed host/QEMU timings are not console FPS measurements.
