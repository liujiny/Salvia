# Optional FBNeo state streaming

`RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK` exposes `fbneo_state_stream_v1`.
Its C ABI is declared in `retro_memory.h`: mode 0 saves, mode 1 loads, and each
callback transfers its complete block of at most 65536 bytes. The state size
and byte sequence are exactly those from the ordinary libretro state methods.
Call synchronously while emulation is paused. A loading caller must validate
its complete file first; streaming cannot roll back a partly restored state
after an I/O error without another state-sized allocation.

Run the transport regression on the host and big-endian PPC:

```sh
python3 libretro/FBNeo/tests/state_stream/run.py --toolchain-root /path/to/ppc/root --output /tmp/state-stream
```

The runner compiles the actual serialization section from `retro_memory.cpp`
with a synthetic driver and the real state/context definitions. It compares
streaming output against the actual legacy serializer byte for byte, restores
the complete state, and checks all five contexts, area/chunk boundaries,
callback failures, incorrect sizes, invalid areas, reentry and recovery.
It does not substitute for a full driver save/load regression or console I/O.
