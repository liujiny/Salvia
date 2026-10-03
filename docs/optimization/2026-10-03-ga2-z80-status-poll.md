# GA2: batch sound CPU polling inside timer segments

Baseline: `9c0c9f619de267421b451f389d5f0168312bbc87`.

## Finding and implementation

The 3,000-frame host replay executed the Z80 polling loop at `0xcb` roughly
9.4 million times: `LD A,ff; IN A,(80); RRA; JP NC,loop`. This polls YM3438
timer A status; it is not silence detection and cannot simply be discarded.

The GA2 driver opts port `0xff80` and the fixed sound ROM range into a
timer-bounded optimization. `BurnTimerUpdate` limits each `ZetRun` to the
next timer event before calling `YM2612TimerOver`. Other emulated CPUs do
not run concurrently with this sound CPU segment.

After a **real IN**, a status of 0 or 2 proves that the busy flag is clear
(`FM_STATUS_FLAG` has cleared any expired `BusyExpire`). This status cannot
change again until a timer callback or register write. The recognized loop
contains no writes. Its repeated reads have no remaining side effects.

The core checks the current opcode/argument page mappings and all eight
instruction bytes, rather than caching or hard-coding PC `0xcb`. It charges
whole 32-cycle `RRA/JP/LD/IN` iterations and preserves A, flags, PC, previous
PC, WZ, last opcode, refresh R and cycle totals. A partial tail executes
normally. The next timer segment performs another real IN before batching.

Pending IRQ/NMI, stop requests, EI/RETN shadows, nonstandard cycle tables,
contention, nontrivial raster callbacks, daisy chains, page crossings and
unmatched code all retain normal execution. Per-CPU configuration is outside
save state data, follows `ZetOpen`/`ZetClose`, and is absent in other games.
No new worker threads or production logs are added.

## Validation

Commands run through `rtk proxy`:

- `python3 libretro/FBNeo/tests/z80_status_poll/test.py`: **43,407** differential
  cases using the actual Z80 core with batching on/off, under ASan/UBSan and
  with assertions enabled. Covers all flags, refresh wrap, cycle tails,
  mid-loop entry, busy expiry, timer changes between segments, IRQ/NMI,
  patched code, different fetch maps, MSX cycle tables and stop callbacks.
- Host replay from reset, 4,000 frames: identical per-frame video/audio and
  final saved state to the baseline.
- Host replay from the existing state, 6,000 frames, two rendering workers:
  identical video/audio hash file and saved state. Hash-file SHA256:
  `34b42788d71703bc17d9b3b8946f0d87263e5eb7dd33d964790fc202d52fa5c6`;
  state SHA256:
  `f956cf6a2b5af163e3d9893c0b54db1e0c3330e99cd9e3e1f4f2fb2e7eb67ca3`.
- Big-endian PowerPC execution under QEMU, 1,200 frames from reset:
  identical per-frame video/audio, final state and raw frame. State SHA256:
  `5c33ee1179595aa6bd87720e46d7e855343638328e593028fe5929aa639790e7`.
- Diagnostic copy on the host: **781,144** real IN executions plus
  **8,615,994** batched iterations = the baseline's **9,397,138** reads.
  About **91.7%** of redundant polling iterations are removed in this replay.
- Sequential host A/B/B/A, 3,000 frames each, two rendering workers:
  baseline **3.2484 / 3.4016 ms/frame**, candidate **2.9721 / 3.0283 ms/frame**
  (about **9.8%** lower average time within this comparison). Host load changed
  since the V25 benchmark; do not compare absolute times across those runs.
- `git -c core.whitespace=cr-at-eol diff --check`: clean; existing CRLF files
  retain their line endings.

Evidence and test binaries live in `/home/humor/salvia-tests/ga2/z80-poll/`.
The user-provided runtime ROM and the previously used ROM have identical
SHA256 `2befa4aa4f5927480595f28cfb5e0daf52003d92db69e43e9091358e2ba45cf3`.
The ROM, logs, states and generated binaries are not committed.

These are replay and instruction-level checks, not a full playthrough or
an Xbox performance measurement. No claim of reaching 60 FPS is made.

## Artifact and rollback

**No Xbox/XEX build**, per user instruction. Existing `Distro360/fbneo.xex`
is still the `6d7c53dffa9b6029f9ae1653e7e9042b3e34fc6b` console build, SHA256
`5db606137ae145792257771f20c5c962abba882536acbe9fe6750e3876a52f17`.

Original source/identity backup: sibling `../ga2-z80-poll-20261003/`.
Revert this document's commit, mirror its five source files into `../Salvia`,
then rebuild when authorized. A source revert alone does not replace an XEX.
