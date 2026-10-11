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
a row and a read of a source byte outside the requested rectangle are failures,
not rounding differences. It covers every width from 0 to 0x2100 including the
16-byte vector boundaries and the two real rectangle sizes, every row count from
0 to 9, both shared and unshared source/destination alignment, eight pitch pairs
including two whose pitch is not a multiple of 16 (the scalar fallback), and it
checks that the source is never written.

The stubs do not only model the intrinsics, they assert the rules the intrinsics
depend on, because getting one of them wrong is not a rounding difference on
this hardware:

- the destination is a locked texture, i.e. write-combined memory on Xenon, so
  `__storeunalignedvector` fails the test if it is ever reached. The first
  version of this copy used it, and the console dropped to the dashboard as
  soon as a state exercised that branch;
- every store is checked to be 16-byte aligned and inside its destination row,
  and every load to be word aligned and inside its source row;
- the prefetch loop records every hint and fails if one addresses a byte outside
  the rectangle being copied.

Both the little-endian sanitized host run and the big-endian PowerPC run
exercise the vector branch. Where the SDK would use the unaligned vector load
pair -- Xbox VMX128 instructions that ordinary PowerPC emulators do not
implement -- the stub performs the same byte-exact load in portable C. No test
timing is a measurement of Xbox performance.
