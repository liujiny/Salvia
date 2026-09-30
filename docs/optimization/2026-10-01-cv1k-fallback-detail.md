# CV1000: attribute sampled interpreter fallback before changing its semantics

Build ID: `cv1k-fallback-detail-20261001-r1`.
Baseline commit: `61579aae5a91a69a9e42253e268f5d2b2ac09999` on `work/reversible-checkpoints`.
The prior short-snapshot experiment was explicitly reverted; this baseline is source-equivalent to the retained ASYNC-RECOVERY version. This commit is diagnostic, not a claimed speedup. No GitHub push is requested.

## Input and rationale

The existing 75-line GPU log is entirely `cv1k-async-recovery-20260930-r1`. Last pause window, lines 40-75: 2628 frames, 42 timing samples, 12 separate workload frames. cpu_io=14.745 ms, draw_sync=1.349 ms, core total=16.266 ms and frontend sampled loop=18.745 ms. It includes one forced-sync fallback and recovery: activations=2, fallbacks=1, attempts=1, active=1, failed=0, timeouts=0. It is not a clean fixed-input A/B benchmark.

In those 12 counted frames, the trace has 461464 native calls, 29839 interpreter steps, 18924 partial native exits and 16810 0x60xx interpreter instructions (GPU lines 42-51). Counts/guest cycles are not host time; the broad opcode family and partial label cannot identify the precise memory guard or prove an unsupported opcode. The CV1000 watched RAM read may charge idle cycles via its original handler and must not be bypassed just because its guest-cycle count is large.

Raw logs, original optimization review, relevant source and EXE/MAP/XEX are preserved byte-for-byte under toolchain `.work/cpu-fallback-detail-20261001-044427/baseline/`. The baseline XEX SHA256 is `8fc88d5337915c33f6f0374df8a1931e1a62e12b7cd568c3f2fa9ad2a72889bc`.

## Single logical change

Refine the existing Count=true workload path to join each actual fallback step to its dispatcher exit category. Capture the already fetched opcode, its instruction PC, delay context, and (only for 0x60xx memory reads) Rm before the interpreter changes it. Classify using existing map metadata without invoking memory handlers or rereading guest instructions/data. Count the charged guest cycles after the original IRQ/EAT tail.

New pause records:

- `fallback_origin`: unknown, disabled, delay, IRQ, gate, fetch, no-entry, short budget or partial; exact predecessor category, not a native guard ID.
- `fallback_60_opcode`: exact opcode counts and guest cycles, top 16 by count among the complete 256-bin histogram.
- `fallback_60_access`: not a 0x60 read, internal, unaligned, directly mapped, mirrored, watched longword, other handler, or unknown. Includes the cross-counts for partial/no-entry/budget.
- `fallback_site`: bounded exact PC/opcode/origin/access/delay entries, first/last operand address, count and guest cycles. At most 64 sites, four probes per observation; no replacement. Untracked events and cycles are explicitly reported. Rankings only describe admitted sites.

The watched class means the existing read map routes that aligned longword to the registered watched handler address. It does not assert that the board-specific idle-PC predicate fired. The site PC is the fetched instruction address, not necessarily the PC visible to a delay-slot memory handler. Guest-cycle totals include interrupt and base-cycle accounting and are never labeled CPU host time.

The original Sh3PpcState ABI, compiler/emitted instructions, interpreter helpers, timing, memory maps, GPU pipeline, SDL recovery and audio are untouched. The detail object is 8064 bytes in the host test (whole work profile 10536 bytes); a compile-time 12 KiB detail bound is enforced. No heap allocation. All observer calls/counters are in Count=true, separate from the 1/64 timing samples. Only pause reporting formats/appends; no fourth runtime log.

## Executed host checks

`run_inline.py` ran six suites in optimized and ASan/UBSan builds. All 12 executions passed, including:

- Five exact dispatcher exit labels and the existing 12000 random traces x 12 slices; ordinary calls leave profile bytes untouched.
- 228125 independent four-way probes and 1000000 replacement/reset operations.
- 442729 exact bounded source comparisons, including protection pages and paired mutations.
- 2323712 production Block snapshot/metadata checks.
- Disjoint sampling/reset/counter checks.
- New fallback detail: all 65536 opcode-width classifications, concrete metadata/alias cases, 200000 aggregate events, collision accounting, 64-bit totals and bounded reports. The actual observer reads modeled map metadata only; it does not emulate a full device transaction.

The updated outer-loop extraction runner accepts a preserved already-templated baseline as well as the older plain loop. Two additional optimized/sanitized executions passed 12000 seeds x 12 slices, matching baseline/ordinary/counted states, memory and charged cycles and verifying exactly one fallback observation per interpreted step. The outer fixture abstracts instruction/mapping callbacks; the detail suite independently exercises the real observer.

Total: 14 passed host test executions. No new generated-PPC execution differential test or full DDPSDOJ replay is claimed. Sampling overhead and Xbox performance are not established by these host results.

## Build and rollback

Mirror only the explicit reviewed file list from `.work/github-publish-salvia` to `.work/Salvia`, with hashes in `mirrored-files.json`. Full native build uses the preserved `.work/cpu-cache-ready-20260930-205053/git-baseline/tools/build_fbneo_checkpoint.cmd` with the existing toolchain root, rebuilding FBNeo and the frontend and checking SDL.

Rollback binary: toolchain `.work/fbneo-before-fallback-detail.xex`, exact ASYNC-RECOVERY baseline above. A local `git revert <this-commit>` must be mirrored back to the runtime tree and rebuilt before it changes the runnable XEX. Raw logs, SDKs, keys and compiler artifacts stay outside Git.

For the next console run, use the same saved state and options. Pause once after loading, resume the combat interval, then pause again with Select/Back+Y. Do not intentionally open system UI during the timing comparison. Copy the same three logs back. Compare counts and categories, not merely reciprocal sampled timings. Explicitly inspect site_untracked before treating site rankings as representative.

## Native build and final identity

FBNeo core Rebuild, SDL Build and Salvia frontend Rebuild completed with exit code 0. The XEX2 header, embedded build ID in the linked EXE and build/distribution SHA256 were independently verified.

The ordinary loop remains 1728 bytes with a 176-byte stack frame, at 0x83266388..0x83266a48. Its 431 non-padding words differ only in named call/tail targets, five global-address displacements shifted by 128 bytes, and the relocated bases/entries of its existing two jump tables. No additional ordinary observer calls occur. The counted loop is 2712 bytes and calls the new observer at 0x83266f48 and 0x832671e4. The native Sh3WorkReset passes 10536 bytes to memset, confirming the total profile storage. These are native isolation checks, not a guarantee of zero cache/layout or sampling overhead.

- Build time on the Runner: 2026-10-01 04:51:16.
- Output: `E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`.
- Build ID: `cv1k-fallback-detail-20261001-r1`.
- Size: 34611200 bytes.
- SHA256: `f87d67213befae2c12de7d833eabd7497d6bb14880215c9e089174e1369fc10b`.
- Rollback: `.work/fbneo-before-fallback-detail.xex`, SHA256 `8fc88d5337915c33f6f0374df8a1931e1a62e12b7cd568c3f2fa9ad2a72889bc`.
- Evidence: `native-isolation.json`, baseline/candidate bounded disassembly, `candidate-artifact.json`, `source-validation.json`, `tests-after/results.json`, `tests-outer/results.json` and `build.log` in this round's external evidence directory.
- Original logs and optimization report are preserved. No generated-PPC execution differential test, full game replay or new console performance test was performed.

The exact local commit and parent are recorded in `final-artifact.json` after committing this diagnostic alone. Nothing is automatically pushed to GitHub.
