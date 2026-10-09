# CV1000: the emitter could write past its own code-write guard array

Baseline for this round: `1fdab65b`. One logical change plus this record.

## The defect

`Compiler::write_checks` is `CodeWriteCheck write_checks[MAX_INSNS]` -- 32
entries -- and every generated guest store appends one:

```
CodeWriteCheck &w=write_checks[write_check_count++];
```

The compile loop bounds `exit_count` (`MAX_INSNS*3-3`) and the emitted word
budget, but never `write_check_count`, while a block may hold up to
`MAX_INSNS` = 32 instructions and *every* one of them may be a store. A block
with 32 or more stores therefore writes one or more `CodeWriteCheck` records
past the end of the array. `Compiler`'s members are plain fields, so that
overwrites whichever field follows on the target layout (`cat`, `cat_words`,
the slot table, the exit array), and the emitter then produces garbage code --
an illegal instruction or a wild store at run time, from a block whose own
words looked fine when dumped.

Found while chasing the write-stamp probe crash. The probe adds 7 words per
store, so it grows the bodies that make long store-heavy blocks reachable; the
same overflow is latent in the shipped build.

## Change

One condition in the compile-loop bound: stop compiling a block when
`write_check_count >= MAX_INSNS-1`, exactly like the existing `exit_count`
bound. The block ends at its ordinary sequential completion, so the guest
behaviour is unchanged and the remaining instructions are compiled as their own
block.

## Verification

- `tests/sh3_ppc/run.py`: **PASS 846026 cases compiled 727804 fallback 118222**
  (the same split as `5131a077`, so the bound does not change the reachable
  instruction mixes the suite drives).
- `ddpdfk` 60 frames: `STATE 0cf251c3d512ddbb` and the per-frame video/audio
  hash file identical to `5131a077`; block boundaries may differ (fewer stores
  per block), the machine state may not.

## Limits

Host-only evidence. No console image was rebuilt for this commit: it changes
which addresses are compiled, not what any compiled block does, and the next
console image will carry it together with the next accepted change.

## Revert

Revert this commit: the added condition in `compile()` and this document.
