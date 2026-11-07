# mmpork boss dialogue: row-major opaque staging columns

Baseline repository HEAD: `5f685238` (documentation only after accepted functional
source `c511cd159101a34560f0cff99361b4698563485e`). Clean before this experiment.
The running/archived general diagnostics image is byte-verified SHA-256
`0d08a0e5a8616e04a07f190c3bf9ec25736bdfa8bb6eaa0db90e27d5da1d4618`.
All reviewed runtime paths matched that baseline before editing/mirroring.

## Evidence and admission

Raw console log and the newly saved `mmpork.state` (saved frame 12913) are under
`/home/humor/salvia-tests/mmpork-boss-dialog-20261107/`, outside Git.
Console peak frames have roughly 1.8 ms CPU/I/O, no new compiled blocks and
120-123 ms draw/synchronization. In the 44-frame window, 15 worker jobs take
1748.488 ms, while their GPU flush spans total 155.769 ms. The intervening
source-page, region and source-dependency flush counts are all zero. Snapshot
counts in the handover are cumulative, not a 44-frame workload. Increasing the
batch bounds or re-vectorizing the already-vectorized rectangle-copy helper
is not supported by these records.

A deterministic no-input replay captures the full first active boss-dialogue
command list. That job has 7861 plain off-screen commands / 1276928 pixels.
Among them are thousands of 2-pixel-wide columns, mostly 84, 94 and 103 rows
high, whose destinations advance by ONE pixel. Each old column traversal
jumps 32768 bytes between adjacent rows. The old horizontal fusion cannot
serve these overlapping columns or their varying source X/Y coordinates.

The admitted prefix groups cover 3501 commands / 639942 pixels in 48 groups.
All actual admitted sources are disjoint from their combined destinations.
This is exact workload coverage, not a measured cache-miss rate or console
speedup. The old and candidate model retain the same GPU batch, snapshot,
texture-upload and guest workload counts.

## Single candidate

Collect 4..128 consecutive neutral, opaque, non-flipped two-pixel columns with
common destination Y/height and destination X advancing exactly one pixel.
Require full clipping, valid command/VRAM bounds and source/destination bounding
boxes that are disjoint. All other cases use the existing stream path.

Execute each admitted group a row at a time. Every second pixel except the
last is overwritten by the next opaque column, so emit the final first pixel
of each command plus the final second pixel. There are no read aliases or
observable GPU commands between the admitted CPU operations. This retains
final memory contents while keeping rows hot and removing overwritten stores.

Retain the existing GPU producer/consumer barriers and original per-command
staging page-version increments. Charge `commands * 2 * height` guest pixels,
not the smaller union or reduced number of actual CPU stores. Keep clipping,
blitter timing, the 128-source-page GPU cap, shader arithmetic and DRC checks.
Only diagnostics adds cumulative `staging_column_rows groups/commands/pixels`
counters, printed at pause; no new running-frame file I/O or instruction clocks.

## Correctness checks

- The new standalone C++98 suite passes 512 generated row/column-order and guard
  comparisons on native host, ASan/UBSan and real emitted PPC execution under
  QEMU. It checks flags/tint, clipping, aliases, bounds, short groups and capacity.
- GPU integration compares a 100-column run against the original sprite
  rasterizers and checks consumed commands, exact page versions and guest delay.
- Same boss save, no input: 60- and 180-frame row-major checkpoints match every
  video/audio hash and the complete final state. The final single-store kernel
  also matches the 180-frame baseline state SHA-256
  `7fa1476b7f6cd6579645501bd59d2d027025d51994a3ad20a6750d8f63c7b1db`.
- Both accepted ddpsdoj scene saves also match their 180-frame video/audio hashes
  and whole states with the final kernel; hashes remain
  `d69b58404e12c4df694ffc026c36b0ee9078dcbc085372983825ec2e6bcde403` and
  `b1d70f125ee5414bde0d4ec67143094c3a38a476ea2faf4609663b3afaeae444`.
- The original row-major GPU suite passed native/PPC. The final complete PPC
  run is retained in `gpu-final-ppc.log` and must exit zero before deployment.
  Do not substitute host/QEMU run time for a Xenon timing result.

Reproduce the new kernel tests with
`python3 libretro/FBNeo/tests/epic12_staging_columns/run.py`, `--sanitize`, or
`--toolchain-root /home/humor/salvia-tests/toolchains/ppc/root`.
The usual `tests/epic12_gpu/run.py --toolchain-root ...` covers full integration.
See `final-host-comparison.json`, `column-admission.json` and the raw replay
logs in the evidence directory. The tests/builds do not establish real-console
FPS, end-to-end relief or preservation in every CV1000 game.

## Console A/B and build identity

Iteration flavor is diagnostics only, tag `mmpork-column-rows`, source this
commit. The task-local `build-reviewed-xex.sh` uses the existing VS2010/XDK
wrapper, source verification, flavor checks and archives, but builds only the
general FBNeo frontend (no unrelated dedicated game images). Verify epic12.cpp
recompiles after mirroring both changed headers and the new kernel header.
Artifact hashes are in `xex-build/artifact-mmpork-column-rows.json` after build.

The handoff pair is under `Distro360/mmpork-dialog-ab/`: original accepted
`fbneo-mmpork-dialog-base-diag.xex` and `fbneo-mmpork-dialog-cand-diag.xex`.
No release is built or accepted yet. Do not overwrite the accepted dedicated
ddpsdoj image for this experiment.

Load the same backed-up slot0 boss save, use no input, pause during the dialogue
and collect the build-tagged log. Match frame/job/pixel work across variants;
GPU counters are cumulative while core timing windows reset on pause. Check
that the new column counter is nonzero and assess worker time / draw_sync /
all-frame peak, not random samples alone. A 100-ms watchdog does not prove
there were no 25-ms frames. The fixed-save console results are recorded below; normal-play acceptance is still pending.

If timing is worse or the change is rejected, make a separate revert commit
with the measurements and restore the verified baseline XEX. A Git revert
alone does not update the Windows mirror or the console executable.


## Fixed-save console result (180 frames, 2026-11-07)

Both images completed the automatic benchmark and wrote `done frames=180`.
The same launch via the general frontend was used. On this console `exit(0)`
powered the machine off; the user rebooted into Aurora to retrieve the logs.
The temporary benchmark configuration was restored to its original empty bytes
and the completion marker removed after collection.

Work is identical: 100 worker jobs, 25,319,953 dispatch calls, 1,973 timer callbacks,
364,387 staging-ident commands, 1,457 staging replay commands, 17,716 onscreen
commands, 4,278 snapshots / 13,993,098 pixels and 42,231,479 measured GPU pixels.
There are no source-page/region/dependency mid-frame flushes in either run.

| Metric | Baseline | Row-major candidate |
| --- | ---: | ---: |
| Worker busy, 100 jobs | 5177.217 ms | 2647.162 ms |
| GPU flush, 99 measured batches | 492.001 ms | 508.644 ms |
| Frames over 100 ms | 20 | 1 |
| All-frame peak (cold initial draw) | 348.630 ms | 279.279 ms |

Worker busy drops 48.87%. The remaining over-100-ms frame is initial drawing,
not compilation (zero compiled blocks). Candidate counters confirm 1,691 column
groups / 136,234 commands / 23,253,902 charged pixels. Two random core samples
show a 45.232-ms maximum; two samples do not establish a whole-run average FPS
or rule out other slow frames. The first change is measured-positive and retained,
but the original smooth-play problem is not yet declared resolved.

Candidate XEX SHA-256: `983cbbaa1d264952f5ca437b082ee30932aba34b56467cd56287090756182c91`.
Raw `baseline-console.log`, `candidate-console.log`, matching summaries and done
markers are retained in the external evidence directory.


The independent aligned vector32 follow-up was reverted in `99512531` after
only a 1.30% single-run difference; see
[its rejection record](2026-11-07-mmpork-staging-vector32-rejected.md).
The restored normal-play image remains the row-columns diagnostics SHA above.
