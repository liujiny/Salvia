# CV1000: Fix In-Game Pause Crash and Recover 32MB Memory Headroom

Baseline `402f7d60` (cv1k-war-drc-arena).
Implementation commit: `cv1k-pause-crash-and-mem-safety`.

## 1. Problem Statement & Root Cause Analysis

During extensive gameplay of Cave CV1000 *Muchi Muchi Pork!* (`mmpork`) on Xbox 360 Jasper hardware, pausing the game after ~18,000 frames resulted in a fatal crash (`0xC0000005` Access Violation).

### 1.1 Root Cause 1: DRC Code Arena Over-Allocation (64MB)
- Commit `a34b3549` doubled the SH-3 PPC translation cache from 32MB to 64MB (`SH3_PPC_CACHE_BYTES = 64MB`).
- On retail-kernel Xbox 360 titles, the total available physical title RAM is ~448 MiB (out of 512 MiB unified GDDR3 RAM).
- For large games like `mmpork`, Flash ROM alone requires 148.4 MiB (`138,412,032` bytes). In addition to video buffers, direct sound buffers, D3D textures, frontend state, and the 64 MiB DRC arena, total committed title memory reached ~380–390 MiB, leaving CRT heap severely fragmented.
- Hardware profiling confirmed that the extra 32MB did not improve performance during dense bullet patterns, as the bottleneck was CPU emulation time (`cpu_io = 37.45 ms`), not DRC evictions.

### 1.2 Root Cause 2: NULL Pointer Dereference in `clonarPantalla`
- When entering the in-game pause menu (`src/menus/gamemenu.cpp:1747`), `GameMenu::clonarPantalla` is called to generate a dimmed screenshot background:
  - It creates `tmp` (1280x720x4 bytes = 3.68 MB).
  - It creates `tmp2` via `zoomSurface` (3.68 MB).
  - It creates `copia` via `SDL_ConvertSurface(tmp, ...)` (3.68 MB).
- Under near-OOM heap fragmentation, `SDL_ConvertSurface` failed and returned `NULL` (`copia == NULL`).
- Line 1910 unconditionally called:
  ```cpp
  SDL_SetAlpha(copia, SDL_RLEACCEL, 0xFF);
  ```
- Because `copia` was NULL, `SDL_SetAlpha` dereferenced `copia->flags` without validation, causing an immediate crash.

---

## 2. Changes Made

1. **`libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h`**:
   - Reverted `SH3_PPC_CACHE_BYTES` to `(32 * 1024 * 1024)` (32 MiB).
   - Reverted `SH3_PPC_SECTOR_BYTES` to `(1024 * 1024)` (1 MiB).
   - Instantly reclaims 32 MiB of physical title RAM, providing ample headroom for memory allocations.

2. **`src/menus/gamemenu.cpp`**:
   - Added `if (!src || !overlay) return NULL;` guard.
   - Added checks for allocation failures of `tmp`, `tmp2`, and `copia`.
   - Guarded `SDL_SetAlpha(copia, ...)` with `if (copia)`.
   - Handled graceful fallback: if surface allocation fails, `clonarPantalla` returns NULL, and the pause menu smoothly falls back to rendering a solid background without crashing.

---

## 3. Verification & Results

1. **Host & PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (all 8 host suites, ASan/UBSan clean, 442,729 test cases).
   - `libretro/FBNeo/tests/frontend_state/run.py`: **100% PASS** (streaming, 151 MiB, gzip CRC/truncation, worker ownership).
   - `libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root ...`: **100% PASS** (all 848,846 test cases: 750,178 compiled, 98,668 fallback).
   - `libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root ...`: **100% PASS** (all 17 suites, 14,400 commands, 10.5M component cases).

2. **Physical Memory Status**:
   - 32 MiB physical RAM reclaimed.
   - Pause menu surface allocation failure guarded against NULL dereference.

3. **Artifact Identity & Build**:
   - Build script: `/home/humor/salvia-tests/xex-build/build-xex.sh --tag cv1k-pause-crash-fix --baseline a34b3549 --flavor diag`
   - Archive Image: `/home/humor/salvia-tests/xex-archive/fbneo-20261106-2058-cv1k-pause-crash-fix-diag.xex`
   - Deployed Image: `Distro360/fbneo.xex`
   - Flavor: `diag` (`SALVIA_FBNEO_DIAGNOSTICS 1`)
   - Image Size: 34,832,384 bytes
   - SHA256: `209728324d43744cf2153ef88b16a75774a59517dae3d3a15faade8a2ff426b3`
