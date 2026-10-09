// Xbox 360 CV1000 phase probes. Console diagnostic builds only; every probe is
// compiled out of the normal image. The pause report prints and clears them.
#ifndef SALVIA_CV1K_PROBE_H
#define SALVIA_CV1K_PROBE_H
#include "salvia_fbneo_diagnostics.h"
#if SALVIA_FBNEO_DIAGNOSTICS && defined(_XBOX)
#define SALVIA_CV1K_PROBE 1
// [0]/[1] blitter worker job ticks/jobs, [2]/[3] main-thread blit write
// ticks/calls, [4]/[5] chained dispatcher ticks/calls, [6]/[7] block entry
// ticks/samples, [8]/[9] idle-device MOV.L service ticks/samples,
// [10]/[11] sh4_run_timers ticks/callback count,
// [12] dispatcher pre-entry ticks (fetch, lookup, validation, rebuild) and
// [13] post-entry ticks (service dispatch, cycle accounting), both over the
// same 1-in-64 sample as the block entry, so the three spans can be compared
// with the whole drc_dispatch phase and the unattributed remainder shows up.
// The clock helper is defined beside the counters in epic12.cpp: the SH4 core
// defines an opcode function named FLOAT, which the XDK's windef.h typedef
// would collide with, so this header must not pull in <xtl.h>.
extern "C" {
	extern unsigned long long salvia_cv1k_probe[16];
	extern unsigned long long salvia_cv1k_tick(void);
}
#else
#define SALVIA_CV1K_PROBE 0
#endif
#endif
