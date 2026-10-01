// Main emulation thread only. Integer workload counts, never per-opcode clocks.
// Only the explicit sampled specialization touches this storage in hot loops.
#ifndef FBNEO_SH3_DRC_WORK_PROFILE_H
#define FBNEO_SH3_DRC_WORK_PROFILE_H
#include <string.h>
#if defined(_MSC_VER)
typedef unsigned __int64 Sh3WorkCount;
#else
typedef unsigned long long Sh3WorkCount;
#endif
#include "sh3_fallback_detail.h"
#include "sh3_idle_candidate.h"
struct Sh3DrcWorkProfile {
 Sh3WorkCount frames, slices, cpu_off_slices, normal_mode_slices;
 Sh3WorkCount dispatch_calls, lookups, rebuilds, validation_spans, validation_words;
 Sh3WorkCount native_calls, native_cycles, interpreter_steps, interpreter_cycles;
 Sh3WorkCount movll_services, movll_service_cycles, movll_service_rejects;
 Sh3WorkCount device_services[2], device_service_cycles[2], device_fallbacks[2];
 Sh3WorkCount exit_gate, exit_fetch, exit_no_entry, exit_budget, exit_partial, exit_boundary;
 // Requested snapshot lengths, not necessarily executed opcode counts.
 Sh3WorkCount snapshot_lengths[34];
 // High-byte opcode families actually sent to the interpreter in sampled frames.
 Sh3WorkCount interpreter_hi8[256];
 Sh3FallbackDetail fallback;
 Sh3IdleCandidates idle_candidates;
 void clear() { memset(this,0,sizeof(*this)); }
};
static Sh3DrcWorkProfile sh3_drc_work;
#endif
