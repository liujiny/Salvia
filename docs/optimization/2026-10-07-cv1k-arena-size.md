# CV1000: default the SH3 PPC code arena to 32 MiB

Baseline SHA for this round: `c4e92f3380ac85ef1ee09897806806790ace8b9a`.

Companion documents:
[arena accounting](./2026-10-07-cv1k-arena-accounting.md),
[block table size](./2026-10-07-cv1k-block-table-size.md),
[PS4 portability review](./2026-10-07-cv1k-ps4-portability.md).

## Why

The previous round added `arena_recycles`/`arena_peak` and measured, for the
first time, how much of the SH3 PPC code arena is actually live during CV1000
gameplay. With the historical 8 MiB arena the high-water mark reached 8,374,304
bytes - effectively the whole arena - and the overflow guard fired 21 times in a
1800-frame `ddpsdoj` segment and 4 times in the same-length `ibara` segment. Each
overflow runs the full reset in `compile()`, discarding every compiled block and
forcing the whole native working set to be rebuilt through the interpreter
fallback.

The equivalent PS4 FBNeo problem (`sh3-arena64m`) was resolved by enlarging the
native code arena and that change is retained in the PS4 build. This round
applies the same fix here, with the size chosen from the Xbox-independent
measurement instead of picked to match PS4.

## Change

- `sh3_drc_ppc.h` gains the `SH3_PPC_CACHE_BYTES` override and raises the default
  from 8 MiB to 32 MiB. The override exists so host differential tests can
  reproduce the previous size from otherwise identical sources.
- On the 32-bit Xbox path the arena is the static
  `xbox_code[CACHE_BYTES / 4]`, so this is a `.bss` change; the host harness uses
  the `mmap` branch and is unaffected by the growth.
- No emitter, dispatch, timing, guard, allocation or save-state behaviour is
  modified.

## Size evidence

`qemu-ppc -cpu g4`, 1800 frames each, started from the in-progress save states
shipped in `Distro360/`, with the block table held at its new 131072 entries.
`peak` is live arena bytes at the high-water mark.

| Guest | arena | recycles | peak | rebuilds |
| --- | ---: | ---: | ---: | ---: |
| `ddpsdoj` | 8 MiB | 21 | 8,374,304 | 273,032 |
| `ddpsdoj` | 32 MiB | 1 | 33,540,480 | 104,414 |
| `ddpsdoj` | 64 MiB | 0 | 18,779,744 | 43,214 |
| `ibara` | 8 MiB | 4 | 8,373,344 | 55,194 |
| `ibara` | 32 MiB | 0 | 13,566,384 | 25,667 |
| `ibara` | 64 MiB | 0 | 11,920,928 | 23,613 |

Note the interaction with the table, which is measured in full in the companion
document: at the original 32768-entry table a 32 MiB arena alone still rebuilds
104,414 blocks on `ddpsdoj`, whereas 32 MiB plus 131072 entries rebuilds 43,214.
The arena fix is necessary but not sufficient on its own - the two changes have
to land together to remove the thrash.

Sizing:

- 32 MiB already removes every overflow on `ibara` and leaves one on
  `ddpsdoj`; combined with the larger table the live set settles at 18.8 MiB
  (`ddpsdoj`) and 11.9 MiB (`ibara`), so 32 MiB has roughly 1.7x headroom.
- 64 MiB produces numbers identical to 32 MiB on both guests. There is no
  evidence to justify doubling the `.bss` cost, so 32 MiB is kept.

## Behavioural equivalence

As recorded in the companion document, six builds differing only in the two size
constants were run for 1800 frames from the same save states: per-frame video and
audio hashes are bit-identical across all six builds for both guests, and the
final whole-machine `STATE` hash is identical. The resize changes performance
only.

## Xbox 360 build and memory budget

Built with the legacy XDK toolchain via `scripts/msbuild.cmd`
(`fba_vs2010_libretro_360.sln` Release + `Salvia.vcxproj` `Release_finalburn`):
**PASS**, `Distro360/fbneo.xex` 34,619,392 bytes, SHA-256
`d18da53c2b804249d8ce6d8983d7d977a82c6de39178a3ef14a423614ee10a13`.

`Salvia.map` section table:

| Section | Size | Delta vs previous build |
| --- | ---: | ---: |
| `.text` | `0x0153ef74` | - |
| `.data` | `0x006f60c8` | - |
| `.bss` | `0x04d6bc7c` (77.4 MiB) | `+0x01800000` = exactly +24 MiB |

The `.bss` growth is exactly the 8 -> 32 MiB arena change. The block table is
`calloc(TABLE_SIZE, sizeof(Block))` (84 bytes x 131072 = 10.5 MiB) and lands on
the heap, so the static image grows only by the arena. The XEX linked and
packaged without a memory-layout error.

## Validation limits

Host `qemu-ppc` evidence and a successful Xbox 360 link only. No FPS or
frame-time number is claimed, and the change has not yet been run on hardware.

## Status

Measured, size-swept, behaviourally verified on the host, and built for Xbox
360; hardware confirmation is still outstanding.
