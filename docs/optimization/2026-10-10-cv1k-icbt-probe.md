# CV1000: does the Xenon implement icbt? (one-boot feasibility probe)

Baseline `11cd2f53`. `SALVIA_CV1K_ICBT_PROBE` is set to 1 **for this probe commit
only** and must go back to 0 or away once the question is answered, whichever
way it goes.

## The question

The console A/B of `2026-10-10-cv1k-code-touch-console.md` measured the
generated code at 8.27 ms/frame -- 60% of the dispatch phase -- and showed that
prefetching it with `dcbt` costs 2.5 ms/frame instead of saving it, because
`dcbt` fills the **data** cache while the block's code is fetched through the
**instruction** cache. Nothing else in the frame's dispatch is that large, and
no other candidate on the table is worth more than about a millisecond.

PowerPC has exactly one instruction that warms the instruction cache:
`icbt`, the instruction cache block touch (opcode 31, XO 22). It arrived after
the ISA revision the Xenon is built on, and whether that core implements it is
not documented anywhere the project has. If it is not implemented, the core
raises a program exception, which this emulator has no handler for: the run
dies. If it is, the code prefetch of the reverted commit becomes viable by
swapping one instruction word, and it is worth 1-3 ms/frame.

## The probe

`sh3_icbt_line()` is a naked leaf -- `__emit(0x7c001a16)` (`icbt r0,r3`) then
`blr` -- called exactly once, from the first `allocate()` (after the state load,
so the emulator is otherwise fully running). `code` is a valid, mapped, aligned
arena address. The knob is guarded to `_XBOX`, so host builds, `STATE` and the
per-frame hash files are untouched by it (verified: `0cf251c3d512ddbb` and
`c42fdb4870f4f997112d88ff6f74ee12` with the probe compiled in).

## Reading the result

- **A log appears** with a `drc_work_probe icbt=1 executed_on_first_allocation=1`
  line: the instruction was accepted. The follow-up is to re-apply the reverted
  code prefetch with `icbt` in place of `dcbt`.
- **The emulator dies or resets at startup**, before any pause: the instruction
  is not implemented, and the whole instruction-cache-prefetch route closes for
  good. There is no third outcome worth planning for.

## Revert

Revert this commit: `SALVIA_CV1K_ICBT_PROBE`, `sh3_icbt_line`,
`sh3_icbt_probe_done` and the `allocate()` call in `sh3_drc_ppc.h`, the report
line in `sh4.cpp`, and this document.
