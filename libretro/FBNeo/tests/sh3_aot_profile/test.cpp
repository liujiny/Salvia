#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <vector>
#include <string>

#include "../../src/cpu/sh4/sh3_aot_profile.h"

Sh3AotProfile sh3_aot_profile;

static std::vector<std::string> emitted_lines;
static void test_emit(const char *text) {
	emitted_lines.push_back(text);
}

int main() {
	sh3_aot_profile.clear();
	assert(sh3_aot_profile.run_count == 0);
	assert(sh3_aot_profile.total_aot_compiled == 0);
	assert(sh3_aot_profile.total_aot_executed == 0);

	// Test 1: Record initial boot run
	Sh3AotRunRecord r1 = {};
	r1.start_pc = 0x0c000000;
	r1.size = 0x200000;
	r1.trigger = SH3_AOT_TRIGGER_BOOT;
	r1.seed_fixed = 4;
	r1.seed_prologues = 1200;
	r1.seed_pointers = 300;
	r1.push_attempts = 4500;
	r1.push_success = 3800;
	r1.push_duplicate = 700;
	r1.push_full = 0;
	r1.queue_peak = 2100;
	r1.popped = 3800;
	r1.map_reject = 0;
	r1.already_compiled = 10;
	r1.compile_attempts = 3790;
	r1.compile_success = 3780;
	r1.compile_failed = 10;
	r1.legacy_precompiled_count = 7570;
	r1.exit_reason = SH3_AOT_EXIT_QUEUE_EMPTY;
	r1.queue_remaining = 0;
	r1.words_added = 65000;
	r1.slots_replaced = 0;
	r1.sync_calls = 3781;
	r1.sync_words = 130000;
	r1.arena_used_after = 65000;
	r1.duration_ticks = 15000000ULL; // 15ms in ns

	sh3_aot_profile.record_run(r1);
	assert(sh3_aot_profile.run_count == 1);
	assert(sh3_aot_profile.total_aot_compiled == 3780);

	// Test 2: Record second segment (0xac000000)
	Sh3AotRunRecord r2 = r1;
	r2.start_pc = 0xac000000;
	r2.compile_success = 3780;
	r2.words_added = 65000;
	r2.arena_used_after = 130000;
	sh3_aot_profile.record_run(r2);
	assert(sh3_aot_profile.run_count == 2);
	assert(sh3_aot_profile.total_aot_compiled == 7560);

	// Test 3: Simulate execution and replacements
	sh3_aot_profile.total_aot_executed = 4500;
	sh3_aot_profile.total_aot_replaced_cold = 120;
	sh3_aot_profile.total_jit_compiled = 45;

	// Test 4: Report emission
	emitted_lines.clear();
	Sh3AotReport(test_emit);
	assert(emitted_lines.size() == 3); // 2 runs + 1 summary
	assert(strstr(emitted_lines[0].c_str(), "aot_run id=1 trigger=boot range=0x0C000000..0x0C200000") != NULL);
	assert(strstr(emitted_lines[0].c_str(), "seeds=1504 (fixed=4 prol=1200 ptr=300)") != NULL);
	assert(strstr(emitted_lines[0].c_str(), "exit=queue_empty") != NULL);
	assert(strstr(emitted_lines[1].c_str(), "aot_run id=2 trigger=boot range=0xAC000000..0xAC200000") != NULL);
	assert(strstr(emitted_lines[2].c_str(), "aot_summary total_aot_compiled=7560 aot_executed=4500 aot_executed_pct=59.5 aot_cold_evicted=120 jit_compiled=45 runs=2") != NULL);

	// Test 5: Verify ring buffer bound (MAX_RUNS = 8)
	for (unsigned i = 0; i < 10; ++i) {
		Sh3AotRunRecord rx = r1;
		rx.start_pc = 0x10000000 + i * 0x100000;
		sh3_aot_profile.record_run(rx);
	}
	assert(sh3_aot_profile.run_count == Sh3AotProfile::MAX_RUNS);
	// Last record should be 0x10000000 + 9 * 0x100000 = 0x10900000
	assert(sh3_aot_profile.runs[Sh3AotProfile::MAX_RUNS - 1].start_pc == 0x10900000);

	// Test 6: Clear
	sh3_aot_profile.clear();
	assert(sh3_aot_profile.run_count == 0);
	assert(sh3_aot_profile.total_aot_compiled == 0);
	assert(sh3_aot_profile.total_aot_executed == 0);

	printf("PASS Sh3AotProfile: lifecycle, bounded runs=%u, execution tracking, formatting\n",
		Sh3AotProfile::MAX_RUNS);
	return 0;
}
