# CV1000 Optimization: Direct Command Queue Writes in `epic12_gpu_submit`

- Date: 2026-10-14
- Baseline Commit: `bf08310cb46ba1ec3a65239e1e793910c279cbe8`
- Target: `libretro/FBNeo/src/burn/devices/epic12_gpu.h`
- Diagnostic XEX: `salvia-tests/xex-archive/fbneo-20261014-1546-submit-direct-cmd-diag.xex` (SHA256: `7a78f6dfe4bb1a2aed8b95d1e3281958764c54d7a0dee51c89ea7cee62188415`)

## Rationale
In `epic12_gpu_submit`, every submitted draw operation previously constructed a 56-byte stack structure `Epic12GpuCommand c`.
1. The 8 rendering attribute fields (`flipx`, `flipy`, `transparent`, `blend`, `dmode`, `sa`, `da`, `tint`) were unconditionally populated and written into the stack frame before bounds/overlap checks.
2. If any bounds check or intersection check failed, those stores were wasted.
3. When the command was accepted, the entire 56-byte structure was copied from the stack frame into `epic12_gpu_commands[epic12_gpu_count++]`, triggering store-to-load forwarding (Load-Hit-Store) stalls across 139,609 calls in 120 frames (~1,163 calls/frame).

## Changes Made
1. **Scalar Register Bounds Checking**:
   - Replaced stack allocation `Epic12GpuCommand c` with local scalar variables `w, h, x, y, sx, sy` kept in CPU registers.
   - Evaluated early rejections (`w > 512 || h > 512`, `x < 0 || y < 0 || x + w > 8192 || y + h > 4096`, `sy + h > 4096`) before computing further coordinates or touching memory.
2. **Direct In-Place Queue Initialization**:
   - Initialized `Epic12GpuCommand &cmd = epic12_gpu_commands[epic12_gpu_count++]` directly at the final destination in the array, completely eliminating stack allocation and LHS copy penalties.

## Verification & Test Results
- PPC GPU Test Suite (`libretro/FBNeo/tests/epic12_gpu/run.py`):
  - PASS: 6,291,456 component cases on unified and fast/feedback shaders.
  - PASS: 14,400 commands full VRAM & delay comparison.
  - PASS: 640 grouped commands, snapshot overlap/area barriers, RECTLIST atlas geometry.
  - PASS: 640 varying-style commands on all paths.

## Performance Comparison (120-Frame Diagnostic Window)

| Metric | Baseline (`bf08310c`) | Optimized (`submit-direct-cmd`) | Delta |
| :--- | :--- | :--- | :--- |
| `game_and_ui` | 162.075 ms | **157.906 ms** | **-4.169 ms/frame** |
| `sampled_active_loop` | 162.217 ms | **158.048 ms** | **-4.169 ms/frame** |
| `drc_dispatch` | 4822.517 ms | **4821.321 ms** | **-1.196 ms** |
| `worker_wait avg_ms` | 6.972 ms | **6.279 ms** | **-0.693 ms** |
| `worker_wait peak_ms` | 13.942 ms | **12.556 ms** | **-1.386 ms** |

## Conclusion
Eliminating the stack command allocation and direct in-place array initialization in `epic12_gpu_submit` reduced memory bus contention and thread synchronization stalls, improving frontend frame loop time by 4.17 ms per frame.
