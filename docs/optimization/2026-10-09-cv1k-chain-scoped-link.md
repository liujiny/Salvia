# CV1000: a chain-scoped link without write stamps -- measured, not worth it

Baseline for this round: `5131a077`. Diagnostics-only change plus this record.

## The idea being tested

The write-stamp line (`2026-10-09-cv1k-write-stamp-design.md`) is blocked on an
emit bug, so this round measured the stamp-free alternative before building it:
a *chain-scoped* link. A dispatcher chain validates every block as it enters
it, so if no guest store has executed since the chain call started, every block
the chain has entered is still valid, and a successor could be entered without
revalidating its source. The signal is one flag, set by generated stores (four
words: `li r0,1`, `lis/ori r12,&flag`, `stb r0,0(r12)` -- only r0 and r12 are
touched, which the earlier bisect proved harmless), cleared once per chain
call, and tested per entry.

## Measurement

`ddpdfk`, `user-ddpdfk4-core.state`, `dips=00,07,00,00`, `render_cores=2`, no
input, 60 frames, `SALVIA_CV1K_CHAIN_PROBE=1`. `STATE 0cf251c3d512ddbb` and the
per-frame video/audio hash file are identical to the build without the probe,
so the probe only reads.

| 60 frames | value |
| --- | ---: |
| source validations | 5,418,967 |
| entries in a chain that had executed no store (`clean`) | **317,281 (5.9%)** |
| entries in a chain that had stored (`dirty`) | 5,101,686 (94.1%) |
| entries (native_calls) | 5,373,584 |

## Conclusion

**5.9% is the ceiling**, and it is a ceiling with no link overhead subtracted:
at the measured 215 cycles per removed entry that is 68M cycles per 60 frames,
about 0.35 ms/frame, before paying for the successor check that every linking
epilogue would carry. The CV1000 guest stores constantly -- roughly once per
entry on average -- so any chain longer than a couple of blocks contains a
store and can never link. The stamp-free route is not worth building; the
fine-grained (per-line) write stamp is the only signal that can license the
other 94%, which is exactly what the blocked probe was for.

This also sizes the stamp line honestly: with per-line stamps, the entries that
can skip revalidation are those whose own two lines were untouched since the
last validation, not those in a store-free chain. The probe that measures that
share is the one that needs the emit bug fixed (`/tmp/fuse/` has the patch, the
dump harness and the qemu fault log), and the bug is already narrowed to the
`add` + `rlwinm` + `stbx` triple: with those three removed the probe runs, so
the clobbers and the table itself are not the cause.

## Revert

Revert this commit: the `SALVIA_CV1K_CHAIN_PROBE` blocks in `sh3_drc_ppc.h`,
`sh3_drc_dispatch.h`, the two counters and the report line, and this document.
