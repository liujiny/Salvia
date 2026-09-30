# CV1000: direct decoding of the measured hot interpreter family

Build ID: `cv1k-hot-fallback-20260930-r1`.
Source baseline: `5ca3c5cdf1ab797b35c9e0760012c77b00f5749c` (WORK-PROFILE) on `work/reversible-checkpoints`.
No GitHub push is requested. This is one performance experiment, not native-code expansion.

## Actual incoming workload

The incoming `cv1000-gpu.log` is 73 lines, entirely WORK-PROFILE. Lines 39-73 are the final pause window: 3037 frames, 53 phase samples and 21 separate workload-count frames. Settings remain ddpsdoj, CPU 102400000 Hz, 2 render cores, DIP 00,07,00,00, bpp=4. Earlier and later snapshots are not added together.

| Phase sample | Prior SOURCE-COMPARE input | WORK-PROFILE input |
| --- | ---: | ---: |
| Frames / phase samples | 3310 / 49 | 3037 / 53 |
| cpu_io ms | 15.979 | 15.622 |
| core total ms | 16.896 | 16.527 |
| frontend sampled_active_loop ms | 18.604 | 18.215 |
| core sampled peak ms | 24.586 | 23.882 |

The preceding diagnostic revision did not optimize the ordinary SH3 instruction sequence. Differences between these unequal input/sample windows are not proof of a speedup or measured FPS. Phase samples exclude workload-count frames, but diagnostic work still has nonzero real-time/cache effects.

The 21 workload frames contain 5040 slices, 994747 lookups, 975144 native calls, 54849 interpreted instructions and 8491 rebuilds. Useful observations:

- Approximately 46435 native calls and 2612 interpreted instructions per counted frame.
- `opcode_hi8=60` contributes 27813 interpreted instructions, **50.7083%** of the interpreted sample. This identifies a family, not an exact opcode, PC or device.
- Native snapshot length averages 8.92074 words. Only 33372 calls (3.4223%) use a 32-word snapshot. Length 32 is not proof of a block cut off by the limit, so blindly increasing MAX_INSNS is not justified.
- `lookups - validation_spans = rebuilds = 8491`. Within this sample, no additional rebuild arose after both metadata fields matched. This is not permission to skip source validation; resets, aliases, DMA and mutable code must still work.
- All exit counts sum to dispatch calls, native histogram bins sum to native calls, and native calls equal lookups minus no-entry and short-budget exits.

The interpreter guest-cycle total is not Xbox CPU time. CV1000's existing `speedhack_read_long` calls `Sh3BurnCycles` at its watched address and PC. The PPC emitter deliberately guards watched long reads back to the interpreter. The high 0x60xx count does not prove all those instructions are unsupported or safe to bypass. Some 0x6 operations are already compiled and others may be genuine interpreter-only operations.

Async presentation is active, with no GPU fallback or presentation timeout. The sound log still reports underruns/time stretching. State load ends ok=1 for 151788652 bytes with 21327872 bytes available. Neither zero dropped audio samples nor a successful load proves a performance improvement.

Raw inputs, prior XEX/EXE/MAP and affected baseline sources are preserved outside Git under `.work/cpu-hot-fallback-20260930-220621/baseline/`. `baseline-manifest.json` and `log-analysis.json` record hashes, exact windows and calculations. The original runtime logs and supplied optimization report are not edited.

## Single change

Add `sh3_interpreter_hot.h`, included after the existing instruction implementations and general decoder. In the timerhack run loop's two fallback sites, call `sh3_execute_hot_fallback` instead of `execute_one`.

For exactly `0x60xx`, the helper directly selects the existing sixteen 0x6 functions. All other opcodes call the unchanged general decoder. The native/JIT path is unchanged. Non-DRC builds retain the general path. The ordinary per-instruction-timer interpreter loop is not changed.

The shortcut reuses MOVBL/MOVWL/MOVLL, MOVBP/MOVWP/MOVLP, MOV, NOT, SWAPB/SWAPW, NEGC/NEG and extension helpers. It does not duplicate their instruction algorithms or perform memory access itself. Therefore mapped RAM/device handlers, watched idle-cycle charging, sign extension, effective addresses, same-register postincrement and T handling remain at their original locations. PC/delay updates and EAT/IRQ/timer order remain in the unchanged outer loop.

The intended saving is general decode dispatch and its function boundary for the measured hot family. It is not elimination of interpreter work. A predicted branch is added for other fallback families, and inlining may enlarge the run loop or increase register pressure. Actual benefit requires console comparison; no percentage is promised.

PPC emitter, snapshots, cache tables/limits/lifetimes, DRC guards, GPU/shaders/128-page batch limit, threads, audio algorithm, guest frequency, frame skip and save format remain unchanged. Existing disjoint workload sampling and pause-time three-log summaries remain unchanged; only the build identifier changes.

## Executed host checks

`tests/sh3_hot_fallback/run.py` extracts the general decoder and the actual sixteen instruction bodies from production source. Other families are handler-identification stubs; memory callbacks are controlled models with watched-idle-cycle side effects.

Both optimized and sanitizer executables passed 524288 opcode/state cases, covering every 16-bit opcode, all 256 hot-family encodings, all source register choices, same-register postincrement, T/EA and signed-value patterns, cached/uncached/internal/unaligned addresses passed to the original callbacks, callback ordering and unchanged cycle effects. Exactly 2048 hot cases bypass the generic decoder in this exhaustive synthetic matrix; its ratio is not the game's workload ratio.

Sanitizer qualification: ASan and UBSan were enabled with shift-base excluded only for unchanged legacy EXTSB/EXTSW signed-shift idioms. The actual flags and logs are saved. This is not an assertion of full UB freedom in the existing interpreter.

The existing five suites passed optimized and full ASan/UBSan configurations: dispatch/cache lifetime, four-way lookup, exact source comparison, Block layout and workload sampling. The extracted outer-loop regression also passed 12000 seeds x 12 slices in both modes. It abstracts instruction functions; the new exhaustive test independently validates the concrete selector/helpers. In total, 14 successful host test executions are recorded across the three runners.

No new PowerPC execution differential test or full DDPSDOJ replay was run. XDK compilation and native disassembly are checked separately.

## Build and reversible delivery

Only six reviewed source/test files were mirrored from the Git record checkout to `.work/Salvia` before building, with explicit digest checks. New test documentation and this review are recorded without including compiler products, SDKs, ROMs, private keys or raw logs.

Build command: the preserved `.work/cpu-cache-ready-20260930-205053/git-baseline/tools/build_fbneo_checkpoint.cmd` with the existing toolchain root as its argument. It rebuilds FBNeo, checks SDL and rebuilds Salvia Release_finalburn. Project post-image copying is not treated as final validation.

Binary rollback is `.work/fbneo-before-hot-fallback.xex`, SHA256 `03f85a24adb2a10924480f8a9157c1ee710578976b6d4689fb94211d68ba6584`, the exact WORK-PROFILE baseline. A Git revert must be mirrored to the runtime source and rebuilt; it cannot change an already copied XEX.

## Native result and artifact

FBNeo core Rebuild, SDL Build and Salvia frontend Rebuild all completed with exit code 0. Native bounded dumpbin inspection also exited 0. The compiler-created opcode tables in the dump are data, not executable instructions; their disassembly is not used for instruction-count claims.

The uncounted timerhack function is at `0x832653c0..0x83265a80` (1728-byte address range), versus the baseline `0x83265440..0x83265830` (1008 bytes). Both retain a 176-byte stack frame. The candidate saves r21..r31 instead of r22..r31, an additional register. The counted specialization grows from 1368 to 2080 bytes. These are code-size/register observations, not measured CPU cycles.

Both ordinary fallback sites mask the opcode high byte and compare with 0x6000 at `0x83265734` and `0x83265880`. Non-hot branches go to the single general-decoder call at `0x83265a14`, targeting SH3 `execute_one` at `0x8325bca8`. Hot cases dispatch by the low nibble to the original operations and then join the existing IRQ/cycle tail at `0x83265a18`. For example the MOVLL case calls its helper at `0x832657a8`, then skips directly to that shared tail. Memory helper calls are intentionally retained; this is not a claim of removing every function call.

The unchanged general decoder was laid out differently by the compiler (7792 to 6640 bytes), with some instruction helpers outlined/shared. Only symbols belonging to `libretro:sh4.obj` are used: similarly named functions from other CPU cores must not be confused with this decoder. `native-functions.json`, `native-routing.json` and the scoped assembly files preserve the evidence.

- Build time on the Runner: **2026-09-30 22:13:03**.
- Output: `E:\\Baiduyundownload\\salvia-toolchain\\.work\\Salvia\\Distro360\\fbneo.xex`.
- Build ID verified in linked EXE: `cv1k-hot-fallback-20260930-r1`; prior ID absent.
- XEX2 size: **34611200 bytes**; build and distribution digests match.
- SHA256: `c359029e0028dbea227897dc26f3c8e0fd387142e499658c4f3a7ce1e333b89b`.
- Rollback: `.work/fbneo-before-hot-fallback.xex`, the WORK-PROFILE baseline.
- Native build/disassembly, host tests and source equality are recorded separately. No candidate console FPS or full-game replay is claimed.

The larger hot run loop, one additional saved register and the new non-hot predicate may offset decode savings. Test against WORK-PROFILE with the same state, configuration and input; do not treat the 50.7% interpreter-family share as a speedup forecast. The unchanged workload counters should still classify 0x60xx as interpreted, because the optimization does not migrate those instructions to native blocks.

The exact source commit and parent are added to the external final-artifact manifest after the separate commit is created. It remains local until an explicit upload request.
