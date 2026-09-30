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

## Transient forced-sync recovery

Run `python3 libs/libSDLx360/tests/async_present/run_recovery.py --output /tmp/salvia-async-recovery`
from the repository root. It executes the policy and extracted notification-adapter tests,
each optimized and with ASan/UBSan. The adapter test includes the actual listener/poll
function bodies with modeled XNotify calls; it is not a hardware notification test.

Recovery requires a forced-sync failure, an explicitly observed system-UI open/close
cycle, at least 1000 ms of stable closure, retirement of the old front buffer, and a
bounded fence for the last synchronous Present. One close epoch permits one attempt.
Create failures, queue timeouts, submission failures and retirement/recovery-fence
timeouts remain sticky until an explicit off/on request. Unknown UI state or a failed
listener cannot authorize a retry. Healthy async frames never poll UI notifications.

Tests cover open UI, duplicate close notifications, repeated forced swaps, stale close
tokens, reopening during cooldown, queue-drain limits, notification-handle failures,
resource lifetime, OOM on retry, cancellation, VSync off, timer/epoch wrap and the
existing 100000-event stress sequence. Raw test outputs stay outside the repository.
