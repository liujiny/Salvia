# CV1000: size the SH3 PPC block table from way-conflict evidence

Baseline SHA for this round: `c4e92f3380ac85ef1ee09897806806790ace8b9a`.

Companion documents:
[arena accounting](./2026-10-07-cv1k-arena-accounting.md),
[arena size](./2026-10-07-cv1k-arena-size.md),
[PS4 portability review](./2026-10-07-cv1k-ps4-portability.md).

## Why

`sh3_drc_ppc.h` kept a fixed 32768-entry, four-way block table. When all four
ways of a set are occupied by other PCs, the probe in `sh3_drc_lookup.h`
(`return next++ & 3u;`) overwrites one way and the dispatcher must call
`compile()` again on the next visit. Nothing recorded why a `compile()` was
requested, so the arena and the table could not be told apart.

The arena accounting round added `arena_recycles`/`arena_peak` and showed the
arena also overflows, but on its own it did not explain the bulk of the
recompilation. This round adds the missing attribution and uses it to size the
table.

## Change

- `sh3_drc_work_profile.h` gains `rebuild_conflict`, `rebuild_source`,
  `rebuild_map`. They separate the three reasons `recompile` can be requested:
  a tag/source-pointer miss (the set could not hold this PC), a same-PC opcode
  snapshot mismatch, and a read-map change with an unchanged snapshot.
- `sh3_drc_dispatch.h` classifies the entry **once**, before `compile()` runs.
  This ordering is required, not cosmetic: `compile()` overwrites the block
  record, so classifying afterwards attributes every rebuild to the newly
  written block and reports `read_map` for everything.
- `sh3_drc_ppc.h` gains the `SH3_PPC_TABLE_SIZE` override and raises the default
  from 32768 to 131072 entries (98104 additional `Block` records). The override
  exists so host differential tests can select sizes from otherwise identical
  sources.
- `sh4.cpp` reports the attribution in `Sh3WorkReport()` as
  `drc_work_rebuild_causes conflict=<n> source=<n> read_map=<n>`.

Both counters are written on the compile path only. Generated code, the
dispatch hot loop, guest cycle accounting and memory guards are untouched.

## Attribution result

Measuring the *unattributed* cause with the 8 MiB arena and the 32768-entry
table, over 1800 frames started from the in-progress save states shipped in
`Distro360/`:

| Guest | conflict | source | read_map |
| --- | ---: | ---: | ---: |
| `ddpsdoj` (`ddpsdoj.state4`) | 273032 | 0 | 0 |
| `ibara` (`ibara.state1`) | 55194 | 0 | 0 |

Every rebuild is a way-conflict eviction. Neither guest rewrote its own opcodes
in these segments and no read-map change contributed. That makes the table the
component to size, and it makes the arena's own overflow a strictly secondary
effect, which the arena round measures separately.

## Size sweep

`qemu-ppc -cpu g4`, one-factor-at-a-time, 1800 frames each, starting from the
`Distro360` save states. `peak` is `arena_peak` (live arena bytes), `rebuilds`
is total `compile()` calls.

`ddpsdoj.state4`:

| arena | table | recycles | peak | rebuilds |
| --- | --- | ---: | ---: | ---: |
| 8 MiB | 32768 | 21 | 8,374,304 | 273,032 |
| 8 MiB | 131072 | 13 | 8,375,008 | 176,875 |
| 32 MiB | 32768 | 1 | 33,540,480 | 104,414 |
| **32 MiB** | **131072** | **0** | **18,779,744** | **43,214** |
| 64 MiB | 131072 | 0 | 18,779,744 | 43,214 |
| 32 MiB | 262144 | 0 | 18,695,024 | 43,059 |

`ibara.state1`:

| arena | table | recycles | peak | rebuilds |
| --- | --- | ---: | ---: | ---: |
| 8 MiB | 32768 | 4 | 8,373,344 | 55,194 |
| 8 MiB | 131072 | 3 | 8,372,992 | 48,852 |
| 32 MiB | 32768 | 0 | 13,566,384 | 25,667 |
| **32 MiB** | **131072** | **0** | **11,920,928** | **23,613** |
| 64 MiB | 131072 | 0 | 11,920,928 | 23,613 |
| 32 MiB | 262144 | 0 | 11,920,928 | 23,613 |

Reading the sweep:

- At a fixed 8 MiB arena the larger table alone cuts rebuilts by 35% on
  `ddpsdoj` and 11% on `ibara`; the two changes are super-additive, because a
  larger table also keeps more blocks resident across the fewer, but still
  occurring, arena resets.
- 131072 -> 262144 changes nothing measurable (43,214 -> 43,059, i.e. 0.4%, and
  0% on `ibara`). 131072 is the sweet spot; the extra 8 MiB would only enlarge
  `calloc()` without buying anything.
- The `Block` table is heap allocated (`calloc(TABLE_SIZE, sizeof(Block))`, 84
  bytes per 32-bit record), so this change does not grow `.bss`.

## Behavioural equivalence

The whole point of a size change is that it must not alter emulation. Six builds
that differ only in the two size constants were run from the same save states
for 1800 frames with identical scripted input:

- **per-frame video and audio hashes: bit-identical** across all six builds, for
  both guests (1800/1800 frames each, 2 hashes per frame);
- **final whole-machine `STATE` hash: identical**
  (`ddpsdoj` `3c56b8c05ffb013b`, `ibara` `9a5151382a7d2c04`).

The change is therefore pure performance: same guest execution, same pixels,
same audio, same serialized state.

## Xbox 360 build

The measured default (32 MiB arena + 131072 table) was built with the legacy XDK
toolchain through `scripts/msbuild.cmd`:

- `libretro/FBNeo/.../fba_vs2010_libretro_360.sln` Release and
  `Salvia.vcxproj` `Release_finalburn`: **PASS**.
- `Distro360/fbneo.xex`, 34,619,392 bytes,
  SHA-256 `d18da53c2b804249d8ce6d8983d7d977a82c6de39178a3ef14a423614ee10a13`.
- `Salvia.map`: `.bss` = `0x04d6bc7c` (77.4 MiB), `.data` = `0x006f60c8`. The
  `.bss` delta against the previous map is exactly `0x01800000` (24 MiB), which
  is the arena change alone; the table lives on the heap as intended.

## Validation limits

The sweep and the equivalence hashes are **host** evidence produced by compiling
the same `sh4.cpp` for `powerpc-linux-gnu` and running it under `qemu-ppc`. They
are not an Xbox 360 FPS measurement and no FPS number is claimed here. The XEX
step proves only that the toolchain accepts the change and that the memory
budget holds. The host harness also replaces the driver and EPIC12 objects with
pinned builds from earlier rounds, so its video output is not a visual reference.

## Status

Counted, size-swept and behaviourally verified on the host; built for Xbox 360;
not yet confirmed on hardware.
