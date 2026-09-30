# SH3 sampled-workload regression

`test.cpp` checks the exact 1/256 selector against existing 1/64 timing, window resets,
64-bit counters and bounded storage. The sibling sh3_dispatch suite compares measured
and unmeasured production dispatch and verifies an ordinary warm entry makes no
profiling writes. Run `../sh3_dispatch/run_inline.py --output /tmp/cv1k-work`.

`run_outer.py --baseline-sh4 <preserved pre-instrumentation sh4.cpp> --output <dir>`
extracts the actual production outer-loop template and the original function, then
executes both with the synthetic callbacks in `outer_fixture.h`. The saved baseline
must be from before the workload-counter template was introduced. Generated C++ and
compiler/run logs live in the requested output directory, not the checkout.

These checks preserve source control flow, state and guest-cycle semantics. They do
not execute PPC machine code, measure console overhead, or replay a game. Native
false/true specialization inspection and real hardware logs are separate evidence.
