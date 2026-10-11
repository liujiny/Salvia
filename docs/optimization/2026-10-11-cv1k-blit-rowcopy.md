# CV1000: copy the batch box a row at a time, with the next row in flight

Baseline `e8406c35`. The console numbers are `Distro360/cv1000-gpu.log`, the
same `mmpork` session `2026-10-11-cv1k-blit-threshold.md` and the census read,
and the transfer it changes is the one the census named as the largest term
that is still a plain scalar copy.

## Why

Steady state, 10,616 sampled batches, 9.6 batches per frame:

| term | ms/batch | ms/frame | state |
| --- | --- | --- | --- |
| `untile_mask` (result texture -> vram) | 0.448 | 4.30 | VMX, tiled order |
| `input_upload` (vram -> input texture) | 0.415 | 3.98 | scalar `memcpy` per row |
| `source_upload` (atlas page swizzle) | 0.355 | 3.40 | VMX |
| replay | | 3.03 | `epic12_blend_fixed` is 98.3% of it |
| command walk and construction | | 6.85 | unmeasured inside the job |

`input_upload` moves 928,246,059 pixels in 11,860 batches -- 78,267 pixels, or
about 313 KB, per batch, 3.0 MB per frame -- and costs 3.98 ms for it. That is
6.0 MB of traffic a frame (read plus write) in 3.98 ms.

The copy was one `memcpy()` per row:

```c
for (int y=0;y<height;++y)
    memcpy((BYTE*)lr.pBits+y*lr.Pitch,vram+(bounds.min_y+y)*8192+bounds.min_x,width*4);
```

Source rows are 32 KB apart, destination rows are the driver's texture pitch
apart, and no row is longer than 2 KB. A `memcpy()` handed one row cannot know
where the next row is, so the hardware prefetcher never sees the line that the
copy will need one row later, and each call re-derives a byte loop sized for a
single long run. The 3.98 ms is what that costs.

## What

`epic12_gpu_region.h` adds `epic12_gpu_copy_rows()` and the input upload calls
it once per batch. Nothing else about the batch changes.

- The **store** leads the row loop and the load follows it. Every store is a
  16-byte `__stvx_volatile` on a 16-byte boundary; the side allowed to be off is
  the `__loadunalignedvector` read of the source. Both pitches are multiples of
  16, so both sides keep their alignment down the rectangle and the split is
  decided once, outside the row loop.
- Each row asks for the row four rows ahead with `__dcbt`, one hint per 128
  bytes of width. The row four rows ahead is still a row of this same batch, so
  every hint addresses a byte of the source rows being copied. `dcbt` cannot
  fault and cannot change a value, so the hint cannot alter the frame; it only
  tells the prefetcher about a line the same copy will read a few microseconds
  later.
- A destination that is not vector aligned, a source that is not even word
  aligned, or a pitch that is not a multiple of 16 has no aligned-store form at
  all. That case falls back to one `memcpy()` per row, which is exactly what the
  caller did before this helper existed. No caller has one.
- The rectangle is exact in both directions: bytes of a destination row beyond
  `width` are never read or written, and nothing is rounded out to a vector or
  a cache line. A row that is not a whole number of vectors ends in plain bytes.

The linear (untiled) atlas upload and the tiled readback are untouched; the
readback is a different shape and already VMX.

The first version of this helper did not look like that, and the console took it
down. That is the next section.

## The console took the first version down

The image `blit-rowcopy c57ccde8 20261011-1506 diag` was handed off; on
2026-10-11 the console answered by dropping back to the dashboard. Loading
`mmpork.state` and continuing takes the console down with that image, while the
`blit-crossover` base runs the same state. The copy is the only live difference
between the two images -- the rest of that round (`e8406c35`, the per-loop pixel
census) is compiled out of both -- so the crash is this helper.

The first version hoisted the alignment split out of the row loop and then kept
the unaligned half of the pair on the **store** side:

```c
const size_t align = (size_t)in & 15;   // low bits of the source byte address
const size_t head = align ? 16 - align : 0;
...
const bool alignedStore = ((((size_t)out ^ (size_t)in) & 15) == 0);
if (alignedStore) __stvx(value, out + x, 0);
else __storeunalignedvector(value, out + x);
```

The destination is not VRAM. It is `lr.pBits`, the locked Xenos texture, which
on Xenon is write-combined memory, and `epic12_gpu_tile.h` writes that same
memory under a rule it states in its first lines: "all writes are aligned and
sequential within each 32x32 block. Never read it or use unaligned stores." The
source is `vram + min_y*8192 + min_x`, so its byte address is `4*min_x`: it is
16-byte aligned only when `min_x` is a multiple of 4, while the locked texture
row is aligned. A shared alignment was therefore the exception and an unaligned
store the usual case.

`epic12_gpu_untile.h` does use `__storeunalignedvector`, and that is the
precedent the first version took: it is legal there because that copy writes the
emulated VRAM, which is cached memory, and it is the pair the readback has
always used. The precedent is for a cached destination, and this destination is
not that one.

The standalone suite could not have caught it, and that is worth stating
plainly: its stub modelled `__storeunalignedvector` as a byte-exact store,
because that is what the instruction does on cached memory, and the test then
compared the copy against its own contract and passed. The copy and its test
agreed on the wrong assumption. Nor can the host harness reach this code: the
new copy is under `_XBOX`, and the harness builds the mock compositor, so no
host run of any kind executes this path.

## The fix: the store leads

The split stays; which side is allowed to be off is inverted.

```c
const size_t body = (width >> 4) << 4;   // whole vectors of this row
for (; x < body; x += 16)
    __stvx_volatile(__loadunalignedvector((const unsigned int*)(in + x)), out + x, 0);
for (; x < width; ++x) out[x] = in[x];   // the bytes that are not a vector
```

Every store now lands on a 16-byte boundary of the destination, the way
`epic12_gpu_tile.h` writes that memory, and the side allowed to be off is the
read of the source, which is cached VRAM. The helpers are the ones this path
already used: `__stvx_volatile` from the atlas upload, `__loadunalignedvector`
for the read. A rectangle whose destination is not vector aligned, or whose
source is not word aligned, or whose pitch is not a multiple of 16, has no
aligned-store form at all and falls back to the per-row `memcpy()` the caller
used before -- the same code the base image runs, so it cannot be worse than the
base there.

The stub now asserts the rule instead of modelling the instruction: reaching
`__storeunalignedvector` fails the test, every store must be 16-byte aligned and
inside its destination row, and every load must be word aligned and inside its
source row. That is the only part of this failure a host can express -- the
rule, not the hardware.

## Verification

`tests/epic12_gpu_region` is byte-exact in both directions and runs on the
little-endian host and on the big-endian PowerPC build:

```
python3 tests/epic12_gpu_region/run.py --toolchain-root <ppc-root> --output <out>
```

Both runs, with the fix:

```
PASS: 4044 rectangles; 10112328 exact bytes; 196769 vector stores, every one
16-byte aligned and inside its row; 0 unaligned stores; 196769 unaligned vector
loads; 20553 prefetch hints, all inside the rectangle; <endian>-endian
```

The cases are every width from 0 to 0x2100 including the vector boundaries and
the two real rectangle sizes, every row count from 0 to 9, four source offsets
(0, 4, 8, 12 -- what a VRAM word address can be, and the three that force the
source through the unaligned load), eight pitch pairs including two whose pitch
is not a multiple of 16 and must take the scalar fallback, and both shared and
unshared source/destination alignment. Every byte of the destination
allocation is compared against the contract, including the guard bytes outside
the rectangle, so a write outside `[0,width)` of a row fails the test rather
than being absorbed. An earlier draft copied the alignment head without checking
it against `width`, so a row shorter than the head wrote up to 15 bytes past the
end of the row; that case is one of the 4,044. The prefetch loop runs through a
stub that fails on any hint outside the rectangle, and the rule checks above run
in the same pass, so a version that keeps the rectangle exact while storing
unaligned now fails outright.

The other five suites are unchanged and still pass: `tests/sh3_ppc` (846,026
cases), `tests/epic12_gpu`, `tests/epic12_blit`, `tests/epic12_gpu_untile`
(6,820 rectangles), `tests/epic12_gpu_cache` (fingerprint `21d3a8b4487bb152`).
Artifacts in `/home/humor/salvia-tests/cv1k-blit-census-20261011/`.

The 60/600/1800-frame `STATE` hashes of the host harness (`0cf251c3d512ddbb`,
`baabc4def027a8a3`, `dff81882efc1b24a`) still hold, but they are not evidence
about this change: the new code is under `_XBOX`, and the host harness builds
with `EPIC12_GPU_TEST`, so the host compiles the mock compositor and never
reaches a texture lock. The host cannot measure or even execute this path, and
the crash above is the proof: the suite was green while the console went down.
What the host proves is that the copy is byte-exact and that it obeys the store
rule; the console is the judge of the rule and, after that, of the timing.

## Console protocol

The candidate is `blit-rowcopy-fix`, built from this commit; its size, sha256
and archive path are named in the docs commit that follows this one. The base is
the same `Distro360/fbneo-blit-crossover-diag.xex`
(`b5dd53ba`, sha256 `429a5c246452f821da220fa845cf6b823749d62b24ad6d5d7377b5631ae7b4eb`),
the image `Distro360/cv1000-gpu.log` came from and the image that runs this
state.

This round answers a question the last one could not: whether the console
survives the load at all. Same state, no input:

1. Load `mmpork.state` and keep playing. If the console goes back to the
   dashboard again, the fix did not remove the cause: stop and report it rather
   than spending pauses on timing. The base runs the same state, so a crash here
   is the candidate, not the state.
2. Then pause three or four times, as before, and pair the windows by the frame
   number the report prints.

- `batch_timing ... input_upload=<us>` is the number this change moves.
  `input_pixels` should be unchanged: the same bytes are copied, only faster.
- `upload_bytes` should be unchanged for the same reason.
- If `input_upload` does not fall, this candidate is dead and should be
  reverted: the row copy is not where the 3.98 ms is.
- `worker_busy` and `worker_wait` are the acceptance numbers; a frame-rate
  change that does not move them is noise.

## Revert

Two options, depending on which part has to go:

- The fix only: restore the previous body of `epic12_gpu_copy_rows()` -- the
  alignment head, the aligned load and the unaligned store -- and the stub that
  models `__storeunalignedvector` instead of failing on it. That produces the
  image that crashed, so this is the record of the rejected version, not an
  option to ship.
- The whole experiment: delete `epic12_gpu_region.h` and
  `tests/epic12_gpu_region/`, restore the per-row `memcpy()` loop in
  `epic12_gpu_xbox.h` and drop the include. `git revert c57ccde8` alone is not
  enough once the fix commits exist -- both have to go back -- and a Git revert
  never updates the runtime tree or an XEX that was already built.
