# CV1000: Fix Staging Buffer WAR Hazard & Expand GPU Command Buffer to 4096

Baseline `815e0cae`. Iteration diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **User Observation & Reported Issues**:
   - The user reported: *"boss出场没有不透明方块了，但机身只显示了一半。/Usb0/Salvia-360/data/states/FinalBurn Neo/mmpork.state2 这个进度是离boss出场最近的进度（但还没有出场）。另外boss死亡后的巨量精灵（一模一样的）仍然会造成帧数骤降至20帧"* (Boss 1 appears without opaque blocks now, but only half the fuselage is displayed. Also, the massive burst of identical sprites/medals after boss death still causes framerate to drop sharply to 20 FPS).

2. **Root Cause Analysis 1: Off-screen Staging Buffer Write-After-Read (WAR) Hazard**:
   - CV1000 hardware uses off-screen VRAM coordinates ($x \ge 768$) as dynamic scratchpad RAM to composite complex, multi-part boss sprites (fuselage sections, rotating propellers, turrets) before blitting the assembled composite onto the visible display buffer ($x < 768$).
   - When Boss 1 enters the screen:
     1. The game software rasterizes the main fuselage parts into off-screen scratchpad VRAM at $(x \in [768, 898], y \in [0, 195])$.
     2. It issues a blit command taking source rectangle $(sx \in [768, 898], sy \in [0, 195])$ and blitting to screen destination $(x = 559, y = 153)$.
     3. Because this destination is on-screen ($x < 768$), it is enqueued into `epic12_gpu_commands[0]`. The GPU marks the source pages in `epic12_gpu_source_pages[0]`.
     4. Before this GPU batch is flushed or rendered, the game CPU immediately begins assembling the *next* boss component (the propeller) in the *exact same* off-screen staging coordinates starting at $(x = 768, y = 0)$!
     5. Because the destination is off-screen ($x \ge 768$), it bypasses GPU queueing and executes as a CPU software blit into `m_bitmaps`.
     6. However, previously, the off-screen bypass handler ONLY checked for Read-After-Write (RAW) hazards (whether CPU reads from a page written by queued GPU commands). It completely lacked a **Write-After-Read (WAR)** hazard check!
     7. Consequently, the CPU blitter immediately overwrote `m_bitmaps` at $x = 768$ with propeller graphics while the GPU queue was still waiting to read the fuselage from that same texture memory!
     8. When the GPU batch was subsequently flushed, the left half of the fuselage at $x = 768$ had already been overwritten by the propeller data, obliterating the left fuselage and cutting the boss in half!
   - Adding a Write-After-Read (WAR) hazard check ensures that whenever a CPU blit writes to any page tracked in `epic12_gpu_source_pages[0]`, the active GPU batch is immediately flushed (`EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY`) before the CPU writes to VRAM, guaranteeing that the GPU samples the correct, pristine fuselage graphics.

3. **Root Cause Analysis 2: Command Buffer Limit (1024) Thrashing During Boss Death (20 FPS Drops)**:
   - When Boss 1 is destroyed, the game generates an enormous burst of hundreds of identical pig medals and particle sprites in a single frame.
   - In `epic12_gpu.h` and `epic12_gpu_xbox.h`, the command queues `epic12_gpu_commands` and `epic12_xenos_cropped` were statically sized to `1024` entries.
   - When sprite count in a frame exceeded 1024, `epic12_gpu_can_fit()` returned `1`, triggering mid-frame GPU flushes.
   - Each mid-frame flush on Xbox 360 forces an EDRAM resolve, Direct3D `LockRect`, and CPU untile copy back to system memory.
   - During the explosion, the game was triggering 3 to 4 mid-frame flushes per frame, collapsing performance to ~20 FPS.
   - Expanding queue capacity from 1024 to 4096 entries requires only 224 KB of static memory (Xbox 360 has 512 MB unified memory) and completely eliminates mid-frame command limit flushes during dense bursts.

## What

1. **`libretro/FBNeo/src/burn/devices/epic12_gpu.h`**:
   - Expanded `epic12_gpu_commands[EPIC12_GPU_BINS][4096]`.
   - Updated capacity check in `epic12_gpu_can_fit` to 4096.
   - Added WAR hazard detection in `epic12_gpu_submit` for `x >= 768`:
     ```cpp
     // 2. WAR hazard: does this CPU blit write to a page that queued GPU commands sample from?
     if (epic12_gpu_count[0]) {
         const int dpy0 = y >> 7, dpy1 = (y + h - 1) >> 7;
         const int dpx0 = x >> 7, dpx1 = (x + w - 1) >> 7;
         bool war_overlap = false;
         for (int py = dpy0; py <= dpy1 && !war_overlap; ++py) {
             const int row = py << 6;
             for (int px = dpx0; px <= dpx1; ++px) {
                 if (epic12_gpu_source_pages[0][row | px]) { war_overlap = true; break; }
             }
         }
         if (war_overlap) {
             epic12_gpu_flush_bin(0, EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY);
         }
     }
     ```

2. **`libretro/FBNeo/src/burn/devices/epic12_gpu_xbox.h`**:
   - Expanded `epic12_xenos_cropped[4096]`.

## Verification & Results

1. **Host PPC Test Suites**:
   - `libretro/FBNeo/tests/epic12_gpu/run.py`: **100% PASS** (all 14,400 commands, 10.4M component shader cases bit-exact).
   - `libretro/FBNeo/tests/epic12_blit/run.py`: **100% PASS** (all 65,536 rounding cases and 5,000 image cases).

2. **Xenia Multi-Frame Verification (`mmpork.state3`, 130 frames)**:
   - Frame 0 (initial): 100.00% match.
   - Frame 60 (propellers): 98.82% match, zero holes, exact alignment.
   - Frame 120 (fuselage): Restored from 85.65% to **98.80%** match. The entire missing left half of the boss fuselage is 100% rendered and intact.
   - Command limit flushes: **0** during medal burst.
