# ddpsdoj: finish GPU producers before non-blended CPU staging reads

Baseline: `3a3c08aa41a409c6ff54a131eaa28b7ca40ba0d9`.

The previous cache/coherence fix makes slot0's 180-frame replay byte-identical
to the original CPU renderer, including the complete 151788648-byte state.
Slot1 agrees for 168 frames, then diverges in video only at frame 7274 and the
following 11 frames. A per-batch original-rasterizer oracle passes those GPU
batches, separating the residual error from shader arithmetic and atlas replay.

The remaining direct and fused-stream CPU staging paths checked pending GPU
consumers of their DESTINATION, but not pending GPU producers of their SOURCE.
A non-blended copy of the on-screen surface could therefore copy old pixels
into off-screen staging and faithfully render those wrong pixels later.

Add a conservative source-rectangle vs pending GPU destination-bounds barrier
in both staging entry points. Repeated-source stream fusion checks the original
source width; contiguous fusion checks the whole source span. Before flushing
a producer inside the stream, invalidate any earlier accumulated CPU writes
so the GPU can see them. No new per-command dependency scan is introduced.
Keep the existing write barriers, batching limits, fusion policy and shaders.

The native GPU suite passes new independent queued-producer tests for both
direct and streamed non-blended copies, in addition to the blended tests and
73728 original-rasterizer source-mode checks. The old snapshot WAR fixture used
the pending viewport as its CPU source, conflating a real read dependency with
an absent write hazard; use an unrelated off-screen source for that WAR test.
Dedicated tests now cover the required viewport-read flush explicitly.

Slot1 now matches the original CPU renderer for all 180 frames' video/audio
hashes AND the entire final 151788648-byte state. Slot0 is rerun on the final
code. Final native/PPC/sanitizer results and both scene comparisons are written
to `/home/humor/salvia-tests/ddpsdoj-takeover-20261107/final-evidence.json`.
The saves represent different scenes and are never compared against each other.
These are correctness results, not Xbox 360 FPS or a verified entrance replay.

Diagnostics image tag: `ddpsdoj-staging-read-fix`, source is this commit.
The task-local `build-reviewed-xex.sh` is a copy of the existing build wrapper
that builds the general frontend and dedicated ddpsdoj frontend only (skipping
unrelated mmpork). It preserves an already-identical diagnostics-header mtime;
flavor checks, source hashes, compiler/toolchain, build tags, archives and
artifact manifest checks are retained. The unchanged original build wrapper
can also build this source. Verify epic12.cpp appears in the final core log.
The generated `artifact-ddpsdoj-staging-read-fix.json` records exact image hashes.

Restore the verified original XEX bytes for console rollback. Use both saves
separately, and test the cold-boot aircraft-selection entrance separately.
Pause once after each reproduced symptom so the all-frame stall watchdog from
f61421f6 can distinguish execution stalls from stale displayed frames. Additional
required GPU producer flushes may affect performance; console acceptance is
still required. Only diagnostics is built; release awaits user acceptance.
