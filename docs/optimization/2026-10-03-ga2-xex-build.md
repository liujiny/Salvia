# GA2 Xbox 360 build: retained source optimizations

Built source checkpoint: `55f2b4273b93c84ba1814da836d180bd9699f9e3`.
The user authorized the XEX build after the source-only optimization rounds.

## Build and checks

Native Windows VS2010/XDK MSBuild built only the FBNeo solution (`Release`)
and the corresponding Salvia frontend (`Release_finalburn`). The successful
build command was run through `rtk proxy cmd.exe /d /c` using the external
`.work/build-fbneo-ga2-20261003.cmd` wrapper. Build exit code: **0 (PASS)**.
The modified System 32, V60, V25, NEC interface and Z80 sources were compiled.

- XEX magic: `XEX2`.
- Intermediate PE target machine: `0x1f2` (PowerPC).
- Build output and deployed `Distro360/fbneo.xex` are byte-identical.
- All eleven optimized source files still match the canonical checkout and
  runtime source tree; their hashes match the recorded pre-build manifest.
- Earlier differential host/PowerPC tests are documented with each source
  optimization. This build is not a measurement of console FPS.

The existing MSBuild .NET reference-assembly warning did not fail compilation.
The optional `xextool.exe` was unavailable, so the project copied the XEX
without additional compression, consistent with the existing toolchain.

## Artifact

Windows output:
`E:\Baiduyundownload\salvia-toolchain\.work\Salvia\Distro360\fbneo.xex`

Size: **34,611,200 bytes**.

SHA256:
`c0413aa4114212439ff0dea160bc0d42278a3510f412b149e63f2e6d21116386`

This XEX includes the retained V60 wait-loop, V25 self-jump, Z80 status-poll,
empty-layer row, unscaled sprite, blend-candidate and contiguous tile-span
optimizations. The user-reported console result for GA2 is recorded below;
other System 32 games have not yet been individually benchmarked on console.

## Console acceptance

On 2026-10-03, the user reported that GA2 runs at full frame rate on the
Xbox 360 with this build. This records the user's console test result; the
per-core setting and a frame-time trace were not provided.

## Rollback

The previous XEX is preserved at:
`E:\Baiduyundownload\salvia-toolchain\.work\ga2-build-20261003-before\fbneo.xex`

Previous SHA256:
`5db606137ae145792257771f20c5c962abba882536acbe9fe6750e3876a52f17`

To restore the earlier playable binary, copy that backup over the current
`Distro360/fbneo.xex` and the Xbox copy. Source rollback remains independent;
each optimization report specifies its own revert procedure. Reverting Git
alone cannot alter an existing XEX.

Raw build log and source/artifact identity JSON are external to Git, under
`.work/build-fbneo-ga2-20261003.log` and
`.work/ga2-build-20261003-before/`. No XEX, library, object, SDK, ROM or raw
build log is committed. No GitHub push was performed.
