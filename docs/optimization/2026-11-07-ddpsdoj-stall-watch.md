# ddpsdoj: preserve rare stall evidence before changing precompilation

Baseline: `f9b2738a3cf1f4aa2b5959444888b9e2c95f360d`, clean `main` checkout.
The runtime DRC header matched baseline SHA-256 before editing.

## Evidence and corrected handover assumptions

Console log fetched by FTP before changing the image is retained outside Git at
`/home/humor/salvia-tests/ddpsdoj-takeover-20261107/console-before.log`.
Its latest ddpsdoj window identifies f9b2738a: 2709 frames, 25474 generated
blocks, zero arena evictions, zero sampled source/read-map rebuilds. Its peak
86.781 ms is sampled, so it does not exclude an unsampled multi-second stall.
The reported 85.843 ms is CPU/I/O, whereas AOT runs before the CPU/I/O mark.
Thus this sampled frame is not evidence of an 85 ms AOT pass.

A baseline QEMU PPC cold boot/replay uses the existing local ROMs, inputs and
cv1000-boot harness. The copied harness adds only per-frame compiler counters
and prologue counts; it does not change emulation or precompilation logic.
At frame 46 and frame 240 the first four RAM MiB already contain
144/2291/16859/681 prologues, nearly identical to the handover's gameplay state.
The claim that only a minimal bootloader is present at frame 46 is contradicted.
The active-low coin condition `(DrvInputs[0] & 0x0c) != 0` fires when either coin
is released; this harness consequently runs AOT at frame 11, not frame 30.
The workload is a reproducible harness replay, not the user's exact input trace.

The queue marks dropped entries visited, the AOT counter increments twice,
and repeated AOT lacks full byte validation. These defects are real, but this
change deliberately does not enlarge the speculative workload or claim that
any of them caused the observed aircraft entrance freeze. The user reports
that music continues while the screen freezes.

## Change

Keep the random 1-in-64 timing averages and disjoint workload samples.
In diagnostics only, read clocks at core and frontend frame boundaries on
every running frame. Store the worst eight frames strictly above 100 ms in a
fixed array, with their coherent phase breakdown and frame number; core events
also store the exact delta of the existing generated-block counter, including
AOT. Frontend coverage starts before processFrontendEvents and separates it
from game/UI, present and limiter time. No allocation or file I/O is added to
running frames, no per-instruction clocks are added, and GPU limits are unchanged.
Only the existing pause report writes and clears the collected events.
Boundary-clock overhead is additional diagnostic cost; performance acceptance
must use the same diagnostic configuration. This is a diagnosis build, not an
accepted performance fix.

## Validation and artifacts

`tests/review_profile/test.cpp` passes C++98 with warnings as errors and
ASan/UBSan. Tests cover threshold equality, invalid clocks, bounded retention,
coherent phases and compiler counts, pause reset, and 64-bit elapsed ticks.
PPC differential and inline dispatch suites are run into the evidence directory.
The post-commit diagnostics build is tagged `ddpsdoj-stall-watch`; build logs,
exact image hashes and archives are recorded by
`/home/humor/salvia-tests/xex-build/artifact-ddpsdoj-stall-watch.json`.
Header changes require explicit rebuild of sh4.cpp and d_cv1k.cpp, verified in
build output. No console FPS or freeze elimination is claimed by host tests.

## Console reproduction

Launch the candidate dedicated ddpsdoj image from a cold boot, insert coin,
start and select the same aircraft. After the entrance freeze, pause once;
if practical also replay the large-enemy explosion before a second pause.
Inspect `core_stall_watch`, `core_stall`, `frontend_stall_watch`,
`frontend_stall` and the image build tag. A CPU/I/O stall with many new blocks
supports a codegen cause; a draw/present stall or a CPU stall with little
compilation requires the corresponding device/render investigation. If the
freeze spans many short frames, all-frame peaks can exclude a single blocked
host frame, but cannot prove game logic or time progression from counters alone.

The A/B handoff folder contains only the original dedicated ddpsdoj image and
the candidate; the general frontend and mmpork builds stay in the external
archive. Reverting this commit alone does not restore a running XEX: restore
the original verified bytes or mirror the revert and rebuild.
