# CV1000: sampled SH3 workload diagnosis before further optimization

Build ID: `cv1k-work-profile-20260930-r1`.
Baseline: `adf3b2537cb4a4475546b1b729d68941733a88ce` on `work/reversible-checkpoints`.
This is a diagnostic checkpoint, not a claimed frame-rate improvement. No automatic push.

## Incoming hardware evidence

The latest GPU log has 39 lines, all from SOURCE-COMPARE. Its last window (22-39)
has 3310 frames, 49 valid timing samples, ddpsdoj, 102400000 Hz, two render cores,
DIP 00,07,00,00 and bpp=4. No mixed build IDs were found.

| Last combat sample | INLINE-DISPATCH | SOURCE-COMPARE |
| --- | ---: | ---: |
| Frames / timing samples | 3110 / 50 | 3310 / 49 |
| cpu_io ms | 16.133 | 15.979 |
| core total ms | 17.057 | 16.896 |
| draw_sync ms | 0.776 | 0.778 |
| frontend sampled_active_loop ms | 18.516 | 18.604 |
| core sampled_peak ms | 21.379 | 24.586 |

The small core-mean reduction is not a demonstrated speedup: input/window lengths,
random sample positions and peaks differ, while frontend mean did not improve.
SOURCE-COMPARE remains the direct baseline, not a proven performance win. Prior
CACHE-READY and INLINE-DISPATCH micro-optimizations also lack controlled console A/B.
Further comparator/lookup rearrangement without workload evidence would be speculative.
No reciprocal of these samples is reported as measured FPS.

Async display remains active without fallback/timeout (GPU line 34); GPU fallback
is zero (30). GPU timing counters are cumulative, unlike pause-window phase timings.
The 15.979 ms cpu_io field includes devices and buffered sound, not just dispatch.
Audio line 2 still shows 126 underrun callbacks and time stretching; zero dropped
samples does not establish healthy audio. State log 1-8 ends load-done ok=1 for
151788652 bytes with about 20.4 MiB free; there is no evidence to enlarge caches.
Audio/state logs lack independent build identifiers; their association uses user context.

Raw logs, original optimization report, relevant baseline source, XEX, EXE and MAP
are byte-preserved under `.work/cpu-work-profile-20260930-214019/baseline/`, outside
Git. SHA256 manifests preserve provenance even if runtime logs are replaced later.

## One logical change: two compile-time execution variants

The SH3 outer run loop and dispatcher now instantiate an ordinary `Count=false`
variant and an explicitly selected `Count=true` variant. The latter counts workload;
it does not use per-instruction or per-block timers. The ordinary path contains no
runtime Count branch in the inner loop after constant folding. A CV1000 driver branch
chooses the variant once per frame, outside its unchanged 240-slice schedule.
Ordinary `Sh3Run` still selects the ordinary runner. Non-Xbox drivers do not opt in.

The existing timing PRNG supplies both selectors without a second random generator:
phase timing is approximately 1/64 frames, while workload counting selects the
low-byte value 1, approximately 1/256 frames. These sets are disjoint. Work counting
is selected only with timer batching and DRC enabled. A shared emulation-thread flag
also prevents the frontend from recording a workload frame as a timing sample if
its sampling stream differs. `work_count_skipped` reports such exclusions.

Instrumentation is low frequency, not free: selected frames perform integer
bookkeeping and may take longer. Code layout and neighboring-cache effects remain
possible even when their timing samples are excluded. This version is for workload
identification, not an FPS comparison or a promise of zero performance impact.

The profile occupies 2472 bytes on the inspected host; target layout is checked by
the native build. No large cache, atlas, queue or new heap allocation is introduced.
The PPC emitter, guest-state layout, cache replacement policy, complete source
validation, guest CPU frequency, timers, GPU algorithms, audio algorithms and
save-state format are unchanged. Rejected TAGTABLE, precise GPU dependency scans,
256-page batching, additional render cores and CRT combinations are not retried.

## Reading the pause report

All new records are emitted via the existing `cv1000-gpu.log` pause callback.
No fourth runtime log and no gameplay file I/O is introduced.

- `drc_work`: sampled frame/slice counts and CPU-off or normal-mode exceptions.
- `drc_work_dispatch`: C++ entries, lookup attempts, rebuild calls, attempted
  validation spans and requested source words. Requested words are NOT measured
  loads: an unequal comparison may stop early.
- `drc_work_execution`: native block calls and interpreter steps, plus consumed
  **guest** cycles, not host milliseconds or proportions of host CPU cost.
  Interpreter cycles include the loop's EAT(1) and any IRQ handling within the step.
- `drc_work_exits`: allocation/budget gate, unsupported/odd fetch, no native entry,
  insufficient block budget, partial native fallback and normal boundary returns.
- `drc_work_snapshot_lengths`: requested snapshot length distribution at native
  entry; not dynamic executed instruction counts. Conditional exits may execute
  fewer instructions; a 33-word snapshot can include lookahead.
- `drc_work_interpreter`: eight most frequent high-byte opcode families, counted
  only when actually dispatched to the interpreter. A family is not a complete
  opcode, and does not by itself distinguish I/O guards from unsupported instructions.

Counters reset after reporting and at CV1000 initialization, so they cover the same
pause window but a DIFFERENT sample population from core_frame_ms. Do not subtract
or divide these counts into host timing to infer an exact instruction cost. They
will guide whether to inspect short blocks, native coverage, validation volume,
recompilation churn or specific fallback families next.

## Executed verification

The unchanged baseline passed the prior four optimized/ASan-UBSan host suites.
The candidate's five suites passed in optimized and ASan/UBSan builds:

- 12000 randomized dispatcher traces x 12 slices compare reference, ordinary,
  single-block and counted variants, including memory, mappings, guest cycles,
  callback order, compile counts, tags/cursors, IRQ/delay gates and allocation lifetime.
- A warm ordinary entry leaves every profiling byte untouched; a counted repeat
  increments source-validation count exactly once; histogram totals match native calls.
- Existing 442729 exact source checks, four-way lookup tests and 2323712 Block checks.
- Exhaustive selector checks and one million sampled frames: zero timing/workload
  collisions; full reset and 64-bit counters checked.
- Additional tests extract the actual new outer-loop function and the preserved
  baseline function. With synthetic interpreter/native/timer callbacks, 12000 seeds
  x 12 slices have identical baseline/ordinary/counted states, memory and cycles;
  interpreter counts and cycle accounting match independently recorded values.

An initial new test incorrectly assumed a long linear stream must contain cache
hits. The tiny 32-entry test cache is overrun by its 512-PC stream, so zero hits is
valid. The assertion was replaced by an explicit repeated warm-PC test, not by
removing coverage. Initial failure is retained in `tests-after/`; corrected output
is in `tests-after-fixed/`. Outer-loop outputs are in `outer-tests/`.

Reproduce from a Linux checkout:

```sh
python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/cv1k-work-tests
python3 libretro/FBNeo/tests/sh3_work_profile/run_outer.py \
  --baseline-sh4 /path/to/preserved/pre-instrumentation/sh4.cpp \
  --output /tmp/cv1k-work-outer-tests
```

These are real source with synthetic callbacks and data tests, not generated PPC
execution, full DDPSDOJ replay, target profiling-overhead measurement or console FPS.

## Build, history and rollback

Reviewed files are mirrored from the Git checkout to sibling `.work/Salvia` with
explicit path/hash manifests. Build uses the preserved
`.work/cpu-cache-ready-20260930-205053/git-baseline/tools/build_fbneo_checkpoint.cmd`.
The original 128-source-page GPU batch limit and 256-slot atlas remain unchanged.

One local diagnostic commit contains source, tests and this report. Revert that
commit, mirror the reverted source/deletions into `.work/Salvia` and rebuild; Git
revert does not replace the runtime XEX. Binary rollback is
`.work/fbneo-before-work-profile.xex`, SOURCE-COMPARE SHA256:
`f6e04cfa276db0bc2939a4ff9dcd19d39dfe2ae6028a71228cfd2a376a40a123`.

## Native verification and artifact

FBNeo core Rebuild, SDL Build and Salvia frontend Rebuild finished with exit 0.
Native dumpbin calls succeeded and the linked EXE contains the new build ID,
`drc_work_execution`, `drc_work_interpreter`, and `work_count_skipped` markers.

- Ordinary `Sh3Run_timerhack_impl<false>`: 0x83265440..0x83265830, 1008-byte
  address range. Its 251 decoded instructions have the exact same opcode and
  register-operand sequence as the SOURCE-COMPARE baseline at
  0x83264ff0..0x832653e0. Relocation addresses differ. This verifies that the
  ordinary hot loop did not acquire runtime counter branches/stores; it does
  not establish identical cache behavior or wall-clock performance.
- Counted `Sh3Run_timerhack_impl<true>`: 0x83265830..0x83265d88, 1368 bytes.
  It is a separate native function selected through Sh3WorkRun only for work frames.
- The ordinary runner retains r22..r31 saves and its 176-byte frame. The counted
  runner saves r21..r31. Do not benchmark the latter as an optimization.
- Native Sh3WorkReset explicitly clears 2472 bytes (li r5,2472), confirming the
  target counter storage size independently of the host struct.
- Frontend/core shared flag links successfully. The foreground loop still has
  a new per-frame flag store/selection check; no zero-overhead claim is made.
- Build time on the Runner: 2026-09-30 21:50:09.
- Output: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.
- Build ID: `cv1k-work-profile-20260930-r1`.
- XEX2 header, 34611200 bytes; build and distribution SHA256 match.
- SHA256: `03f85a24adb2a10924480f8a9157c1ee710578976b6d4689fb94211d68ba6584`.
- Twelve successful host test executions (five suites in two modes, plus two
  extracted-outer-loop runs) are recorded; the initial fixture-assumption
  failure remains archived separately and was resolved with an explicit warm-hit test.
- Evidence: `native-functions.json`, `native-0.asm` through `native-3.asm`,
  `normal-baseline.asm`, `native-normal-comparison.json`, `profile-reset-native.asm`,
  test results, source hashes and final-artifact manifest in the evidence directory.

No generated-PPC differential execution, full game replay, target instrumentation
cost or new console frame-rate result is claimed. Use the same saved state, settings
and battle segment, pause after loading to separate that window, and pause again
after the battle to emit the new workload summaries. Keep the existing three files.
