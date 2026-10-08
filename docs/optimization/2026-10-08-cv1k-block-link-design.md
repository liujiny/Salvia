# CV1000 block linking: what this DRC needs before a link is safe

Target, from `2026-10-08-cv1k-phase-split-result.md`: about 1,080 host cycles
per dispatched block, of which only about 250 are generated code. `cpu_io`
scales linearly with the dispatch count (ddpdfk 32,185 dispatches at 10.888 ms,
ddpsdoj 52,296 at 17.593 ms), so the only change that moves the frame rate
materially is one that removes per-dispatch work.

## The current contract

The dispatcher revalidates **every** block entry, not just the first:

```c
rebuild = (b.source!=source || b.pc!=pc || !sh3_drc_source_equal(b.original,source,b.words) ||
           (b.check_read_map && MemMapR[phys>>SH3_SHIFT]!=page));
```

That check is the only thing standing between a stale compiled block and wrong
emulation. It covers guest code rewriting itself, a DMA upload replacing code,
cheat writes, and `Sh3MapMemory` fetch-map changes. `sh3_drc_source_check.h`
compares all `b.words` 16-bit opcodes, so the dispatcher's own cost is not in
the comparison: for 8.9 words it is roughly 40 years-of-CPU cycles if the lines
are hot.

The generated code has one write guard per RAM store, emitted in
`sh3_drc_ppc.h::memory()` and patched by `protect_code()`:

```
lwz/imm r0, source_begin&~(size-1)
sub     r0, r12, r0
cmplwi  r0, <len>          ; len patched after decoding
branch-if-in-range -> slow exit
```

This catches a store that overlaps **the executing block's own** instruction
bytes, through any alias. It does **not** catch a store that overwrites some
**other** block's source, which is exactly the case the dispatcher's per-entry
revalidation exists for. A link that skips revalidation therefore needs its own
invalidation rule.

## Why a per-store guard is affordable

The obvious objection is that widening the store guard costs performance on the
hottest SH3 path. The measured budget says otherwise: the dispatch machinery
costs about 34.8 M host cycles per frame (32,185 x 1,080), while a CV1000 frame
issues on the order of tens of thousands of guest stores. Adding four
instructions per store is on the order of 0.1 M cycles, or well under 1% of the
frame, against a link that can remove most of 34.8 M.

## Design

1. **Compiled-page bitmap.** `compile()` sets `sh3_code_page[(pc-ram.start)>>16]`
   for the block's source page. The RAM window is 16 MiB, so this is 256 bytes
   and stays in one cache line.
2. **Store-side marking.** The existing per-store guard gains a page test: if
   the store address lies on a marked page, take the slow exit. The slow exit
   bumps a global `sh3_code_epoch`. Because the guard already computes
   `r12 = effective address`, the added cost is a shift, a byte load from the
   bitmap, a compare and a branch.
3. **Link slot per block.** `Block` gains `UINT32 link_pc` and
   `int (*link_entry)(Sh3PpcState*)`, plus `UINT32 link_epoch`. When the
   dispatcher validates a block B at PC p, it records that result into the
   predecessor A it just came from (`A.link_pc=p`, `A.link_entry=B.entry`,
   `A.link_epoch=sh3_code_epoch`). No emitter change is needed for this half.
4. **Fast path in the dispatcher.** On the next iteration, if the running
   block's `link_entry` is set, `link_pc == m_pc`, `link_epoch == sh3_code_epoch`
   and `link_entry` belongs to a still-tagged block, the dispatcher executes the
   linked entry directly, skipping the hash and the `lookup[]` probe. With
   the epoch unchanged, revalidation is not needed: no store has touched any
   compiled page since the link was made.
5. **Emitter fast path (second landing).** Once 1-4 are proven, the link check
   moves into the generated epilogue, where the successor PC is a compile-time
   constant: `compare m_pc to target, compare sh3_code_epoch to the immediate
   recorded epoch, and branch to the linked entry`, otherwise fall back to the
   current `blr`. That removes the dispatcher frame, the call/return and the
   `Block` record fetch from the common path.

Steps 3-4 are semantics-preserving and are the first landing, because the
validation result is carried forward rather than dropped. Steps 1-2 and 5 are
what actually remove the metadata traffic; they change emitter output and must
be validated with the per-frame video and audio hash regression plus the final
state hash (`0cf251c3d512ddbb`) on the PowerPC harness before any console build.

## Risks that must be handled, not assumed away

- The guard in step 2 is **conservative only if the bitmap is never cleared**.
  Reusing a cleared page would let a stale link survive a code write.
  Clearing must also bump the epoch.
- `sh3_drc_reset()`, `Sh3SetDrcReadMirror`, `Sh3SetDrcDeviceRead`,
  `Sh3SetDrcRam` and the arena overflow path (`used+MAX_WORDS`) all drop blocks;
  each must bump the epoch, otherwise a link can target freed or replaced code.
- The interpreter fallback path and `m_delay` exits must keep going through the
  dispatcher; only a completed block with a matching successor PC may link.
- The arena recycles (`arena_recycles`) and `xbox_code[]` aliasing mean the
  generated entry address of a recompiled block can be reused; the epoch plus
  the tag check must both pass.

## Status

Step 3 and step 4 are landed in `bb2455d6`: `Block` carries
`link_block`/`link_pc`, the size asserts moved from 84 to 92 bytes, and the
chained dispatcher takes the linked record when its tag, source pointer,
instruction snapshot and read map all still match, falling back to the
four-way probe otherwise. Nothing skips a validation, so the emulation result
is unchanged by construction; the build and the hash regression for that
commit are still outstanding.

Steps 1, 2 and 5 are **not** implemented. They are the half that actually
removes the metadata traffic, and they are the half that changes generated PPC,
so they must land together with the epoch bumps. Concretely, still missing:

- `sh3_code_page[256]`, set by `compile()` for each block's source page.
- A page test in the store guard emitted by `Compiler::memory()`
  (`sh3_drc_ppc.h`, the `if (write)` block that materialises
  `source_begin&~(size-1)` into r0). The store path has no free register: r3 is
  the state base, r4-r10 are the guest-register cache, r11 and r12 are the
  address scratch pair and r0 is the compare scratch, so the page index and the
  bitmap base have to be folded into the existing comparison rather than added
  beside it.
- `sh3_code_epoch`, bumped from that guard, from `sh3_drc_reset()`, from the
  arena overflow path in `allocate()`, and from the paths that drop blocks
  (`Sh3SetDrcReadMirror`, `Sh3SetDrcDeviceRead`, `Sh3SetDrcRam`,
  `sh3_drc_invalidate_ram`).
- An external-write guard for cheats: the FBNeo cheat engine writes guest RAM
  directly, so it never runs generated store code. The core must bump the epoch
  whenever any cheat is active, or links must be disabled while it is.
- The epilogue link check itself, in `Compiler::finish()`/`emit_exits()`: the
  successor PC is a compile-time constant, so the check is a compare against
  `m_pc` plus a compare against the linked epoch, then a branch to the linked
  entry instead of `blr`.

No XEX has been built from any of this. The measurement that justifies the work
is `c12a0a60`.
