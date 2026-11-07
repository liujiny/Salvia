# ddpsdoj: synchronize blended CPU staging with the GPU atlas

Baseline: `dbd3a4dff4baf621505aedad8dcd2126d2781f68`.

## Root cause reproduced from both console saves

These are two distinct gameplay scenes, not an A/B pair of game states:
slot0 is `ddpsdoj.state` (saved frame 22802), slot1 is `ddpsdoj.state1`
(saved frame 7106). Compare each replay only against the original CPU renderer
started from the SAME save, with no input. The opening after aircraft selection
is a third reproduction and is not asserted to be represented by either save.

After admission, `epic12_gpu_submit` performed blended off-screen writes by
calling CPU replay directly. That bypassed atlas invalidation, source-version
updates, and ordering barriers. A later GPU draw could read an already-cached
old staging page or reuse a snapshot made before the write. It could also
read before an earlier GPU producer had finished. Therefore zero rejected
blend commands/zero GPU-render failures do not prove this path coherent.

A per-batch original-rasterizer oracle locates the first baseline RGB difference
in both scenes at their fourth replayed frame:

| scene | frame | pixel | expected | GPU model |
| --- | --- | --- | --- | --- |
| slot0 | 22805 | 44,32 | 20b8d8b0 | 20d8f8b8 |
| slot1 | 7109 | 32,32 | 209898b8 | 209898b0 |

Both batches sampled a 320x240 staging surface at source (7872,32), after CPU
compositing. The component shader arithmetic agreed with the original pixel
rasterizer; the texture data were stale.

## Change

Before a blended CPU staging draw, finish a pending GPU producer if its
conservative destination bounds intersect the CPU source. Reuse the existing
CPU-upload write barrier to finish any pending consumer of the destination,
and invalidate its resident atlas pages. Keep disjoint batches pending; this
adds no new per-command precise dependency scan. Update staging versions for
CPU writes so snapshots made earlier in the same batch cannot be reused after
an update. The 128-source-page limit, shaders and guest CPU timing are unchanged.

Also align the CPU model's cropped-command buffer with the production command
array (4096). The old model held only 1024 commands; busy saved-state replays
could corrupt its diagnostic counters. Results produced with that old fixture
are not conclusive for large batches. The temporary per-batch oracle copies
the full 4352-row VRAM/snapshot allocation and preserves the blitter delay, and
is retained outside Git, not installed in the runtime.

## Validation

The native GPU suite passes, including independent tests for a warmed stale
atlas page, two different snapshots across an intervening CPU staging write,
retention of a disjoint pending batch, a GPU-producer/CPU-reader dependency,
and a full 4096-command crop/reorder batch. The prior commit's 73728 original
source-mode replay comparisons remain in the suite. The saved-state batch
oracle passes the first five frames of BOTH scenes, including both formerly
failing frames. Extended replay and PPC GPU suite results are retained under
`/home/humor/salvia-tests/ddpsdoj-takeover-20261107/`; the final evidence summary
records exact results rather than inferring Xenon timing from QEMU.

## Image identity and console acceptance

Diagnostics tag: `ddpsdoj-staging-coherent`; source is this commit. Build with
`build-xex.sh --baseline f9b2738a --tag ddpsdoj-staging-coherent --flavor diag --no-deploy`.
The generated `artifact-ddpsdoj-staging-coherent.json` records exact SHA-256,
source SHA and archive paths. Verify epic12.cpp rebuilds after the header mirror.
Compare the original f9b2738a dedicated image with this candidate in the handoff
pair. Console validation must cover each saved scene separately and the cold
start/aircraft-selection entrance. Pause-time `core_stall`/`frontend_stall`
records can identify residual long stalls; no console FPS or entrance-stall
elimination is claimed by these host checks. An extra required dependency
flush can change performance; acceptance requires console measurement.

Rollback restores the verified original XEX bytes as well as the source revert.
No saved game, ROM, SDK, raw runtime log or generated binary is committed.
