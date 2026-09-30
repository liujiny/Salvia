# Xbox asynchronous presentation regression

`test_async_present.c` exercises the production pure C presentation controller.
The mock separates GPU completion from VBlank scanout: an asynchronous fence
may complete while its texture remains queued or visible. It asserts that the
controller never overwrites that texture or frees it before a synchronous
Present to the automatic front buffer has completed.

Build and run from the repository root:

```sh
cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  libs/libSDLx360/tests/async_present/test_async_present.c -o /tmp/test-async-present
/tmp/test-async-present
```

Coverage: alternating buffers, one pending swap, idempotent enable, allocation
failure and sticky fallback, Guide fallback, asynchronous submission failure,
pending-swap timeout, retirement-fence timeout, deferred reset, black clear only
for forced reset Present, VSync disabled, unsigned time/sequence wrap, and
100,000 deterministic request/Guide/reset/VSync/failure events.

The Xbox adapter additionally needs native XDK compilation and hardware testing.
This regression cannot establish frame rate, display timing, or Guide behavior
on a console. Inspect the pause-time `present_state` record in
`cv1000-gpu.log` alongside the GPU timing buckets: activation should report
`active=1`, and fallback must retain a safe display. The live Present path does
not perform file I/O.
