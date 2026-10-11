# CV1000 row-strided rectangle copy test

`epic12_gpu_copy_rows()` moves `width` bytes out of each of `rows` source rows
whose pitch is the VRAM pitch (32 KB) into rows whose pitch is the driver's
texture pitch. The compositor uses it for the batch bounding box on its way
into the 512x512 input texture.

Run with the extracted Debian PowerPC toolchain used by the other FBNeo tests:

```sh
python3 tests/epic12_gpu_region/run.py \
  --toolchain-root /path/to/ppc/root --output /path/to/results
```

The test is byte-exact in both directions: every byte of the destination
allocation is compared against the contract, so a write outside `[0,width)` of
a row -- including the case of a row shorter than the alignment head, which
earlier drafts got wrong -- and a read of a source byte outside the requested
rectangle are failures, not rounding differences. It covers every width from 0
to 0x2100 including the 16-byte vector boundaries and the two real rectangle
sizes, every row count from 0 to 9, both shared and unshared source/destination
alignment, eight pitch pairs including two whose pitch is not a multiple of 16
(the scalar fallback), and it checks that the source is never written.

The prefetch loop is exercised through a stub that records every hint and
fails if a hint addresses a byte outside the rectangle being copied.

Both the little-endian sanitized host run and the big-endian PowerPC run
exercise the vector branch. Their partial-store helper models the Xbox VMX128
store pair because ordinary PowerPC emulators do not implement those Xbox
instructions. No test timing is a measurement of Xbox performance.
