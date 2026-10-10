# CV1000: the Xenon does not implement icbt -- measured, route closed

Baseline `11cd2f53`. The probe commit `01d183d8` was built, run on the console
and reverted here; its knob, helper and report line are gone.

## The question and the answer

The generated code is 8.27 ms/frame, 60% of the dispatch phase, and
`2026-10-10-cv1k-code-touch-console.md` showed that prefetching it with `dcbt`
costs 2.5 ms/frame instead of saving them: `dcbt` fills the data cache, the
block's code comes through the instruction cache, and the code lines pushed into
L1D/L2 then compete with the guest RAM the block is about to touch. PowerPC has
exactly one instruction that warms the instruction cache -- `icbt`, the
instruction cache block touch -- and whether the Xenon implements it is not
documented. If it does not, the core raises a program exception.

`fbneo-icbt-probe-diag.xex` (sha256 `d49e1d05811fa16c9a347e62d5399af753a88d57b073287739a9e9ef75d832f6`)
executed one `icbt` -- `0x7c001a16` -- on a valid arena line, once, from the
first `allocate()`, after the state load. The console answer: **fatal error
immediately after loading the game.** The instruction is not implemented, and it
takes the emulator down as designed rather than being silently ignored.

## What this closes

There is no way to prefetch the generated code on this CPU, so its 8.27 ms/frame
-- 60% of the dispatch phase, 538 cycles per entry for about three 128-byte
lines, a code working set of 15.6 MiB walked once per frame against a 1 MiB L2
-- cannot be attacked by moving the fetch earlier. It can only be attacked by
executing fewer bytes per entry, and that was measured in the same round:

- Address-guard hoisting to the base register: **worse**. 4,659 accesses could
  reuse a validated range against 25,857 that would need a fresh guard, +14
  words per block, because the ~3.4 hoistable accesses in a block almost always
  use different base registers.
- Merging the two guard branches with `crand`: **no words saved**. Both tests
  still need their own rotate and compare; the merge trades the second branch
  for the `crand`, which is a wash.
- Budget refusals (`drc_work_exits short_budget`): 5,226 of 521,565 entries,
  about 0.17 ms/frame. Not a lever either.

## Where the accepted build stands

The shadow dispatch record (`8bed73ba`) is this session's win and the tree's
released content: pre-entry 7.21 -> 4.22 ms/frame, `cpu_io` -2.9 ms, the frame's
work from over budget to 1.9 ms under it in its A/B window, `STATE` and the
per-frame hash files identical at 60, 600 and 1800 frames, 846,026 PPC
differential cases passing. Everything measured since is at or below the noise
floor of a console round, so the remaining large gains are structural rather
than local: the 13.87 ms of dispatch is 60% irreducible instruction fetch and
35% pre-entry that is itself a forced miss because the code walk flushes the
level the metadata lives in.

## Revert

Revert this commit to restore the probe commit's document; reverting the probe
commit itself removed `SALVIA_CV1K_ICBT_PROBE`, `sh3_icbt_line`,
`sh3_icbt_probe_done`, the `allocate()` call and the report line.
