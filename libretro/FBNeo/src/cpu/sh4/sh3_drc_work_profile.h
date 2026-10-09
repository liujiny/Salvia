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
 // Why a compile() was requested. `conflict` is a tag/source-pointer miss (the
 // set could not hold this PC), `source` is a same-PC snapshot mismatch (the
 // guest rewrote or re-uploaded the opcodes), `map` is a read-map change only.
 // Written on the compile path, never from generated code or the hot loop.
 Sh3WorkCount rebuild_conflict, rebuild_source, rebuild_map;
 // Code-arena pressure. `arena_recycles` counts full-arena resets caused by the
 // overflow guard; `arena_peak` is the highest word count reached since the
 // last report. Both are written on the compile path only, never from
 // generated code or the dispatched hot loop.
 Sh3WorkCount arena_recycles, arena_peak;
 // The arena is a ring of sectors: entering a sector whose space is handed out
 // again invalidates the block-table slots that held its previous code.
 // `arena_evictions` counts those reuses that actually dropped slots and
 // `arena_evicted_slots` how many records they cleared. Both are compile-path
 // only, like the counters above.
 Sh3WorkCount arena_evictions, arena_evicted_slots;
 Sh3WorkCount native_calls, native_cycles, interpreter_steps, interpreter_cycles;
 Sh3WorkCount movll_services, movll_service_cycles, movll_service_rejects;
 Sh3WorkCount device_services[2], device_service_cycles[2], device_fallbacks[2];
 Sh3WorkCount exit_gate, exit_fetch, exit_no_entry, exit_budget, exit_partial, exit_boundary;
 // Requested snapshot lengths, not necessarily executed opcode counts.
 Sh3WorkCount snapshot_lengths[34];
 // High-byte opcode families actually sent to the interpreter in sampled frames.
 Sh3WorkCount interpreter_hi8[256];
#if defined(SH3_PPC_DRC_WORK_TEST)
 // Host differential builds only: exact 16-bit histogram of the opcodes the
 // interpreter fallback sites consume. Diagnostic width, never in the console
 // image, and the increment is compiled out of the console work-sample path.
 Sh3WorkCount interpreter_op16[65536];
#endif
 Sh3FallbackDetail fallback;
 Sh3IdleCandidates idle_candidates;
 void clear() { memset(this,0,sizeof(*this)); }
};
static Sh3DrcWorkProfile sh3_drc_work;
#endif
