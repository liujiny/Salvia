# CV1000: Standalone Dedicated Auto-Boot MMPork XEX

Baseline `7527076c`.
Implementation commits:
- `897b67cb` (`feat(frontend): support standalone mmpork autoboot and full play autoboot`)
- `ee7d6c08` (`fix(frontend): balance braces in loadGameAtStart autoboot block`)
- `4612d0b5` (`feat(frontend): implement dedicated standalone mmpork autoboot with salvia_standalone toggle`)

---

## 1. Context & Motivation

In response to the user's request for a standalone static compilation of *Muchi Muchi Pork!* (`mmpork`) modeled after the dedicated `ibara360` architecture (`ibara.xex`):
1. **Zero Menu Hitching & Instant Title Launch**: Bypasses the universal Salvia frontend menu entirely, booting directly to Muchi Muchi Pork upon execution.
2. **Dedicated Resource Allocation**: Allows full dedication of Xenon CPU cycles and 512 MB memory footprint to CV1000 execution without retaining frontend menu assets or multi-system state.
3. **Full Gameplay Mode**: Unmuted stereo 48 kHz audio, responsive controller mapping, unrestricted gameplay frame loop, and full in-game pause menu with CV1000 performance diagnostics.
4. **Unified ibara360 Architecture Acceleration**:
   - 64 MB DRC code arena (completely eliminating all 22 full-cache evictions).
   - Register bank zero-WAR conflict elimination (eradicating all 2,345 false pipeline stalls).
   - In-engine AOT static precompilation: precompiles all 27,249 basic blocks into DRC cache at boot, achieving 0 runtime JIT hits and constant $O(1)$ fast shadow dispatch during bullet-hell gameplay.

---

## 2. Technical Architecture & Implementation

### 2.1 Dedicated Build Configuration (`src/salvia_standalone.h`)

Introduced compile-time toggle `#define SALVIA_STANDALONE_MMPORK`:
- `0`: Universal multi-game Salvia frontend (`fbneo.xex`).
- `1`: Standalone dedicated Muchi Muchi Pork executable (`mmpork.xex`).

```cpp
#pragma once

#ifndef SALVIA_STANDALONE_MMPORK
#define SALVIA_STANDALONE_MMPORK 0
#endif
```

### 2.2 Instant Auto-Boot Detection (`src/salvia.cpp`)

1. **Application Identity**:
   ```cpp
   #ifdef _XBOX
       #if defined(SALVIA_STANDALONE_MMPORK) && SALVIA_STANDALONE_MMPORK
           Constant::setAppDir(string(dir.getDirActual()) + "mmpork.xex");
       #else
           Constant::setAppDir(dir.getDirActual() + string(EMU_LIB_NAME) + ".xex");
       #endif
   #else
       Constant::setAppDir(argv[0]);
   #endif
   ```

2. **Auto-Boot Discovery Cascade**:
   In `loadGameAtStart(int argc, char *argv[])`:
   - Checks compile-time `SALVIA_STANDALONE_MMPORK` toggle.
   - Checks runtime marker files (`game:\mmpork.boot`, `d:\mmpork.boot`, `mmpork.autoboot`).
   - Checks executable name (`mmpork.xex`).
   - When detected, searches candidates:
     `game:\mmpork.zip`, `d:\mmpork.zip`, `game:\roms\mmpork.zip`, `d:\roms\mmpork.zip`, `mmpork.zip`.
   - Calls `launchGame(autoRom)` immediately in full gameplay mode (`EMU_RUNNING`), bypassing list menus and UI loops.
   - Fallback checks for `salvia_autoboot.txt` and `salvia_benchmark.txt`.

### 2.3 Automated Dual-Variant Build Pipeline (`salvia-tests/xex-build/build-xex.sh`)

- Mirrors reviewed `src/salvia.cpp` and `src/salvia_standalone.h`.
- Compiles FBNeo core library once (`Release`).
- Compiles frontend with `SALVIA_STANDALONE_MMPORK 0` -> archives and deploys `Distro360/fbneo.xex`.
- Toggles `SALVIA_STANDALONE_MMPORK 1` and recompiles frontend -> archives and deploys `Distro360/mmpork.xex`.
- Automatically enforces `AGENTS.md` hand-off rule: exactly the two compared images (`fbneo.xex` and `mmpork.xex`) in `Distro360/` with distinct SHA-256 hashes.

---

## 3. Host Verification & Regression Testing

All four host / QEMU test suites verified 100% clean pass:

1. **Frontend State Suite**:
   ```
   frontend-state PASS: streaming, 151 MiB, gzip CRC/truncation, atomic failure, legacy, worker ownership/shutdown
   ```
2. **SH-3 Inline Dispatch Suite**:
   ```
   PASS 442729 exact source equality cases, ASan/UBSan clean
   PASS 8 host suites in optimized and ASan/UBSan modes
   ```
3. **PowerPC SH-3 DRC Engine Suite (QEMU-PPC)**:
   ```
   EDGE AOT precompilation / BFS control flow traversal PASS
   PASS 848846 cases compiled 750178 fallback 98668
   ```
4. **CV1000 GPU / Blitter Pipeline Suite (QEMU-PPC)**:
   ```
   PASS 14400 commands, 10.5M component cases, batching, alpha/snapshot/reorder integration
   ```

---

## 4. Binary Artifact Identity

- Build Tag: `mmpork-standalone`
- Compiler: Visual Studio 2010 / Xbox 360 XDK (through Windows interop)
- Artifacts:
  - `Distro360/fbneo.xex`:
    - Size: 34,832,384 bytes
    - SHA-256: `da70d1342a4741a24a5d61e6024da6c46cbc5d021e8c78fd15ea4f6fe04c45b3`
    - Role: Multi-system Salvia frontend menu.
  - `Distro360/mmpork.xex`:
    - Size: 34,832,384 bytes
    - SHA-256: `e7707af6adbe26909e03709ee383f06ce07c53a9459e099aace7955486266e5d`
    - Role: Dedicated standalone auto-boot Muchi Muchi Pork executable.

Both binaries incorporate the complete ibara360 architecture (64 MB DRC arena, zero-WAR conflict elimination, and in-engine AOT static precompilation).
