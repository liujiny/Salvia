# CV1000: inline the C++ dispatcher into the SH3 run loop

Build ID: `cv1k-inline-dispatch-20260930-r1`.
Baseline commit: `a7b0ad926b75d98fd28c8229fa1667e4be3ee2b0` on `work/reversible-checkpoints`.
This is one local, reversible experiment. No GitHub push is requested.

## Input and comparison limits

The input GPU log is 39 lines, entirely `cv1k-cache-ready-20260930-r1`, not a mixture of prior runs.
Lines 22-39 are the last pause window: 2568 frames, 43 valid random samples, no invalid samples.
Configuration: ddpsdoj, 102400000 Hz, 2 render cores, DIP 00,07,00,00, bpp=4.

| Sampled combat phase | HOTMETA reference | CACHE-READY input |
| --- | ---: | ---: |
| Frames / samples | 3412 / 55 | 2568 / 43 |
| cpu_io ms | 15.464 | 16.021 |
| core total ms | 16.388 | 16.937 |
| draw_sync ms | 0.776 | 0.777 |
| frontend sampled_active_loop ms | 18.091 | 18.541 |
| core sampled_peak ms | 22.773 | 22.904 |

The HOTMETA reference is the archived GPU log from the preceding tag-table review, lines 61-78.
The small difference does not establish a stable improvement or regression: durations, inputs and
random samples differ, and there is no variance estimate or deterministic console replay. The
CACHE-READY guard remains an unproven micro-optimization. It is retained unchanged as the direct
baseline for this experiment, rather than bundling a rollback with another code change.
Comparison with rejected TAGTABLE would confound its rollback with the guard's contribution.
No sampled timing reciprocal is reported as measured FPS.

Async presentation is active without fallback or timeout (GPU line 34); GPU fallback is zero (line 30).
GPU waits and counts are cumulative, unlike the core/front-end pause windows, so they are not summed
or directly subtracted from frame phase times. cpu_io includes device I/O and buffered sound.
The audio log still has underruns and time stretching; dropped_samples=0 does not imply healthy audio.
The state log records load-done ok=1 for 151788652 bytes with 21327872 bytes available.

Original logs, report, executable, map and affected sources are byte-preserved under toolchain
`.work/cpu-inline-dispatch-20260930-210835/baseline/`, with `baseline-manifest.json`.
The baseline XEX SHA256 is `e233b3ba3f05b42ce57058f9e973349f2286056921981bda7d31db1ae2036812`.

## Concrete native-code observation

The linked baseline `Sh3Run_timerhack` is at `0x832652b0..0x83265400` (336 bytes).
Its call at `0x83265330` branches to the separate C++ dispatcher at
`0x83261a08..0x83261cc8` (704 bytes). The caller has its own 128-byte frame;
the dispatcher has a 176-byte frame and saves/restores r21..r31.
This is a real function boundary, not merely a source-level helper that was already inlined.
See `timerhack-baseline.asm`, `dispatcher-baseline.asm` and `native-baseline.json` outside Git.
The old XDK dumpbin warns that /nologo is ignored; the disassembly itself exits successfully.

The boundary is crossed on C++ dispatcher re-entry, for example after interpreter fallback.
It is NOT crossed for every native block within an already fused chain. Therefore removing it
cannot be equated to eliminating every native-block call or a known fraction of total frame time.

## Single performance change

Qualify `sh3_drc_dispatch` with `__forceinline` on Xbox/XDK and `always_inline` for GNU builds,
with an ordinary inline fallback for other compilers. The GNU qualifier lets host regressions
exercise the same requested inline form. The macro is undefined at the end of the header.

The function body and its caller are byte-for-byte unchanged. Only the function qualifier changes;
the profile header changes only the build ID. This asks the compiler to place the C++ dispatch loop
in the SH3 run-loop caller so native-to-interpreter transitions need no additional dispatcher frame.
It does not link native PPC blocks, remove indirect native-entry calls, raise instruction limits,
skip source validation, cache mappings, alter cycles or reorder emulated events.

HOTMETA layout, the 4-way lookup, exact halfword checks, CACHE-READY guard, allocator, generated-PPC
emitter, interpreter, timers, GPU/shaders, thread assignments, audio, front-end and save format remain
unchanged. There is no new allocation or runtime profiling work. Rejected TAGTABLE, 256-page batching,
precise GPU dependency scans, additional render cores and CRT combinations are not retried.

Tradeoff: inlining can increase caller register pressure and code size, and the compiler may choose
an unfavorable schedule. Native verification and a controlled console comparison remain necessary.
A source annotation alone does not prove that the XDK honored inlining or that performance improves.

## Actual host verification

The baseline dispatcher regression passed before the qualifier changed.
`libretro/FBNeo/tests/sh3_dispatch/run_inline.py` then ran four suites, optimized and ASan/UBSan:

- 12000 random dispatch traces x 12 slices against the reference algorithm, including mutable code,
  map changes, IRQ/delay gates, partial exits, short budgets, cache eviction and final tags/cursors.
- Cold/warm/sticky-failure allocation lifetime checks, including reset and reallocation.
- 228125 independent 4-way probes and 1000000 replacement/reset operations.
- 345929 exact snapshot comparisons, including guarded pages and halfword/tail boundaries.
- 2323712 production Block snapshot/metadata checks over 64 positions and 0..33 lengths.

All passed. The script records the actual compiler/run argv, exit codes and logs in its explicit
output directory; it fails if any compile or test fails. Example from the repository root:

```sh
python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/salvia-inline-tests
```

These are host synthetic/data tests, not PowerPC execution, a full game replay or console FPS.
The PPC compiler and QEMU are not available in the inspected WSL PATH; no such run is claimed.

## Mirroring, build, rollback

Only the two production headers and the new test runner were mirrored to sibling `.work/Salvia`,
after checking all relevant baseline hashes against the Git checkout. `mirrored-files.json` records
exact digests. Build uses the preserved `.work/cpu-cache-ready-20260930-205053/git-baseline/tools/build_fbneo_checkpoint.cmd`
with the existing toolchain root. It rebuilds the FBNeo core, checks SDL and rebuilds Salvia.
Project post-image steps can copy the XEX early; successful build alone is not final publication validation.

Binary rollback: toolchain `.work/fbneo-before-inline-dispatch.xex`, the exact CACHE-READY baseline.
To undo source changes, use `git revert <this-performance-commit>`, mirror reverted source to the
runtime directory, then rebuild and verify. A Git revert alone does not replace an existing XEX.
No raw logs, SDKs, keys, ROMs, compiler products or XEX files are included in the commit.

Performance summaries retain the three-log, pause-time workflow and existing 1/64 random sampling.
A fresh build ID distinguishes appended records. Test from the same saved state and with the same
settings/input; compare this candidate with CACHE-READY, not an older rejected experiment.

## Native build and final identity

FBNeo core Rebuild, SDL Build and Salvia frontend Rebuild completed with exit code 0.
The final map no longer contains a separate `sh3_drc_dispatch` symbol. The actual
`Sh3Run_timerhack` is now `0x83264ff0..0x832653e8` (1016 bytes of address range).
The prior caller plus dispatcher occupied 336 + 704 = 1040 bytes of address ranges;
these include linker alignment and are not a performance measurement.

The candidate's bounded disassembly contains the four-way lookup, HOTMETA stride 84,
full halfword validation, conditional compilation, cycle guards, native indirect call
at `0x832652c8`, interpreter fallback at `0x8326537c`, IRQ processing and timer tail
in one function. The dispatcher call/return boundary in the old caller is gone.
The native-entry `bctrl` remains, as intended; this is not native block chaining.

Register-pressure tradeoff is visible: the inlined caller saves r18..r31 and has a
208-byte frame instead of the old caller's r28..r31 and 128-byte frame. It no longer
adds the separate dispatcher's 176-byte frame on each re-entry. This shifts work
from repeated dispatcher transitions to run-loop entry and may be less favorable
when little native execution occurs. No console speedup is inferred from these facts.

- Build time on the Runner: 2026-09-30 21:14:18.
- Output: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.
- Build ID: `cv1k-inline-dispatch-20260930-r1`; verified in the linked EXE.
- Size: 34611200 bytes; XEX2 header; build and distribution SHA256 match.
- SHA256: `843f4a254097762643a39330b4a7277f03d404bf8341e9a3d4b02888d5f42a52`.
- Rollback is the direct CACHE-READY baseline, SHA256
  `e233b3ba3f05b42ce57058f9e973349f2286056921981bda7d31db1ae2036812`.
- Source body and caller/emitter/validation/data layout are unchanged; the two
  production header differences are the inline qualifier and build identifier.
- Evidence outside Git: `final-artifact.json`, `source-validation.json`,
  `timerhack-baseline.asm`, `dispatcher-baseline.asm`, `timerhack-candidate.asm`,
  `log-analysis.json`, `tests-before/`, `tests-after/` and `build.log`.
- No new PPC execution differential test, full-game replay or console FPS measurement.

The single performance commit also carries this report and the host reproduction
script. Its exact SHA and parent are written to the external final-artifact manifest
once committed. It remains local until an explicit upload request.
