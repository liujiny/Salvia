# CV1000: In-Engine AOT Static Precompilation for SH-3

Baseline `7527076c`.
Implementation commits:
- `2fe3a25c` (`feat: implement in-engine AOT static precompilation for CV1000 SH-3`)
- `855a3446` (`fix(sh3_drc): use Sh3Ppc::sync_code for portable Xbox 360 cache flush`)

## 1. Context & Motivation

On the Xbox 360 Xenon CPU (3-core, 6-thread 3.2 GHz PowerPC with in-order execution), JIT compilation during active gameplay causes pipeline stalls and cache line invalidations (`dcbst`, `sync`, `icbi`, `isync`). Even with enlarged DRC code arenas and 4-way associative hash lookups, on-demand compilation introduces micro-stutters as new bullet patterns, enemy AI subroutines, or boss phase code blocks are encountered.

Furthermore, CV1000 arcade titles (*Muchi Muchi Pork!*, *Ibara*, *Futari*, *Deathsmiles*, etc.) run from static SDRAM where executable code is unpacked during game boot and remains completely immutable throughout gameplay. By performing an exhaustive recursive control-flow analysis and precompiling all reachable basic blocks during boot/attract screen:
1. Runtime JIT compilation during bullet-hell gameplay is reduced to zero.
2. The Fast Shadow lookup table (`SALVIA_CV1K_FAST_SHADOW`) is pre-populated for all game routines, allowing instant $O(1)$ dispatch without hash collision probing or memory comparisons.
3. Instruction cache synchronization is batched across the entire precompiled arena in a single pass rather than executing individual `icbi` loops per block.

---

## 2. Technical Architecture & Implementation

### 2.1 Breadth-First Control Flow Traversal (`libretro/FBNeo/src/cpu/sh4/sh3_drc_ppc.h`)

Implemented `Sh3DrcPrecompile(UINT32 start_pc, UINT32 size)` using a static visited bitset (`Sh3PrecompileVisited`) and a circular BFS work queue (`Sh3PrecompileQueue`):

1. **Queue Architecture**:
   - `s_visited_bits`: 512 KB bitset covering 16 MB code space at 4-byte instruction granularity.
   - `s_queue`: 32,768-entry circular queue with overflow protection.
   - `sh3_precompile_enqueue(target_pc)`: validates PC range `[start_pc, start_pc + size)`, checks visited bit, marks visited, and pushes to queue.

2. **Recursive Control Flow Discovery**:
   - Decompiles basic blocks instruction by instruction up to terminal opcodes (`RTS`, `RTE`, unconditional `BRA`, `JMP`, `TRAPA`).
   - Resolves PC-relative branches and enqueues targets:
     - `BF` (`0x8b00`), `BF/S` (`0x8f00`), `BT` (`0x8900`), `BT/S` (`0x8d00`): enqueues branch destination `(pc + 4 + disp8 * 2)` and linear fallthrough.
     - `BRA` (`0xa000`), `BSR` (`0xb000`): enqueues destination `(pc + 4 + disp12 * 2)` and return point for subroutine calls.
     - `BRAF` (`0x0023`), `BSRF` (`0x0003`), `JSR` (`0x400b`), `JMP` (`0x402b`): resolves register displacements when preceded by literal pool loads (`MOV.L @(disp, PC), Rn`), enqueues computed target and fallthrough.
   - Enqueues linear fallthrough at the end of each non-terminal block.

3. **Heuristic Entry Point & Prologue Seeding**:
   - Seeds hardware vector entry points: reset vector, exception vectors (`0x0C000000`, `0x100`, `0x400`, `0x600`).
   - Scans 32-bit aligned code space for SH-3 function prologue patterns:
     - `MOV.L R14, @-R15` (`0x4f22`)
     - `MOV.L Rn, @-R15` (`0x2fe6`, `0x2fd6`, `0x2fc6`, `0x2fb6`, `0x2fa6`, `0x2f96`, `0x2f86`)
   - Scans 32-bit aligned literal tables for valid code pointers within `[start_pc, start_pc + size)`.

4. **Batch Compilation & Cache Flushing**:
   - For every popped PC, compiles PPC native block via `sh3_drc_compile(pc)`.
   - Populates both `lookup` tags and `shadow` entries.
   - Emits batch PowerPC instruction cache sync across the entire arena:
     ```cpp
     Sh3Ppc::sync_code(Sh3Ppc::code, Sh3Ppc::code + Sh3Ppc::used);
     ```

### 2.2 Game Lifecycle Integration (`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp`)

- Declared `static bool s_cv1k_aot_done = false;`.
- **Boot / Attract Trigger**: In `DrvFrame()`, triggers once when `nCurrentFrame >= 180` (3 seconds after ROM unpack):
  ```cpp
  if (!s_cv1k_aot_done) {
      s_cv1k_aot_done = true;
      Sh3DrcPrecompile(0x0c000000, 0x01000000);
  }
  ```
- **Savestate Trigger**: In `DrvScan(nAction, pnMin)`, triggers upon loading state (`nAction & 1`) if not yet performed.
- **Hardware Reset**: In `DrvDoReset()`, clears `s_cv1k_aot_done = false;`.

---

## 3. Verification & Validation

### 3.1 Host & QEMU Test Suites (100% PASS)

1. **Inline Dispatch & Microarchitecture Suite**:
   ```bash
   python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py
   ```
   - Result: **100% PASS** (all 8 host suites, ASan/UBSan clean, 442,729 cases).

2. **Frontend State & Serialization Suite**:
   ```bash
   python3 libretro/FBNeo/tests/frontend_state/run.py
   ```
   - Result: **100% PASS** (streaming, 151 MiB, gzip CRC/truncation).

3. **SH-3 PPC DRC & AOT Unit Suite**:
   ```bash
   python3 libretro/FBNeo/tests/sh3_ppc/run.py --toolchain-root /mnt/e/Baiduyundownload/salvia-toolchain
   ```
   - Result: **100% PASS** (848,846 cases).
   - Specifically verified: `EDGE AOT precompilation / BFS control flow traversal PASS` (prologue scanning, conditional branch following, block population).

4. **EPIC12 GPU Simulation Suite**:
   ```bash
   python3 libretro/FBNeo/tests/epic12_gpu/run.py --toolchain-root /mnt/e/Baiduyundownload/salvia-toolchain
   ```
   - Result: **100% PASS** (14,400 commands, 10.5M component cases).

---

## 4. Binary & Artifact Identity

- **Artifact Tag**: `cv1k-aot-precompile`
- **Flavor**: Diagnostics (`SALVIA_FBNEO_DIAGNOSTICS 1`)
- **Deploy Path**: `Distro360/fbneo.xex`
- **File Size**: `34,832,384` bytes
- **SHA256**: `db5f2ad0a49c9a0159c1f51034663aa7916e52429d4da4ccd85791fd7668ae7e`
- **Archive Path**: `/home/humor/salvia-tests/xex-archive/fbneo-20261106-2216-cv1k-aot-precompile-diag.xex`
