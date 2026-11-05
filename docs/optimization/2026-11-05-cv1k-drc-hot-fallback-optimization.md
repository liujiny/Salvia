# CV1000: Optimize Hot Fallback Opcodes and Register External Device Read

Baseline `469bcce2` (Option A verification). Implementation commit `e10aea50`.

## Why

1. **Hardware Profiling of Interpreter Fallbacks**:
   - Hardware profiling on Xbox 360 Jasper (`cv1000-gpu.log`) revealed recurring CPU spikes during peak frames (`cpu_io = 34.349 ms`), driven by interpreter fallbacks:
     - `pc=8C000604 opcode=6002 reason=partial class=handler first_address=A4000000`: 411 calls per session.
     - `pc=0C05C06A..0C05C092`: RTC9701 real-time clock bit-banging access to `0xB0C00001` via opcodes `0x2740` (`MOV.B R4, @R7`), `0x2750` (`MOV.B R5, @R7`), and `0x6170` (`MOV.B @R7, R1`) totaling > 3,000 calls per session.
     - `pc=0C060184..0C060186`: YMZ770 audio register writes to `0xB0400000` and `0xB0400001` via `0x2200` (`MOV.B R0, @R2`) and `0x2310` (`MOV.B R1, @R3`) totaling 1,226 calls per session.
     - `pc=0C002580..0C0025DC`: TMU on-chip timer registers `0xFFFFFE9C` (`TCR0`) and `0xFFFFFEA8` (`TCR1`) via `0x6011` (`MOV.W @R1, R0`) and `0x2101` (`MOV.W R0, @R1`).

2. **Root Cause Analysis**:
   - **Inactive Device Read Registration**: In `libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`, line 688 initialized device read slot 1 as `Sh3SetDrcDeviceRead(1, 0x0400002c, 7)`. The address `0x0400002c` was never read during gameplay, and handler 7 was unmapped in the driver. In reality, the SH-3 code at `8C000604` polls `0xA4000000` (physical `0x04000000`, handler 0 `main_read_long`) 411 times. Because slot 1 was misconfigured, `sh3_drc_service_movll` rejected all 411 reads (`handled=0`), forcing the DRC block to exit and fall back to the C++ interpreter every time.
   - **Narrow Destination Filtering in Hot Fallback Dispatcher**: In `libretro/FBNeo/src/cpu/sh4/sh3_interpreter_hot.h`, `sh3_execute_hot_fallback` checked `(opcode & 0xff00) == 0x6000`, which restricted fast-path dispatch exclusively to destination register R0 (`n == 0`). Any load with destination $n \ne 0$ (such as `0x6170` at `0x0C05C084` or `0x6011` at `0x0C0025DC`) and all store opcodes (`0x2000`, `0x1000`, `0x5000`) fell through to the full multi-thousand-line `execute_one` decoder.

## What

1. **`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`**:
   - Corrected device read slot 1 from `(0x0400002c, 7)` to `(0x04000000, 0)`:
     ```cpp
     Sh3SetDrcDeviceRead(0, 0x18000010, 0);
     Sh3SetDrcDeviceRead(1, 0x04000000, 0);
     ```
   - Matches the physical address and handler for `0xA4000000`, allowing `sh3_drc_service_movll` to service `0x6002` at `8C000604` directly without dropping out of DRC, eliminating 411 exits per session.

2. **`libretro/FBNeo/src/cpu/sh4/sh3_interpreter_hot.h`**:
   - Expanded `0x6000` fast path from `(opcode & 0xff00) == 0x6000` to `(opcode & 0xf000) == 0x6000`, covering all 16 destination registers ($n = 0..15$).
   - Added fast path for `0x2000` memory stores: `MOVBS` (`0x2nn0`), `MOVWS` (`0x2nn1`), `MOVLS` (`0x2nn2`), `MOVBM` (`0x2nn4`), `MOVWM` (`0x2nn5`), `MOVLM` (`0x2nn6`).
   - Added fast path for `0x1000` (`MOVLS4`) and `0x5000` (`MOVLL4`).
   - Increases hot bypassed opcodes in `tests/sh3_hot_fallback` from 2,048 up to **110,592** cases (a 54x expansion), completely eliminating `execute_one` overhead on all dominant memory fallback instructions.

3. **`libretro/FBNeo/tests/sh3_hot_fallback/`**:
   - Updated `fixture.h` with modeled `write_memory` (`WB`, `WW`, `WL`).
   - Updated `run.py` to test all 24 production helpers and all 110,592 hot cases across all 65,536 opcodes and 8 test states.

## Verification & Results

1. **Host & PPC Test Suites**:
   - `libretro/FBNeo/tests/sh3_hot_fallback/run.py --output /tmp/sh3_hot_fallback_out`: **100% PASS** (all 524,288 state/opcode cases bit-identical between `execute_one` and `sh3_execute_hot_fallback`; 110,592 hot cases bypass general decoding).
   - `libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`: **100% PASS** (848,846 cases compiled and verified under QEMU PPC).
   - `libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/sh3_dispatch_out`: **100% PASS** (8 suites, 12,000 traces $\times$ 12 slices).
   - `libretro/FBNeo/tests/ymz770_idct/run.py`: **100% PASS** (100,000 subband vectors bit-identical).
   - `libretro/FBNeo/tests/frontend_state/run.py`: **100% PASS** (streaming, CRC, worker ownership).

2. **Artifact Identity & Build**:
   - Build script: `/home/humor/salvia-tests/xex-build/build-xex.sh --tag cv1k-drc-hot-fallback --baseline 469bcce2 --flavor diag`
   - Archive Image: `/home/humor/salvia-tests/xex-archive/fbneo-20261105-2021-cv1k-drc-hot-fallback-diag.xex`
   - Deployed Image: `Distro360/fbneo.xex`
   - Flavor: `diag` (`SALVIA_FBNEO_DIAGNOSTICS 1`)
   - Image Size: 34,832,384 bytes
   - SHA256: `3f233c23ed8a85754f4b92f3d620d7c28e0c5aecb89165d2c398fba8b64f07d1`

3. **Console Deployment**:
   - Target: Xbox 360 Jasper at `192.168.2.232:/Usb0/Salvia-360/fbneo.xex`
