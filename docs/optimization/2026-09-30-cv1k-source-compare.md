# CV1000: direct halfword equality in the SH3 source validator

Candidate build ID: `cv1k-source-compare-20260930-r1`.
Baseline: `3ce184b95b20b5d92cbee458f411e0c0dcdd454d` on `work/reversible-checkpoints`.
One independent local experiment; no GitHub push requested.

## Incoming console logs

The GPU log has 39 lines, all from `cv1k-inline-dispatch-20260930-r1`.
The final pause window (lines 22-39) has 3110 frames, 50 valid samples and no invalid samples.
It reports ddpsdoj, CPU 102400000 Hz, 2 render cores, DIP 00,07,00,00 and bpp=4.

| Sampled combat phase | CACHE-READY reference | INLINE-DISPATCH input |
| --- | ---: | ---: |
| Frames / samples | 2568 / 43 | 3110 / 50 |
| cpu_io ms | 16.021 | 16.133 |
| core total ms | 16.937 | 17.057 |
| draw_sync ms | 0.777 | 0.776 |
| frontend sampled_active_loop ms | 18.541 | 18.516 |
| core sampled_peak ms | 22.904 | 21.379 |

Reference values are in the preceding inline experiment's archived input logs.
These are different input lengths and random samples, without deterministic A/B
or a variance estimate. Neither stable improvement nor a precise slowdown is established.
Inlining remains unproven and is retained as the direct baseline to isolate this
new comparator experiment; the next comparison is not against reverted TAGTABLE.
Sampled loop reciprocals are not measured FPS. cpu_io includes device handlers
and buffered sound; the logs do not measure the validator's share of that time.

Async presentation is active with zero fallbacks/timeouts (GPU line 34), and GPU
fallback is zero (line 30). Cumulative GPU timings are not added to pause-window
CPU timings. Audio still has underruns/stretching (audio line 2), despite zero
reported dropped samples. State loading finishes ok=1 for 151788652 bytes (state
lines 1-8). The audio/state logs lack build identifiers; their association uses
the supplied run context rather than an independent version field.

Raw input and baseline source/XEX/EXE/MAP are byte-preserved at toolchain
`.work/cpu-source-compare-20260930-212201/baseline/` with `baseline-manifest.json`.
Baseline XEX SHA256: `843f4a254097762643a39330b4a7277f03d404bf8341e9a3d4b02888d5f42a52`.

## Single code change and rationale

`sh3_drc_source_equal` previously used a four-pair XOR/OR reduction for each group
of four SH3 opcodes. Its linked baseline contains eight lhz loads, four XORs,
three ORs and an additional halfword mask before comparing that reduction.
The actual inlined timerhack function is at 0x83264ff0..0x832653e8; its baseline
disassembly is retained in `timerhack-baseline.asm` outside Git.

The candidate retains groups of four but compares each pair directly with an
early mismatch return. It asks the compiler to use equality branches instead
of keeping the reduction's intermediate values live. More conditional branches
may offset fewer arithmetic operations or reduced register pressure; a native
code observation is not an Xbox timing result.

Every opcode in an equal snapshot is still read and compared. A mismatch still
requests recompilation, with no change to the compared length, 0..3 tail handling,
source alignment, fetch/read-map checks or cache lifetime. Only UINT16 accesses
are used, with no type-punning, overread, new allocation, skipped writable-code
validation, code linking or instruction-emitter changes. Source memory is ordinary
mapped instruction storage, not device handlers; unsupported fetches remain guarded.

The four-way lookup, HOTMETA layout, CACHE-READY guard and inlined dispatcher stay
unchanged, as do guest cycles, timers, GPU, threads, audio and presentation.
The profile header changes only its build ID. Rejected GPU dependency scans,
256-page batches, split tag tables, extra render cores and CRT combinations are
not retried. Runtime diagnostics keep the existing three-file pause-only performance
summary workflow and existing random sampling; no new runtime instrumentation.

## Executed host regressions

Expanded the existing independent source-equality tests with every paired same-bit
mutation across distinct opcode positions and all halfword alignments, including
four-opcode boundaries. Also checked same-pointer equality. The expanded tests
passed on the original implementation before modifying production code.

Both baseline and candidate were run by the existing `tests/sh3_dispatch/run_inline.py`
runner in optimized and ASan/UBSan configurations. All four suites passed:

- 442729 exact source equality cases per run, including all 0..33 lengths, every
  individual bit, paired mutations, aliasing, alignment and guarded-page tails.
- 12000 random dispatch traces x 12 slices with identical state, memory, maps,
  callback order, compilation counts, emulated cycles and final tags/cursors.
- Cold/warm allocation failure/reset/reallocation checks.
- 228125 independent four-way probes and 1000000 replacement/reset operations.
- 2323712 production Block snapshot/metadata checks.

Reproduce from the repository root on a Linux host:

```sh
python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/salvia-source-compare-tests
```

Actual argv, exit codes and logs are in the evidence directory's `tests-before/`
and `tests-after/`. These host tests do not execute the generated PPC instructions
and are not a full-game replay or console frame-rate test.

## Build, mirroring and rollback

Only the comparator, build-ID header and source-validator test were mirrored from
this Git checkout to sibling `.work/Salvia`, after checking 17 related baseline
source hashes. `mirrored-files.json` records the exact reviewed files.
Build uses the preserved `.work/cpu-cache-ready-20260930-205053/git-baseline/tools/build_fbneo_checkpoint.cmd`
with the toolchain root argument. Project post-image copying is independently
verified after native build completion, not treated as validation itself.

Binary rollback is `.work/fbneo-before-source-compare.xex`, the exact INLINE-DISPATCH
baseline. Reverting this commit in the record checkout does not update sibling
`.work/Salvia` or its XEX: mirror the reverted source and rebuild, or restore the
verified binary. Raw logs, executable artifacts, SDKs, ROMs and keys stay out of Git.
Original input logs and optimization report are not modified.

## Native build and final identity

FBNeo core Rebuild, SDL Build and Salvia frontend Rebuild completed with exit
code 0. The project copied an uncompressed XEX because xextool was absent;
that warning is not a test failure. Build output is retained as `build.log`.

The native timerhack function is 0x83264ff0..0x832653e0 (1008 bytes, compared
with the baseline's 1016-byte address range). Full equal four-opcode iterations
were counted directly from the two disassemblies:

| Native loop | Baseline | Candidate |
| --- | ---: | ---: |
| Halfword loads | 8 | 8 |
| XOR + OR reduction operations | 4 + 3 | 0 |
| Mismatch branches | 1 | 4 |
| Instructions including loop maintenance | 23 | 21 |
| Run-loop stack frame | 208 bytes | 176 bytes |
| Saved nonvolatile GPRs | r18..r31 | r22..r31 |

The baseline loop runs from 0x832651ac to 0x83265204 inclusive; the candidate
from 0x832651ac to 0x832651fc inclusive. The candidate uses lhz/cmplw/bne pairs,
retains tail comparisons, and routes mismatches to the same compile path.
The 84-byte HOTMETA stride, source-map guards, cycle guard and native indirect
call remain in the inlined run loop. `native-comparison.json` records the counts.
This is static instruction analysis, not measured CPU cycles: extra branches,
load latency and compiler scheduling may offset fewer intermediate values.

The first dumpbin attempt used an unprefixed hexadecimal /RANGE and failed
with LNK1147. Adding the required 0x prefixes resolved the argument error;
bounded baseline and candidate disassembly then exited 0. It was not a platform
permission failure.

- Build time on the Runner: 2026-09-30 21:27:27.
- Build ID verified in native EXE: `cv1k-source-compare-20260930-r1`; prior ID absent.
- XEX size: 34611200 bytes; XEX2 header; build and distribution SHA256 match.
- SHA256: `f6e04cfa276db0bc2939a4ff9dcd19d39dfe2ae6028a71228cfd2a376a40a123`.
- Rollback SHA256: `843f4a254097762643a39330b4a7277f03d404bf8341e9a3d4b02888d5f42a52`.
- Eight host test executions and eight compiler invocations passed for the candidate.
- No new console FPS result, full-game replay or generated-PPC execution test is claimed.
- Exact commit, parent and input preservation results are recorded in the external
  `final-artifact.json` after this independent commit is created.

For the next test compare SOURCE-COMPARE with INLINE-DISPATCH under matching
saved state, settings and input. Keep the same pause-summary workflow. The
current noisy windows do not establish either CACHE-READY or INLINE-DISPATCH
as independently beneficial; this candidate must not be described as proven FPS gain.
