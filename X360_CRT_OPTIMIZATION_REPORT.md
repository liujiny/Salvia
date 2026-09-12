# Xbox 360 CRT Shader Optimization Report

## 1. Problem and scope

Complex CRT presets increase reported Xenon CPU usage even though their pixel
work runs on Xenos. The issue reproduces with the unmodified FBNeo core, so all
changes are confined to Salvia's common Xbox SDL/D3D9 backend and shader assets.
No libretro core, emulation timing, frameskip, audio, or input code was changed.

The first combined build regressed CRT Easymode to 96-100% CPU. Consequently
none of its three experimental changes is enabled by default. This revision is
an A/B isolation build, not a claim that the combined approach is beneficial.

User-supplied baseline (`batcir`, same CPS2 scene):

| Shader | CPU before |
|---|---:|
| Nearest | ~60% |
| Bilinear | ~60% |
| Scanlines | ~60% |
| CRT Geom | 70-77% |
| CRT Lottes | 83-96% |
| CRT Easymode | 83-96% |

Optimized real-hardware CPU results are pending. This report does not claim an
Xbox hardware measurement that was not performed locally.

## 2. P0: synchronization audit and diagnostics

The normal frame path contains no explicit `BlockUntilIdle`, fence loop,
`WaitForVBlank`, `GetRenderTargetData`, or CPU GPU-status polling. VSync is
implemented with `D3DPRESENT_INTERVAL_ONE`, not a frontend polling loop.

Two implicit synchronization hazards were found:

1. One permanently locked game texture was unlocked for draw, then the exact
   same texture was locked immediately after `Present` for the next CPU frame.
2. The overlay texture was unlocked, drawn after the CRT quad, and immediately
   relocked *before* `Present`. A resource lock at that point can serialize the
   CPU behind all preceding CRT work.

`XBOX_CRT_PERF_DIAG` is enabled in this final diagnostic build and uses the XDK-supported
`QueryPerformanceCounter/Frequency` pair. It accumulates in memory and emits one
Xbox debugger line every 300 flips. Release builds compile out all timer reads,
counters, formatting, and output.

Reported stages are the ones actually present: texture unlock, draw submission,
Present, next game-texture lock, overlay relock, inferred total GPU wait (the two
lock durations), and total flip. Upload/copy are reported as zero because the
current direct framebuffer writes into the locked texture; there is no backend
frame memcpy. The backend has no explicit BeginScene/EndScene or Resolve stage.

## 3. P1: submission ordering

With `XBOX_OVERLAY_POST_PRESENT_RELOCK=1`, overlay relock occurs after the
complete frame has been submitted through `Present`, rather than immediately
after its draw. The default `0` preserves the original order. No full GPU
synchronization was added.

Whether `Present` itself waits is now directly observable as `present_avg_us`.
Whether a resource hazard waits is observable separately in the two lock fields.

## 4. P2: three-texture game ring

The backend creates three CPU-cached linear game textures. The texture containing
the current completed framebuffer is unlocked and presented immediately. The
CPU then locks the next ring member for the following emulated frame. Thus CPU
Frame N+1 can write B while Xenos samples A from Frame N.

This does not display an old framebuffer intentionally: Frame N is still
submitted in Frame N's `SDL_Flip`. It removes the same-resource lock hazard but
does not add an explicit N-1 presentation FIFO. Resize and shutdown release all
members. VSync reset unlocks and relocks only the active CPU member.

`XBOX_GAME_TEXTURE_RING` defaults to `0`, which reduces the implementation to
the original single texture. Its effectiveness must be proven independently
from `game_lock_gpu_wait_avg_us` before it may become a release default.

## 5. CRT Easymode optimization

When the compile-time `XBOX_CRT_FAST=1` experiment is selected, the Xbox shader
compiler receives the matching HLSL define. It defaults to `0`; Windows and the
Xbox A baseline use the original code. The optional fast path:

- retains full precision for UV, texel coordinates, dimensions, floor/frac,
  VPOS and mask position;
- uses `half3` for sampled colors and accumulated output;
- replaces eleven input gamma `pow(x, 2.2)` operations with `x*x`;
- replaces output inverse gamma `pow(x, 1/2.2)` with `sqrt(x)`;
- replaces fixed `pow(abs(d), 2)` with `d*d`;
- uses CPU-precomputed inverse texture dimensions from `c1.zw`.

The 11 texture fetches are deliberately retained. Folding taps with bilinear
sampling would interpolate gamma-encoded colors before linearization and creates
a larger visual change; that experiment is deferred until hardware timing says
texture bandwidth, rather than ALU, remains dominant.

## 6. CRT Lottes optimization

Lottes receives the same precision-safe gamma, fixed-square and inverse-dimension
changes. Its warp, rounded corners, mask layout, scanline kernel, number of taps,
clamp addressing and runtime UI identity are preserved.

## 7. XDK shader compiler comparison

Measured locally with Xbox 360 SDK `fxc.exe` 2.0.21250.0, target `ps_3_0`:

| Shader | Variant | ALU estimate | Instructions | Texture cycles | Fetches | PsMaxReg |
|---|---|---:|---:|---:|---:|---:|
| CRT Lottes | original | 158.67 | 119 | 44 | 11 | 17 |
| CRT Lottes | X360 fast | 117.33 | 88 | 44 | 11 | 15 |
| CRT Easymode | original | 145.33 | 109 | 44 | 11 | 15 |
| CRT Easymode | X360 fast | 100.00 | 75 | 44 | 11 | 15 |

This is a 26% instruction reduction for Lottes and 31% for Easymode. Both exact
and fast variants compiled successfully. Instruction counts are compiler facts;
they are not substitutes for real-console frame timing.

## 8. CPU results

The original figures are listed in section 1. Optimized CPU average/min/max,
FPS, Present time, game lock time and overlay lock time require the prescribed
60-second `batcir` console A/B run. No fabricated optimized result is included.

## 9. Expected visual difference

Geometry, taps, beam weights and masks are unchanged. The Xbox fast gamma uses
2.0 instead of 2.2, so a small midtone/brightness difference is expected. Check
black level, saturated highlights, mask color, scanline thickness and scrolling.
Keep `XBOX_CRT_FAST=0` for the exact original shader and visual baseline.

## 10. Compatibility and latency

Nearest, Bilinear, Scanlines and other presets use the same selection pipeline.
The overlay is still composed every enabled frame. Rotation, aspect, integer
scale, overscan and VSync state handling are unchanged. The game texture bound
for draw is explicitly the just-completed ring member, including the MAME indexed
palette path. No emulator core was modified.

## 11. Modified files

- `libs/libSDLx360/SDL/src/video/xbox/SDL_xboxvideo.c`: diagnostics, submission
  ordering, texture ring, inverse-dimension constant, Xbox shader macro/cache ABI.
- `libs/libSDLx360/SDL/src/video/xbox/SDL_xboxvideo.h`: ring ownership state.
- `src/io/sync.cpp`: independent limiter sleep/spin diagnosis and optional
  low-CPU limiter experiment.
- `assets/shaders/06-crt-lottes.hlsl`: Xenos ALU/precision fast path.
- `assets/shaders/07-crt-easymode.hlsl`: Xenos ALU/precision fast path.
- `X360_CRT_OPTIMIZATION_REPORT.md`: this report.
- `X360_CRT_OPTIMIZATION.patch`: reproducible source diff.

## 12. Build and test controls

- `XBOX_CRT_PERF_DIAG=1` in the retained build, producing paired 300-frame
  `[X360GPU]` and `[X360LIMITER]` summaries.
- `XBOX_GAME_TEXTURE_RING=0` default; set `1` only for experiment C/E.
- `XBOX_CRT_FAST=0` default; set `1` only for experiment B/E.
- `XBOX_OVERLAY_POST_PRESENT_RELOCK=0` default; set `1` only for D/E.
- `XBOX_LOW_CPU_FRAME_LIMITER=1` in the retained build. It remains isolated
  from the disabled ring, shader-fast, and overlay-ordering experiments.

Required isolation matrix:

| Build | Texture ring | CRT fast | Overlay post-Present relock |
|---|---:|---:|---:|
| A original | 0 | 0 | 0 |
| B fast shader only | 0 | 1 | 0 |
| C texture ring only | 1 | 0 | 0 |
| D overlay ordering only | 0 | 0 | 1 |
| E combined | 1 | 1 | 1 |

The low-CPU limiter is a sixth, separate experiment. The original limiter has a
4 ms cliff: at 4 ms or less it performs no sleep and busy-spins the entire tail.
The optional limiter sleeps when more than 1.5 ms remains and reserves roughly
0.75 ms for the spin tail. Diagnostics measure actual sleep and spin wall time;
limiter spin is never included in the backend's GPU-wait fields.

Real-hardware result sheet (fill each row after a fixed 60-second scene):

| Build/effect | CPU% | FPS | FRAME_WORK_US | LIMITER_SPIN_US | PRESENT_US | GAME_LOCK_US | OVERLAY_LOCK_US |
|---|---:|---:|---:|---:|---:|---:|---:|
| A / Nearest | | | | | | | |
| A / CRT Geom | | | | | | | |
| A / CRT Lottes | | | | | | | |
| A / CRT Easymode | | | | | | | |
| B / Nearest | | | | | | | |
| B / CRT Geom | | | | | | | |
| B / CRT Lottes | | | | | | | |
| B / CRT Easymode | | | | | | | |
| C / Nearest | | | | | | | |
| C / CRT Geom | | | | | | | |
| C / CRT Lottes | | | | | | | |
| C / CRT Easymode | | | | | | | |
| D / Nearest | | | | | | | |
| D / CRT Geom | | | | | | | |
| D / CRT Lottes | | | | | | | |
| D / CRT Easymode | | | | | | | |
| E / Nearest | | | | | | | |
| E / CRT Geom | | | | | | | |
| E / CRT Lottes | | | | | | | |
| E / CRT Easymode | 96-100% observed | | | | | | |

For a clean A/B, delete `game:\shadercache` after changing shader mode. The
cache hash includes the mode, so stale bytecode will not be selected, but clearing
it makes test provenance obvious.

Test `batcir` for at least 60 seconds per effect with identical resolution,
VSync, audio sync, scene and ROM. Then test menu/OSD, hot switching, rotation,
aspect/integer scale, fast-forward, screenshots, game switch and exit. Record the
diagnostic line for effects 0, 5, 6 and 7 before drawing performance conclusions.
