# Frontend save-state regression

Run from the Salvia checkout:

```sh
python3 libretro/FBNeo/tests/frontend_state/run.py --sanitize --output /tmp/frontend-state-test
```

Requires a C++98 compiler, pthreads, and zlib development headers. The test includes
the production `src/io/statesram.h` and `statefile.h`; small SDL, UI, and core mocks
replace external frontend dependencies. It is a host correctness test, not an
Xbox 360 timing measurement.

Coverage:

- FBNeo streaming and the ordinary libretro buffer path preserve the gzip core
  bytes plus optional native-endian `RCHV` trailer used by existing states.
- A 151 MiB fake core saves and restores while frontend allocations above 64 KiB
  are denied. The live emulated memory remains allocated; no full state copy is
  created. Small achievement metadata and zlib's bounded workspace are additional.
- Gzip CRC corruption, truncated files, and core load failure never report success;
  invalid files are rejected before the mock core changes.
- Save/deflate failure, including a `/dev/full` finalization failure, preserves the
  previous slot. The temporary file is closed and synced before replacement.
- Screenshot allocation failure is nonfatal and does not discard the saved state.
- Worker buffers are detached under the mutex, a second request cannot replace an
  active job, UI completion runs on the main thread, and shutdown drains pending
  SRAM. Legacy files without achievement metadata still load.

The optional `fbneo_state_stream_v1` extension is used only for FBNeo. A streaming
save runs synchronously between emulated frames; the frontend displays a waiting
message and suspends audio until it finishes. This is a save/load reliability fix,
not a frame-rate improvement. Loading validates the complete gzip file first and
rewinds the same open file for restoration. A storage failure during the second
read is reported, but cannot roll back without retaining another full state.

Xbox diagnostics append stages, byte counts, and available memory to
`game:\fbneo-state.log` next to the executable. Native compilation and actual core
stream/buffer compatibility are tested separately in `tests/state_stream`.
