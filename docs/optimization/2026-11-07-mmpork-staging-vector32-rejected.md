# mmpork remaining staging: aligned 32-pixel row copy

Baseline source: `8a62efd` (functional `84989393`); clean before this experiment.
Baseline XEX: `983cbbaa1d264952f5ca437b082ee30932aba34b56467cd56287090756182c91`.
All three changed production headers matched the runtime source mirror before editing.
The first row-major-column change remains enabled and measured-positive.

## Admission and single change

The captured first active boss job contains 4,006 neutral opaque 32-pixel-wide
staging commands / 581,120 pixels. Of these, 3,823 commands / 193,376 pixels
meet both-side 16-byte alignment and same-row separation of at least 128 bytes.
This is 15.1% of all plain offscreen pixels, approximately 30% of the pixels
outside the preceding column optimization. Many other 32-wide destinations are
not aligned and continue to use the existing scalar loop.

Reuse the existing exact-width VMX rectangle-copy helper for this admitted
case, with 128-byte rows and the unchanged 32-KB VRAM pitch. The helper retains
row order, source prefetch and aligned vector stores. `_BurnMalloc` allocates
CPU VRAM with ordinary malloc; this is cached CPU memory. No cache-zero or
unaligned-store intrinsics are introduced. The same-row overlap rejection is
required because the prior forward qword loop propagates overlapping writes,
whereas the vector unroll loads the whole row before writing. Row-to-row aliases
remain legal because both implementations complete rows in the same order.

Reject zero/negative height, reversed Y, misaligned inputs and overlapping rows.
Keep flip, transparency, tint, command ordering, source dependencies, page
versions, guest pixel charging, 128-page batch cap and GPU shaders unchanged.
Pause diagnostics adds cumulative `staging_vector32 calls/pixels` counts only.
No extra timers or live-frame file writes are added.

## Validation and console protocol

`tests/epic12_gpu_region/run.py --toolchain-root
/home/humor/salvia-tests/toolchains/ppc/root --output <evidence>/vector32-tests`
passes on native ASan/UBSan and emitted AltiVec PPC under QEMU: 48 admitted and
744 rejected staging cases plus 4,044 existing rectangle cases; exact allocation
comparison includes alignment, direction and row aliases. Vector load/store
stubs model SDK semantics and validate alignment/bounds; this is not an XDK
instruction timing measurement.

All three fixed-save 180-frame replays pass: every video/audio hash and full
final state matches the original baseline (mmpork and both ddpsdoj saves).
The full PPC GPU suite must also exit zero before console deployment. Raw results reside under
`/home/humor/salvia-tests/mmpork-boss-dialog-20261107/vector32-*`.
QEMU does not measure Xenon cache or speed. Console comparison reuses the exact
previous 180-frame candidate as its baseline and tests only this additional copy
path. Verify new counters are nonzero and all guest work matches before comparing
worker time; two random samples cannot establish average FPS.

Build diagnostics only with task wrapper, baseline `8a62efd`, tag
`mmpork-vector32`, general FBNeo frontend only. Verify epic12.cpp recompiles,
linked PE contains tag/counter, archive identity and FTP readback SHA. The pair
contains the previous column-rows image and this candidate. No release acceptance
or dedicated ddpsdoj image replacement is authorized by test success alone.

Automatic benchmark uses the backed-up boss save, no input and 180 frames. Its
exit may power off the console; the user returns to Aurora for log retrieval.
Restore the original empty benchmark configuration and remove its done marker
after collection. Negative timing requires a separate revert commit and restoring
the verified baseline XEX on both runtime/deployment, not merely reverting Git.


## Console result: not retained

Candidate `52848677`, XEX SHA-256
`be44e1856c2e64b0cdf6cf9686dd071dd41c88ae7c83a9d2a241c6d44a39aa9b`,
completed the same 180 frames. The new path ran 173,517 calls / 10,444,736
pixels. All workload fields match the preceding row-columns run: 100 jobs,
25,319,953 dispatch calls, 1,973 timers, 1,691 column groups, 136,234 column
commands, 23,253,902 column pixels, 4,278 snapshots / 13,993,098 snapshot pixels,
and 99 measured GPU batches / 42,231,479 pixels. All mid-frame flush counts
remain zero. Full PPC GPU suite exited zero before deployment.

| Metric | Retained row-columns | Additional vector32 |
| --- | ---: | ---: |
| Worker busy, 100 jobs | 2647.162 ms | 2612.659 ms |
| GPU flush, 99 batches | 508.644 ms | 503.487 ms |
| Frames over 100 ms | 1 | 1 |
| Cold initial-frame peak | 279.279 ms | 276.849 ms |
| Peak among two random samples | 45.232 ms | 43.842 ms |

Worker busy improves only 1.30% (34.503 ms across 100 jobs). This small single-run
difference does not demonstrate a stable benefit worth retaining the additional
path; it is not a measured regression or a correctness failure. No full-run FPS
claim is made. Reject this experiment and retain the independently measured
48.87% first change.

`git revert --no-edit 528486775224f5fdc107761bf7a63ea7fa746812` produced revert
`99512531`. Restore all three production headers in the runtime mirror and the
archived row-columns XEX (SHA `983cbbaa1d264952f5ca437b082ee30932aba34b56467cd56287090756182c91`)
in the repository deployment folder, runtime deployment folder and console;
verify remote bytes by FTP readback. Preserve rejected binaries and raw logs in
the external archive. The local pair returns to original c511 diagnostics vs
row-columns diagnostics. No rebuild or release is needed to restore verified
archived bytes; the last linked PE/object files still represent the rejected
build and must not be mistaken for the restored deployment image.

`collect-vector32.py` retrieved the done marker and build-tagged log, checked
workload and restored the original empty benchmark configuration. Its marker was
removed. Continue with normal-play visual validation of the retained first
change. The issue is not marked resolved until that feedback is available.


Final status: the user subsequently accepted the restored first change with
remaining stutter on 2026-11-07. See [accepted handover](2026-11-07-mmpork-accepted-handover.md).
