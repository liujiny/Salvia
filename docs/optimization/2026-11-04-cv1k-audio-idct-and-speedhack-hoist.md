# CV1000: YMZ770 MPEG-2 Audio IDCT32 Acceleration and Idle RAM Speedhack Guard Hoisting

Baseline `3ab4f607` (restored Phase 1 stable baseline). Implementation commit `638f80dc`.

## Why

1. **Cave CV1000 Frame Time Breakdown & Audio Subsystem Contention**:
   - Console profiling reveals that across active gameplay frames, the CPU thread spends ~11.0 ms in `cpu_io`.
   - In `d_cv1k.cpp:839`, `ymz770_update` runs synchronously inside `DrvFrame` on the main CPU thread at the end of each frame, decoding MPEG-2 Layer II streams for up to 16 audio channels.
   - For every decoded audio block, `mpeg_audio::idct32` is called 36 times per channel ($36 \times 2 = 72$ times for stereo).
   - In upstream FBNeo, `mpeg_audio::idct32` was implemented as a nested $32 \times 32$ loop executing 1,024 double-precision floating-point multiplications and additions per call. Crucially:
     - The outer loop was index `i` (output sample 0..31) and the inner loop was `j` (frequency subband 0..31).
     - Each channel decoder maintained its own duplicate 8 KiB $32 \times 32$ cosine table (`m_cos_cache`). Across 16 channels, this consumed 128 KiB of data cache, thrashing L1/L2 caches.
     - Accesses to `m_cos_cache[i][j]` had strided memory steps of 32 doubles (256 bytes) across iterations.
     - Crucially, in MPEG Layer II audio, 50% to 75% of higher-frequency subbands (`input[j]`) are completely zero (`0.0`). The existing loop computed all 1,024 multiply-adds unconditionally, multiplying zeros by cosine constants and adding them up.

2. **Lessons from `ibara360` (`runtime/ymz770.c`)**:
   - In `ibara360`, `idct32` inverts the loop order:
     - Outer loop iterates over subbands `j` (0..31).
     - If `input[j] == 0.0`, the entire 32-element inner loop is **skipped entirely**!
     - When `input[j] != 0.0`, the inner loop streams over `s[i] += in * c[i]`, where `c = cos_transposed[j]` is a contiguous 32-double array in memory.
     - On PowerPC Xenon's in-order pipeline, this eliminates cache thrashing, skips 50%-75% of all floating-point math, and vectors into straight-line contiguous loads and fused multiply-adds (`fmadd`).
     - Mathematical output is **100% bit-for-bit identical** (zero error) because terms are accumulated in the exact same `j` order for each output sample `i`.

3. **CV1000 Idle RAM Speedhack Guard Hoisting (`d_cv1k.cpp`)**:
   - In `speedhack_read_long(offset)`, the handler previously called `UINT32 pc = Sh3GetPC(-1)` on **every single 32-bit read** inside the 64 KiB region `0x0c000000..0x0c00ffff`.
   - In CV1000 games, this 64 KiB page contains high-traffic working RAM, stack frames, and engine globals. Calling `Sh3GetPC(-1)` (which reads global delay state and masks address bits) on every non-idle read generated hundreds of thousands of useless calculations per second.
   - By hoisting `if (offset == hacky_idle_ram)` ahead of `Sh3GetPC(-1)`, all non-idle memory reads return immediately from RAM, reserving PC inspection solely for the genuine idle watch address.

## What

1. **`libretro/FBNeo/src/burn/snd/mpeg_audio.cpp`**:
   - Added static cache-friendly transposed cosine table `s_cos_transposed[32][32]` shared across all 16 `mpeg_audio` channel decoders, reducing active cache footprint from 128 KiB to 8 KiB.
   - Initialized `s_cos_transposed` automatically in the constructor, `scan()`, and `idct32()` fallback path.
   - Inverted `idct32()` loops to outer `j` (0..31) and inner `i` (0..31), with 4-way loop unrolling:
     ```cpp
     double s[32] = { 0.0 };
     for (int j = 0; j < 32; j++) {
         const double in = input[j];
         if (in == 0.0)
             continue;
         const double *c = s_cos_transposed[j];
         for (int i = 0; i < 32; i += 4) {
             s[i + 0] += in * c[i + 0];
             s[i + 1] += in * c[i + 1];
             s[i + 2] += in * c[i + 2];
             s[i + 3] += in * c[i + 3];
         }
     }
     for (int i = 0; i < 32; i++)
         output[i] = s[i];
     ```
   - Preserved `m_cos_cache[32][32]` member in `mpeg_audio` and `SCAN_VAR(m_cos_cache)` in `scan()` for 100% savestate compatibility.

2. **`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`**:
   - In `speedhack_read_long(offset)`:
     ```cpp
     static UINT32 __fastcall speedhack_read_long(UINT32 offset)
     {
         if ( offset == hacky_idle_ram ) {
             UINT32 pc = Sh3GetPC(-1);
             if (pc == hacky_idle_pc || pc == hacky_idle_pc+2) {
                 INT32 rem = Sh3GetRemainingCycles();
                 Sh3BurnCycles(rem > 0 ? rem : speedhack_burn);
             }
         }
         UINT32 V = *((UINT32 *)(DrvMainRAM + (offset & 0xfffffc)));
     #ifdef LSB_FIRST
         V = (V << 16) | (V >> 16);
     #endif
         return V;
     }
     ```

3. **`libretro/FBNeo/tests/ymz770_idct/`**:
   - Added standalone unit and differential test suite (`test.cpp`, `run.py`).
   - Evaluates 100,000 randomized subband vectors, impulses, all zeros, and full-spectrum patterns against the baseline mathematical formula.

## Verification

1. **YMZ770 IDCT32 Test Suite**:
   - Host mode: `python3 libretro/FBNeo/tests/ymz770_idct/run.py` -> **PASS**: 100,000 subband vectors bit-identical (`max diff = 0.000000e+00`).
   - Sanitizer mode: `python3 libretro/FBNeo/tests/ymz770_idct/run.py --sanitize` -> **PASS** (ASan / UBSan 100% clean).
   - QEMU PowerPC cross-compilation: `python3 libretro/FBNeo/tests/ymz770_idct/run.py --toolchain-root ...` -> **PASS** (`max diff = 0.000000e+00`).
   - Host synthetic benchmark: **2.98x faster** than upstream loop.

2. **Host Regression Suites**:
   - `libretro/FBNeo/tests/frontend_state/run.py`: **100% PASS** (savestates, 151 MiB, CRC, worker shutdown).
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py`: **100% PASS** (8 host suites, 442,729 cases, ASan/UBSan clean).
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (6,291,456 component cases, 14,400 commands, VRAM identical).
   - `libretro/FBNeo/tests/sh3_ppc/run.py`: **100% PASS** (848,846 cases under QEMU PPC).

3. **XEX Binary Identity & Deployment**:
   - Baseline commit: `3ab4f607`
   - Implementation commit: `638f80dc`
   - Image set compiled with:
     `build-xex.sh --baseline 3ab4f607 --tag cv1k-audio-drc --flavor diag --force`
   - **Candidate Image (Diagnostic)**: `Distro360/fbneo.xex`
     - Tag: `cv1k-audio-drc 638f80dc 20261104-2005 diag`
     - Size: `34,832,384` bytes
     - SHA256: `9e55d28733282919361f82c16ccdd3e8721b9d0e8a21e37814f6ce96e84feeea`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261104-2005-cv1k-audio-drc-diag.xex`
   - Directly deployed to Xbox 360 Jasper at `192.168.2.232` (`/Usb0/Salvia-360/fbneo.xex`).
