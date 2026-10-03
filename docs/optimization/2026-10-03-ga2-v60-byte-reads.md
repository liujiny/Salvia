# GA2: Xbox V60 byte-aligned instruction reads

Baseline: `7df834cbd38f577f0b0ed95ee6e361d77c160761` (stable silent FBNeo).

V60 has variable-length instructions: their 16/32-bit parameters need not be aligned. The original mapped fetches dereferenced UINT16/UINT32 pointers. In a 2,000-frame GA2 replay there were 29,621,232 odd-address 16-bit fetches, versus 1,147,100 even-address fetches. Xbox compiler 16.00.11886 emits `lhz` for the old 16-bit read and `lwbrx` for the old 32-bit read; the replacement emits byte loads (`lbz`) and combines them in little-endian order. This removes unaligned multibyte host accesses. Actual Xbox speedup is not measured yet.

Only Xbox builds (or the explicit host test define) use the new branch. Mapped address masking, the existing contiguous mapped-page access semantics, handler dispatch, cycles, interrupts and CPU state are preserved. The V60 word data-read path receives the same treatment. No runtime logging or CPU clock changes were added.

## Validation

- `python3 libretro/FBNeo/tests/v60_byte_reads/test.py`: PASS under AddressSanitizer/UndefinedBehaviorSanitizer. All offsets, address masking, contiguous page ends, handler calls, absent handlers.
- Native libretro GA2 replay: 6,000 frames from one baseline savestate with scripted controls. The combined candidate (this CPU change plus the following mixer experiment) matched baseline video/audio hashes for every frame and the final serialized state. This is host emulation evidence, not Xbox execution evidence.
- Xbox compiler assembly probe confirms byte loads rather than unaligned `lhz`/`lwbrx` in the new read bodies.
- Xbox FBNeo Release and Salvia Release_finalburn build passed, only FBNeo and its frontend were built. XEX2 output: 34,611,200 bytes.

XEX SHA256: `56b39f03c5c24b776cda13e80adcd2aeb4eb0797c9a51dad6fbc5fbf7391c4d7`.

Local baseline and CPU-only XEX backups: `/home/humor/salvia-tests/ga2/fbneo-stable-before.xex`, `fbneo-v60-byte.xex`. ROMs, replay output, profiler sources and artifacts are outside Git.

Rollback: `git revert <commit containing this document>`; mirror `libretro/FBNeo/src/cpu/v60/v60.cpp` to sibling `../Salvia` and rebuild, or restore the verified baseline XEX. Reverting Git alone does not replace the deployed XEX.
