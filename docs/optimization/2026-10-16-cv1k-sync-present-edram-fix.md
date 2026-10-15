# CV1000: Fix Flickering Dark Mask and Horizontal Tearing via Synchronous Present and EDRAM Isolation

Baseline `ecaf0a64`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

During Cave CV1000 emulation (*Muchi Muchi Pork!*, *Ibara*), a prominent horizontal split tear and flickering dark mask ("遮罩") appeared across the upper 50% of the display. Crucially:
1. When gameplay was paused, the dark mask and horizontal line disappeared immediately.
2. The mask returned as soon as gameplay resumed.
3. The boundary shifted slightly between ~48% and ~52% of the screen height.

### Root Cause Analysis

1. **Mid-Scanout GPU Collisions from Async Swap**:
   - `libSDLx360` used `XBOX_AsyncPresentFrame`, which enabled Direct3D 9 swap mode (`IDirect3DDevice9_SetSwapMode(D3D_Device, TRUE)`) and scheduled front-buffer presentation asynchronously with `IDirect3DDevice9_Swap()`.
   - While the display DAC scanned out the frame buffer, the background blitter thread (`thready`) concurrently rendered the next frame's Cave CV1000 GPU batches (`epic12_gpu_render` / `epic12_xenos_run`), issuing Direct3D draw calls and EDRAM resolves (`D3DRESOLVE_RENDERTARGET0`).
   - The GPU command processor actively resolved render targets to memory while scanout was in flight, causing scanline tearing across the center of the display.

2. **EDRAM Base Collision**:
   - In `epic12_gpu_xbox.h`, the offscreen render target was created with `surface.Base = 0`, sharing EDRAM Base 0 with the display backbuffer and scanout structures.

3. **30Hz CV1000 Alpha Interleaving**:
   - Cave CV1000 games alternate shadow/transparency intensity on consecutive frames (30Hz flickering).
   - Because of the mid-frame scanline tear, the top half of the display was showing Frame $N$ while the bottom half was showing Frame $N-1$.
   - This phase offset trapped the top half in an alternating dark shadow state, making it look permanently covered by a dark, flickering mask.
   - On pause, emulation stopped, GPU command execution ceased, and the DAC scanned out a single complete frame, causing the mask to vanish instantly.

4. **Build Pipeline Decoupling**:
   - `build-xex.sh` only rebuilt `libretro.lib` and `Salvia.vcxproj`.
   - `libSDLx360.lib` is a separate Visual Studio project (`libs/libSDLx360/libSDLx360.vcxproj`) and was never rebuilt by `build-xex.sh`, leaving `Salvia.vcxproj` statically linked to an outdated binary.

## What

1. **Synchronous VBlank Presentation in `libs/libSDLx360/SDL/src/video/xbox/SDL_xboxvideo.c`**:
   - Replaced `XBOX_AsyncPresentFrame` in `XBOX_Present(void)` with direct `IDirect3DDevice9_Present(D3D_Device, NULL, NULL, NULL, NULL)`.
   - Synchronous `Present` flips cleanly during the vertical blank interval (`D3DPRESENT_INTERVAL_ONE`), preventing tearing and race conditions between scanout and offscreen rendering.

2. **EDRAM Base Isolation in `libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h`**:
   - Moved the offscreen render target to `surface.Base = 1024`, completely isolating it above the 720p frontbuffer EDRAM space (tiles 0..719).
   - Disabled async presentation request via `SDL_XBOX_SetCoreGpuAsync(0)`.

3. **Rebuilt `libSDLx360.lib`**:
   - Rebuilt `libs/libSDLx360/libSDLx360.vcxproj` via `msbuild.cmd` into `libs/libSDLx360/Release/libSDLx360.lib`.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component cases, and alpha/snapshot/reorder integration tests).
   - `libretro/FBNeo/tests/epic12_blit/run.py`: **100% PASS** (all 65,536 tint/alpha rounding cases and 5,000 image test cases).

2. **Xenia Canary Live Frame Analysis**:
   - Captured 10 consecutive frames (`sync_present_frame_0.png` through `sync_present_frame_9.png`) during intensive gameplay.
   - Row-by-row luminance ratio step detector verified that the sharp mid-screen step discontinuity dropped to 0.
   - Visual inspection verified that the upper half and lower half match in lighting and transparency, with zero horizontal tearing or masking.
   - Automated 300-frame live detector confirmed: `CV1000 DETECT_SUMMARY: checked 300 frames, detected=0`.

3. **Performance (300 frames `mmpork`, Xenia Canary)**:
   - Main thread presentation time: `present=0.254 ms`.
   - Full 60 FPS performance maintained without regression.

4. **Artifacts**:
   - Diagnostics XEX: `Distro360/fbneo.xex`
     - SHA256: `b086cc5eae159d7eb8c45daf995d76e25c66b9c786c20cb88ed33a9abeb00276`
   - Archive: `salvia-tests/xex-archive/fbneo-20261016-0606-ecaf0a64-diag.xex`
