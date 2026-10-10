# Reversible Salvia development

User requirement: one independently reviewable and revertible Git commit per logical change.

- This checkout records source history on `work/reversible-checkpoints`. Record HEAD and inspect the worktree before changing files. Preserve user edits; do not reset, clean, amend published history or force-push.
- The configured Windows runtime/build source is the sibling `../Salvia`. Before editing, verify the relevant baseline text matches this Git checkout. Edit here, mirror only the reviewed changed paths to the runtime copy, and verify hashes before building. A Git revert in this checkout does NOT update that sibling or an existing XEX; mirror the reverted paths and rebuild, or restore the separately verified XEX backup.
- Keep each independent experiment in its own commit together with tests and a tracked Markdown review under `docs/optimization/`. Record the baseline SHA, change rationale, actual commands/results, validation limits and XEX identity. Use a separate `git revert <sha>` commit for rejected experiments.
- Do not commit SDKs, keys, ROMs, saved games, raw runtime logs, object/library/debug files, generated binaries or ignored Distro360 contents. Store raw inputs, old XEX and build evidence outside this Git checkout. Do not stage unrelated files.
- No automatic push without a current upload request. When requested, use the existing WSL Ubuntu-24.04 SSH workflow; never read or print private key contents.
- CV1000 runtime diagnostics retain the existing three-log workflow and pause-time performance summaries. Keep low-frequency sampling; do not add per-instruction timers or live gameplay file I/O.
- Iteration builds compile the diagnostics flavor only (`SALVIA_FBNEO_DIAGNOSTICS 1`); it is the image that writes `cv1000-gpu.log` on pause. Compile the release flavor (`SALVIA_FBNEO_DIAGNOSTICS 0`) once, when the user accepts the change. One image set is two compiler invocations (FBNeo core + Salvia frontend), not one.
- Hand off exactly the two images an A/B compares, named for their role, and nothing else in `Distro360/`: one file per variant, no `fbneo.xex`/`fbneo-diag.xex` duplicates of the same bytes under a second name. The archive keeps every image; the folder the user copies from keeps only the pair.
- Separate appended runs by build ID and session boundaries. Core timing fields are per-pause windows; GPU counters/timings can be cumulative. Do not sum cumulative snapshots or convert sampled loop reciprocals into measured FPS.
- Retain the existing 128-source-page GPU batching limit. Do not repeat rejected precise GPU dependency scans, 256-page batches, more render cores or CRT combination experiments without new evidence and explicit justification.
- Builds, host synthetic tests and native disassembly are different evidence. Never claim console FPS, full-game replay or PPC execution verification from host-only tests.
