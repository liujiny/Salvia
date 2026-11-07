// Ahead-of-time (AOT) static precompilation telemetry.
// Captures scan, queue, compile, exit reason and runtime execution coverage.
// Diagnostic telemetry only: no guest state, registers or timing are altered.
#ifndef FBNEO_SH3_AOT_PROFILE_H
#define FBNEO_SH3_AOT_PROFILE_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifndef UINT32
typedef uint32_t UINT32;
#endif
#ifndef UINT64
typedef uint64_t UINT64;
#endif
#ifndef UINT8
typedef uint8_t UINT8;
#endif

#if defined(_MSC_VER)
typedef unsigned __int64 Sh3AotCount;
#define SH3_AOT_FMT "I64u"
#else
typedef unsigned long long Sh3AotCount;
#define SH3_AOT_FMT "llu"
#endif

enum Sh3AotTrigger {
	SH3_AOT_TRIGGER_BOOT = 0,
	SH3_AOT_TRIGGER_LOAD = 1,
	SH3_AOT_TRIGGER_RESET = 2,
	SH3_AOT_TRIGGER_TEST = 3
};

enum Sh3AotExitReason {
	SH3_AOT_EXIT_QUEUE_EMPTY = 0,
	SH3_AOT_EXIT_BUDGET_REACHED = 1,
	SH3_AOT_EXIT_ARENA_FULL = 2
};

struct Sh3AotRunRecord {
	UINT32 start_pc;
	UINT32 size;
	UINT32 trigger;                  // Sh3AotTrigger (boot=0, load=1, reset=2, test=3)
	UINT32 seed_fixed;               // Vector and primary entry seeds (up to 4)
	UINT32 seed_prologues;           // Function prologues found in scan
	UINT32 seed_pointers;            // Code pointers found in scan
	UINT32 push_attempts;            // Total push() calls
	UINT32 push_success;             // Successfully enqueued
	UINT32 push_duplicate;           // Rejected by is_visited()
	UINT32 push_full;                // Rejected because queue was full
	UINT32 queue_peak;               // Peak queue depth during BFS
	UINT32 popped;                   // Items popped from queue
	UINT32 map_reject;               // Invalid page, < MAXHANDLER, or odd
	UINT32 already_compiled;         // Block already present in cache
	UINT32 compile_attempts;         // compile() called
	UINT32 compile_success;          // compile() produced non-null entry
	UINT32 compile_failed;           // compile() produced null entry
	UINT32 legacy_precompiled_count; // The value of precompiled_count at exit
	UINT32 exit_reason;              // Sh3AotExitReason (queue_empty=0, budget=1, arena=2)
	UINT32 queue_remaining;          // Items left in queue when loop exited
	UINT32 words_added;              // PPC code words added to arena (used_after - used_before)
	UINT32 slots_replaced;           // Cache slots that already held an entry
	UINT32 sync_calls;               // sync_code calls during this AOT run
	UINT32 sync_words;               // Total PPC code words synced
	UINT32 arena_used_after;         // Total arena words used after this run
	UINT64 duration_ticks;           // Hardware timer ticks
};

struct Sh3AotProfile {
	enum { MAX_RUNS = 8 };
	Sh3AotRunRecord runs[MAX_RUNS];
	unsigned run_count;

	// Cumulative block metrics across session
	Sh3AotCount total_aot_compiled;      // AOT blocks successfully compiled
	Sh3AotCount total_aot_executed;      // AOT blocks executed at least once
	Sh3AotCount total_aot_replaced_cold; // AOT blocks evicted/overwritten before being executed
	Sh3AotCount total_jit_compiled;      // Runtime blocks compiled by JIT
	Sh3AotCount total_jit_executed;      // Runtime JIT blocks executed

	void clear() {
		memset(this, 0, sizeof(*this));
	}

	void record_run(const Sh3AotRunRecord &r) {
		if (run_count < MAX_RUNS) {
			runs[run_count++] = r;
		} else {
			for (unsigned i = 0; i < MAX_RUNS - 1; ++i) runs[i] = runs[i + 1];
			runs[MAX_RUNS - 1] = r;
		}
		total_aot_compiled += r.compile_success;
	}
};

extern Sh3AotProfile sh3_aot_profile;

static inline UINT64 sh3_aot_timer_ticks() {
#if defined(_XBOX)
	LARGE_INTEGER now;
	if (QueryPerformanceCounter(&now) && now.QuadPart > 0) return (UINT64)now.QuadPart;
	return 0;
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86) || defined(__powerpc__) || defined(__ppc__))
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (UINT64)ts.tv_sec * 1000000000ULL + (UINT64)ts.tv_nsec;
#else
	return 0;
#endif
}

static inline double sh3_aot_ticks_to_ms(UINT64 ticks) {
#if defined(_XBOX)
	LARGE_INTEGER freq;
	if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0) {
		return (double)ticks * 1000.0 / (double)freq.QuadPart;
	}
	return 0.0;
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86) || defined(__powerpc__) || defined(__ppc__))
	return (double)ticks / 1000000.0;
#else
	return 0.0;
#endif
}

static inline void Sh3AotReport(void (*emit)(const char*))
{
	if (!emit) return;
	char text[768];
	for (unsigned i = 0; i < sh3_aot_profile.run_count; ++i) {
		const Sh3AotRunRecord &r = sh3_aot_profile.runs[i];
		const char *trig_str = (r.trigger == SH3_AOT_TRIGGER_BOOT) ? "boot" :
		                       (r.trigger == SH3_AOT_TRIGGER_LOAD) ? "load" :
		                       (r.trigger == SH3_AOT_TRIGGER_RESET) ? "reset" : "test";
		const char *exit_str = (r.exit_reason == SH3_AOT_EXIT_QUEUE_EMPTY) ? "queue_empty" :
		                       (r.exit_reason == SH3_AOT_EXIT_BUDGET_REACHED) ? "budget_reached" :
		                       (r.exit_reason == SH3_AOT_EXIT_ARENA_FULL) ? "arena_full" : "unknown";
		double ms = sh3_aot_ticks_to_ms(r.duration_ticks);
		sprintf(text, "aot_run id=%u trigger=%s range=0x%08X..0x%08X size=%uKB seeds=%u (fixed=%u prol=%u ptr=%u) push_att=%u push_ok=%u push_dup=%u push_full=%u q_peak=%u popped=%u map_rej=%u already=%u compiled=%u fail=%u legacy_cnt=%u exit=%s q_rem=%u words=+%u slots_rep=%u time_ms=%.3f syncs=%u sync_words=%u arena_used=%u",
			i + 1, trig_str, r.start_pc, r.start_pc + r.size, r.size / 1024,
			r.seed_fixed + r.seed_prologues + r.seed_pointers,
			r.seed_fixed, r.seed_prologues, r.seed_pointers,
			r.push_attempts, r.push_success, r.push_duplicate, r.push_full,
			r.queue_peak, r.popped, r.map_reject, r.already_compiled,
			r.compile_success, r.compile_failed, r.legacy_precompiled_count,
			exit_str, r.queue_remaining, r.words_added, r.slots_replaced,
			ms, r.sync_calls, r.sync_words, r.arena_used_after);
		emit(text);
	}
	double exec_pct = sh3_aot_profile.total_aot_compiled ?
		(double)sh3_aot_profile.total_aot_executed * 100.0 / (double)sh3_aot_profile.total_aot_compiled : 0.0;
	sprintf(text, "aot_summary total_aot_compiled=%" SH3_AOT_FMT " aot_executed=%" SH3_AOT_FMT " aot_executed_pct=%.1f aot_cold_evicted=%" SH3_AOT_FMT " jit_compiled=%" SH3_AOT_FMT " runs=%u",
		sh3_aot_profile.total_aot_compiled,
		sh3_aot_profile.total_aot_executed,
		exec_pct,
		sh3_aot_profile.total_aot_replaced_cold,
		sh3_aot_profile.total_jit_compiled,
		sh3_aot_profile.run_count);
	emit(text);
}

#endif // FBNEO_SH3_AOT_PROFILE_H
