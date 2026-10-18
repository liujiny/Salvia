# CV1000: Native __mftb Hardware Timebase Inlining and 16-Word Snapshot Unrolling

Baseline `cdad080a`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Analysis of Hardware Profiling under Heavy Stress (9,630 Frames)**:
   - Real console execution of `build=cd2bab8e` showed outstanding overall progress:
     - Average total frame time dropped from **18.028 ms (55.5 FPS)** to **15.452 ms (64.7 FPS)**.
     - `draw_sync` wait time dropped from **6.095 ms** to **3.092 ms** (a 49.3% reduction).
     - Slow frames exceeding 25 ms dropped to **only 3 frames out of 9,630 frames** (0.03%).
   - However, in the remaining 3 peak spike frames (e.g. frame 4808 during intense bullet storms), `cpu_io` still reached 26.513 ms.

2. **Probe Kernel Function Call Overhead in DRC Inner Loops**:
   - `core_phase_ms` showed `dispatch_calls=20,231,923` across 9,630 frames (~2,100 calls per frame).
   - In diagnostic builds, `Sh3Run_timerhack_impl` measures `drc_dispatch` by wrapping each dispatch call with:
     ```cpp
     const unsigned long long dispatchStart = salvia_cv1k_tick();
     ...
     const unsigned long long dispatchEnd = salvia_cv1k_tick();
     ```
   - `salvia_cv1k_tick()` was previously an out-of-line extern function in `epic12.cpp` calling the Win32/XTL kernel export `QueryPerformanceCounter(&now)`.
   - On Xbox 360, this meant executing **4,200 to 10,000 kernel API calls per frame**, involving register spilling, stack frame allocation, DLL call indirection, pointer dereferencing, and return value validation right inside the highest-frequency SH3 CPU dispatch loop.
   - On PowerPC Xenon, the hardware timebase register can be read directly in **1 single instruction** via the MSVC intrinsic `__mftb()` (which operates at 50 MHz, identical to `QueryPerformanceFrequency`).

3. **Loop Branch Overhead for 32-Word Snapshots**:
   - Hardware profiling reveals that 32 halfwords (64 bytes, the maximum compiler block capacity `MAX_INSNS = 32`) is the single most frequent snapshot bucket (45,457 entries).
   - Comparing 32 halfwords with 8-halfword iterations required 4 loop branch evaluations per block dispatch. On Xenon's 24-stage in-order pipeline, unrolling to 16 halfwords (32 bytes) cuts loop branching by 50% with four 64-bit ALU XOR operations per iteration.

## What

1. **`libretro/FBNeo/src/burn/devices/salvia_cv1k_probe.h` & `epic12.cpp`**:
   - Replaced the extern `QueryPerformanceCounter` call in `salvia_cv1k_tick()` with an inlined `__mftb()` compiler intrinsic on Xbox 360:
     ```cpp
     #if defined(_XBOX)
     extern "C" unsigned __int64 __mftb(void);
     #pragma intrinsic(__mftb)
     static inline unsigned long long salvia_cv1k_tick(void) {
         return (unsigned long long)__mftb();
     }
     #elif defined(__powerpc__) || defined(__ppc__) || defined(__PPC__)
     static inline unsigned long long salvia_cv1k_tick(void) {
         return (unsigned long long)__builtin_ppc_get_timebase();
     }
     #endif
     ```
   - Completely eliminates all kernel function call overhead, stack pushing, and pointer memory writes across 4,200+ calls per frame.

2. **`libretro/FBNeo/src/cpu/sh4/sh3_drc_source_check.h`**:
   - Added an unrolled 16-halfword (32-byte) loop before the 8-halfword loop:
     ```cpp
     while (words >= 16) {
         const uint64_t diff = (sh3_load64_unaligned(expected) ^ sh3_load64_unaligned(actual))
                             | (sh3_load64_unaligned(expected + 4) ^ sh3_load64_unaligned(actual + 4))
                             | (sh3_load64_unaligned(expected + 8) ^ sh3_load64_unaligned(actual + 8))
                             | (sh3_load64_unaligned(expected + 12) ^ sh3_load64_unaligned(actual + 12));
         if (diff) return false;
         expected += 16; actual += 16; words -= 16;
     }
     ```
   - Bypasses intermediate branches for 32-halfword blocks and checks 32 bytes at a time in straight-line 64-bit registers.

## Verification

1. **Host Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** in both `-O3` and `-fsanitize=address,undefined` modes (442,729 test cases, zero errors).

2. **Build Identity**:
   - Commit: `f7039ef2`
   - Archive Image: `fbneo-20261018-2035-f7039ef2-diag.xex`
   - Image SHA256: `6bb7ca93658183f00167cbc7e7d1af0c4dbe144306dc6d237cf3cf7c420c49a0`

