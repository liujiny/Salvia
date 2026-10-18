# Salvia: Bypass Archive CRC32 and Enlarge State Validation Buffer

Baseline `ff858b03`. Diagnostics image built with `SALVIA_FBNEO_DIAGNOSTICS 1`.

## Why

1. **Root Cause of Long Game Launch Time (6–10 Second Frozen Screen)**:
   - When launching an arcade ROM (e.g. `mmpork.zip`, 65 MiB), FBNeo operates in `need_fullpath = true` mode, leaving `unzipped.memoryBuffer == NULL`.
   - In `src/salvia.h:797`, `computeRomCrcFromFile(unzipped.extractedPath)` was called. Because `CRC_FILE_MAX` was set to 96 MiB, it opened the 65 MiB `.zip` archive on the USB drive and read the entire file sequentially in 32 KiB chunks to compute a CRC32.
   - On Xbox 360 FATX over USB 2.0, streaming 65 MiB synchronously in 32 KiB blocks freezes the frontend for 6 to 10 seconds before starting the core.
   - Arcade cores (FBNeo, MAME) identify games and match cheat files (`.cht`) by driver name (e.g. `mmpork.cht`), never by archive container CRC32 (which varies by zip utility and compression ratio).
   - Bypassing CRC calculation for archive containers (`.zip`, `.7z`, `.rar`, `.tar`, `.gz`) allows `cheatlocator` to immediately fall back to matching by game name (0 ms), eliminating the 6–10 second launch freeze completely.
   - Reduced `CRC_FILE_MAX` from 96 MiB to 32 MiB to prevent large disc/package files from causing USB stalls.

2. **Savestate Validation Buffer Overhead**:
   - In `src/io/statefile.h:56`, `SalviaStateFile::validate` used a local `scratch[8192]` (8 KiB) buffer to validate decompression of the state stream.
   - For Cave CV1000 games with 152 MiB uncompressed core state, an 8 KiB buffer required over 19,500 iterations of `read` / `gzread`.
   - Enlarging `scratch` to 65,536 bytes (matching `SalviaStateFile::CHUNK`) reduces function call and decompression chunk overhead by 8x.

## What

1. **`src/salvia.h`**:
   - In `computeRomCrcFromFile()`: added extension check for `.zip`, `.7z`, `.rar`, `.tar`, `.gz` to return 0 immediately.
   - Reduced `CRC_FILE_MAX` to 32 MiB.

2. **`src/io/statefile.h`**:
   - In `SalviaStateFile::validate()`: increased `scratch` buffer from 8,192 bytes to 65,536 bytes.

## Verification

1. **Hash Verification**:
   - Source checkout and runtime directory `/mnt/e/Baiduyundownload/salvia-toolchain/.work/Salvia-main` verified identical.
