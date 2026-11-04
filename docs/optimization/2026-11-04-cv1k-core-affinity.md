# CV1000: Xenon 3-Core SMT Hardware Thread Affinity Alignment

Baseline `779a9a16` (blitter spin loop fast-forwarding & branchless blend). Implementation commit `4763a884`.

## Why

1. **Xenon CPU Hardware Architecture**:
   - The Xbox 360's Xenon processor contains 3 symmetrical PowerPC physical cores running at 3.2 GHz, each with 2 simultaneous hardware threads (SMT):
     - **Physical Core 0**: HW Thread 0 & HW Thread 1 (Thread 1 is also shared with OS/dashboard kernel tasks).
     - **Physical Core 1**: HW Thread 2 & HW Thread 3.
     - **Physical Core 2**: HW Thread 4 & HW Thread 5.
   - SMT threads on the same physical core share execution pipelines, L1 caches, and issue slots. If two compute-heavy threads share a physical core, in-order execution stalls frequently.

2. **Previous Misalignment in Salvia**:
   - In `src/engine.cpp:43-48`, the documented design intention was:
     `// Keep CPU emulation on hardware thread 0 and leave HW thread 1 idle for OS/dashboard tasks`
   - However, in `src/const/constant.h:29`, the macro was defined as:
     `#define CPU_THREAD 1`
     This inadvertently pinned the primary SH-3 / 68K emulation loop to HW Thread 1—the secondary hyperthread contending with OS tasks.
   - Furthermore, `libs/libSDLx360/SDL/src/audio/xbox/SDL_xboxaudio.c` pinned the audio playback thread to HW Thread 5 (Physical Core 2), where Salvia's IO thread (`IO_THREAD 4` / `SDL_SYS_CreateThreadEx(..., 4)`) also resides.
   - Meanwhile, HW Thread 3 (Physical Core 1's secondary hyperthread) was left completely unused.

3. **Lessons from `ibara360` (`main.c:315-316`)**:
   - `ibara360` achieves rock-solid framerates by cleanly mapping workloads across the 3 physical cores:
     - **Core 0 (HW Thread 0)**: Primary arcade CPU emulation (SH-3 DRC + burn loops) with elevated priority (`THREAD_PRIORITY_ABOVE_NORMAL`).
     - **Core 1 (HW Thread 2 & 3)**: HW Thread 2 handles GPU command submission / render worker; HW Thread 3 handles audio playback (sleep-paced XAudio2 pump).
     - **Core 2 (HW Thread 4 & 5)**: Network, file IO, and background workers.
   - This layout isolates the latency-critical main emulation on Core 0 without SMT contention, places the audio thread adjacent to the GPU render worker on Core 1, and pushes all file/network IO to Core 2.

## What

1. **`src/const/constant.h`**:
   - Changed `CPU_THREAD` from `1` to `0`.
   - Updated thread map documentation to reflect the clean 3-core separation.

2. **`src/engine.cpp`**:
   - Set the main emulation thread priority to `THREAD_PRIORITY_ABOVE_NORMAL` and explicitly pinned to `CPU_THREAD` (HW Thread 0).

3. **`libs/libSDLx360/SDL/src/audio/xbox/SDL_xboxaudio.c`**:
   - Pinned the XAudio2 audio pump thread to HW Thread 3 (`XSetThreadProcessor(GetCurrentThread(), 3)`).

4. **`libretro/FBNeo/src/burn/devices/render_worker.h`**:
   - Updated comments clarifying that the render worker on HW Thread 2 pairs with audio on HW Thread 3 on Core 1, keeping Core 0 dedicated to main emulation.

## Verification

1. **Host State Test Suite**:
   - `tests/frontend_state/run.py`: **100% PASS**.

2. **Subsystem Builds**:
   - `libSDLx360.lib`: Rebuilt cleanly with `SDL_xboxaudio.c` pinned to HW Thread 3.
   - `Salvia.vcxproj` (`Release_default`): Rebuilt cleanly with `DeployOnBuild=false`, producing `default.xex` (6,856,704 bytes, SHA256: `726f8a688ed57d98734d1f69df834556af9b821e23644746f296202ec35a8e1f`).

3. **Core and Diagnostic Image Build**:
   - Compiled with `build-xex.sh --baseline 779a9a16 --tag cv1k-core-affinity --flavor diag --force`:
     - FBNeo core: clean build (`libretro.lib`).
     - Salvia frontend: recompiled `engine.cpp` and linked with `libSDLx360.lib`.
   - **Candidate Image (Diagnostic)**: `Distro360/fbneo.xex`
     - Tag: `cv1k-core-affinity 4763a884 20261104-1900 diag`
     - Size: `34,832,384` bytes
     - SHA256: `6aa963c87514250a94321e58712a2b1b6f012bcb9245cac39b4d6c6c97da2394`
     - Archive: `/home/humor/salvia-tests/xex-archive/fbneo-20261104-1900-cv1k-core-affinity-diag.xex`
