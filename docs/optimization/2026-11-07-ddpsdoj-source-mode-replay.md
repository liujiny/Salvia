# ddpsdoj: preserve source blend modes during CPU replay

Baseline: `f61421f6eff2bf1f42a05403591c4e9332174467` (stall-watch diagnostics),
with rendering behavior inherited from `f9b2738a`.

## Reproduced defect

The recent GPU admission changes accept source modes 1 (square) and 2
(destination modulation) with destination modes 0, 3 and 7. However,
`epic12_gpu_replay` dispatched primarily on destination mode. Mode 1/destination 0
fell into the source-alpha/reverse-destination-alpha rasterizer, and both source
modes with destination 3/7 fell into source-mode-0 rasterizers. This path also
serves complex off-screen staging draws, so zero GPU-render failure counts do
not rule out incorrect CPU-produced source pages.

An independent one-pixel comparison against the original rasterizer function
tables reproduces five mismatches before the change:

| source mode | destination mode | expected RGB pen | old replay pen |
| --- | --- | --- | --- |
| 1 | 0 | 204098d8 | 2080f8f8 |
| 1 | 3 | 2078f8f8 | 20b0f8f8 |
| 1 | 7 | 2078f8f8 | 20b0f8f8 |
| 2 | 3 | 2078f868 | 20b0f8f8 |
| 2 | 7 | 2078f868 | 20b0f8f8 |

## Fix and validation

Dispatch admitted source modes 1/2 to their corresponding original rasterizers,
retaining flip, transparency, tint and destination mode. Preserve the existing
fixed-alpha mode-0 fast path. No shader, batching, memory limit, guest timing or
CPU execution changes are part of this fix.

Add 73728 independent original-rasterizer comparisons covering both source
modes, all three admitted destination modes, flips, transparency, neutral and
non-neutral tints, and every source/destination alpha pair. The full native GPU
suite including these checks passes. PPC GPU suite and saved-state comparison
results are retained in `/home/humor/salvia-tests/ddpsdoj-takeover-20261107/`.

The user's slot0 (`ddpsdoj.state`) and slot1 (`ddpsdoj.state1`) were downloaded
without modifying the console saves. Their gzip payloads contain a big-endian
frame number, the exact host area sequence and an 8-byte empty RCHV trailer.
Replays use the original files' frames 22802 and 7106, no input, and compare
video/audio hashes against the original CPU renderer. A host model's graphic
correctness or instruction counters cannot establish Xenos performance or
prove the separate aircraft-entrance freeze eliminated. The diagnostic watchdog
from the previous commit remains available for that console measurement.

Build identity: diagnostics only, tag `ddpsdoj-source-replay-fix`, produced by
`/home/humor/salvia-tests/xex-build/build-xex.sh --baseline f9b2738a --tag ddpsdoj-source-replay-fix --flavor diag --no-deploy`.
The generated artifact JSON under `xex-build/` records source SHA, exact hashes
and archive paths. Ensure epic12.cpp is rebuilt after mirroring its header.
Hand off the verified original ddpsdoj image and the candidate only in the A/B
folder. Rollback means restoring original XEX bytes as well as reverting source.
