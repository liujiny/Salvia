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
