# CV1000: exact short-snapshot validation candidate

Build ID: `cv1k-short-snapshot-20260930-r1`.
Baseline: `593f7336a4bf251f7698c671be2808b5f00c02e3` (ASYNC-RECOVERY).
Branch: `work/reversible-checkpoints`. No GitHub push requested.

## Latest console input

The supplied GPU log is 75 lines, all `cv1k-async-recovery-20260930-r1`.
Raw logs, original review, affected sources, XEX, EXE and MAP are preserved at
`.work/cpu-short-snapshot-20260930-225146/baseline/` with a SHA256 manifest.
The last pause window is GPU lines 40-75: 2628 frames, 42 timing samples and
12 separate workload-count frames. Settings remain ddpsdoj, 102400000 Hz,
2 render cores, DIP 00,07,00,00, bpp=4.

| Last pause phase | Mean ms |
| --- | ---: |
| cpu_io | 14.745 |
| audio_tail_misc | 0.169 |
| draw_sync | 1.349 |
| core total | 16.266 |
| frontend sampled_active_loop | 18.745 |
| core sampled_peak | 34.473 |

This window includes a display-mode transition. First pause: active=1,
activations=1, fallbacks=0. Last pause: active=1, failed=0, activations=2,
fallbacks=1, timeouts=0, forced=1, attempts=1, last_epoch=1, ui_state=0.
These fields support one successful recovery from forced synchronous mode;
they do not establish a general FPS increase. The 297 normal-mode GPU batches
include only ONE timing sample. Do not treat that sample as a stable mode cost,
subtract cumulative GPU timing from the frame timings, or attribute changes
in the mixed window exclusively to CPU code. The recovery fix is retained.

The audio log still reports underruns/time stretching. The state load completed
with ok=1 and 21327872 bytes available. Neither a memory leak nor audio health
can be inferred merely from these totals.

## Evidence for this one candidate

GPU lines 44-50 report 461464 native calls over 12 counted frames. Of these,
278764 have requested validation snapshots of 0..8 halfwords (60.4086%).
This is the distribution of snapshots for executed native blocks, not every
validation attempt, host CPU time, or the number of guest opcodes executed.
Full source validation must remain in place even when no writes were observed.

Add a bounded switch/fallthrough path for snapshots of up to eight halfwords.
It compares exactly the requested halfwords without group-loop maintenance.
Zero words dereferences neither pointer. Longer snapshots retain the existing
four-halfword loop and tail code byte-for-byte. Both operands remain UINT16
objects with only halfword alignment required; there are no wide or out-of-range
loads. Short comparisons proceed from the end of the snapshot; these are
ordinary mapped instruction RAM, not volatile/MMIO reads. A mismatch still
requires the same recompilation, and an equal snapshot checks every opcode.

The runtime production differences are the source-check helper and build ID.
Dispatcher, HOTMETA layout, four-way table, allocator, PPC generator, memory
maps, interpreter helpers, guest timing, GPU batching/shaders, recovery, audio,
thread assignment and sampling are unchanged. Rejected TAGTABLE, 256-page
batches, precise GPU hazards, extra cores and CRT combinations are not retried.

Tradeoff: a length check and short-path branch targets can increase code size,
branch-prediction pressure and the long-path entry cost. Native inspection and
console comparison are required; fewer source-level loops do not prove speed.

## Executed host regression

The same extended tests ran before and after the production helper change.
Five suites passed in optimized and ASan/UBSan builds, ten test executions per
revision. No sanitizer category was disabled in these suites.

- Source equality: 458809 cases per execution, including every mismatch subset
  for 0..8 words, offsets within an eight-byte boundary, threshold 8/9,
  long lengths through 127, zero-length null pointers, paired bit mutations,
  and mapped-page start/end guards for all production lengths 0..33.
- Dispatcher: 12000 random traces x 12 time slices, mutable source and mappings,
  partial exits, delay/IRQ gates, short budgets, allocation lifetime and resets;
  counted and ordinary modes preserve states/cycles and profile accounting.
- Four-way lookup: 228125 independent probes and 1000000 replacement/reset probes.
- Block layout: 2323712 production snapshot/metadata checks.
- Work-profile selector: disjoint samples, wide counters and complete reset.

Reproduce from the repository using the existing runner:

```sh
python3 libretro/FBNeo/tests/sh3_dispatch/run_inline.py --output /tmp/salvia-short-snapshot-tests
```

These are host correctness/control-flow tests with synthetic native callbacks,
not generated PowerPC execution, a full game replay or measured console FPS.
`tests-before/` and `tests-after/` contain compiler argv, exit codes and outputs.

## Build and rollback

Mirror only the reviewed source/test paths from this Git checkout to sibling
`.work/Salvia`; compare all baseline and post-copy hashes. The existing build
wrapper is `.work/cpu-cache-ready-20260930-205053/git-baseline/tools/build_fbneo_checkpoint.cmd`.
It takes the toolchain root and rebuilds FBNeo, checks SDL, then rebuilds the
Salvia frontend. Post-image steps may copy XEX before final identity checks.

Rollback binary: `.work/fbneo-before-short-snapshot.xex`, the direct ASYNC-RECOVERY
baseline with SHA256 `8fc88d5337915c33f6f0374df8a1931e1a62e12b7cd568c3f2fa9ad2a72889bc`.
Use `git revert <this-performance-commit>`, mirror reverted source and rebuild
for a source rollback; Git revert alone cannot update an existing XEX.
No raw runtime logs, SDKs, keys, ROMs or binary products are staged.

No new runtime logging or sample frequency changes: pause-time summaries still
use the same three files. For a cleaner performance comparison, load the same
state, close the system UI, let async recover, then pause once to separate that
transition window before running the combat segment. Compare with the direct
ASYNC-RECOVERY baseline, not a previously rejected experiment.

## Native build and final artifact

FBNeo Rebuild, SDL Build and Salvia frontend Rebuild completed successfully.
The candidate EXE contains the correct build ID and the build/distribution XEX
hashes match. The candidate was built at 2026-09-30 22:56:53, size 34611200,
SHA256 `90d6ab91ffbf5dd2f143edb6e2c65ee0c1924b11d625ce723f4fe87bea4022e0`.
This is an inspected experiment, NOT the final recommended console binary.

Native disassembly shows a compare-to-8 guard followed by a sequence of
CTR-decrement branches rather than a cheap constant-time length dispatch.
The ordinary execution function grows from 1728 to 1960 bytes and the counted
function from 2080 to 2312 bytes. Both keep the 176-byte stack frame and
r21..r31 saved-register range. Longer comparisons keep the old four-word loop,
but add an entry guard. Short equality comparisons have no back edge.

A bounded static equal-data control-flow walk from the snapshot-length load
to the read-map flag, including branch instructions actually taken, finds:

| Snapshot words | Baseline instructions | Candidate instructions |
| --- | ---: | ---: |
| 1 | 16 | 21 |
| 2 | 22 | 26 |
| 3 | 28 | 31 |
| 4 | 36 | 36 |
| 5 | 37 | 41 |
| 6 | 43 | 46 |
| 7 | 49 | 51 |
| 8 | 57 | 55 |
| 9 | 58 | 60 |

All sampled lengths above eight add two instructions on this path. Weighting
these static paths by the supplied native snapshot histogram gives 58.717 vs
60.873 instructions per validation path. This is NOT hardware cycles, a cache
simulation, generated-PPC execution testing or measured FPS. Nevertheless it
fails the candidate's intended native hot-path reduction for most observed
lengths while expanding code, so there is insufficient reason to request a
console test. Static correctness checks alone are not a performance pass.

## Decision: rejected before console testing

Record this isolated experiment with tests/review in its own commit, then use
a separate `git revert` commit to restore the parent. Preserve both commit IDs
and all original/candidate binaries outside Git. Mirror only the reverted paths
back to `.work/Salvia`. Restore the byte-verified ASYNC-RECOVERY distribution
XEX from `.work/fbneo-before-short-snapshot.xex`; do not ask the user to run the
rejected SHORT-SNAPSHOT build. Restore the saved EXE/MAP and matching build XEX
for identity consistency. Rejected build intermediates must not be used as an
incremental baseline; the next change must run the existing full core/frontend
Rebuild wrapper. The candidate remains available only in the evidence archive.

No console performance regression is claimed as measured. This decision avoids
another poorly supported console experiment, and the failed native lowering is
now recorded so the same switch specialization is not retried unchanged.
The exact candidate/revert SHAs and post-rollback checks are written into the
external `final-artifact.json` after completing the recorded revert.
