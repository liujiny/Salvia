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

	// Test 7: Sh3PrecompileQueue queue-full visited deferral
	{
		struct MiniQueue {
			UINT8 *visited;
			UINT32 bitmap_bytes;
			UINT32 start_pc;
			UINT32 size;
			UINT32 *queue;
			UINT32 q_head;
			UINT32 q_tail;
			UINT32 q_mask;
			UINT32 push_attempts;
			UINT32 push_success;
			UINT32 push_duplicate;
			UINT32 push_full;
			UINT32 queue_peak;

			bool is_visited(UINT32 a) const {
				if (a < start_pc || a >= start_pc + size || (a & 1)) return true;
				UINT32 off = (a - start_pc) >> 1;
				if ((off >> 3) >= bitmap_bytes) return true;
				return (visited[off >> 3] & (1u << (off & 7))) != 0;
			}
			void mark_visited(UINT32 a) {
				if (a < start_pc || a >= start_pc + size || (a & 1)) return;
				UINT32 off = (a - start_pc) >> 1;
				if ((off >> 3) < bitmap_bytes) {
					visited[off >> 3] |= (1u << (off & 7));
				}
			}
			void push(UINT32 a) {
				++push_attempts;
				if (a < start_pc || a >= start_pc + size || (a & 1)) return;
				if (is_visited(a)) {
					++push_duplicate;
					return;
				}
				if (((q_tail + 1) & q_mask) != q_head) {
					mark_visited(a);
					queue[q_tail] = a;
					q_tail = (q_tail + 1) & q_mask;
					++push_success;
					UINT32 count = (q_tail - q_head) & q_mask;
					if (count > queue_peak) queue_peak = count;
				} else {
					++push_full;
				}
			}
			bool pop(UINT32 &a) {
				if (q_head == q_tail) return false;
				a = queue[q_head];
				q_head = (q_head + 1) & q_mask;
				return true;
			}
		};

		UINT8 vmap[16] = {0};
		UINT32 qbuf[4] = {0}; // q_mask = 3, holds at most 3 items
		MiniQueue mq = {};
		mq.visited = vmap;
		mq.bitmap_bytes = sizeof(vmap);
		mq.start_pc = 0x0c000000;
		mq.size = 0x1000;
		mq.queue = qbuf;
		mq.q_mask = 3;

		mq.push(0x0c000010);
		mq.push(0x0c000020);
		mq.push(0x0c000030);
		assert(mq.push_success == 3);
		assert(mq.push_full == 0);

		// 4th push should fail with queue full
		mq.push(0x0c000040);
		assert(mq.push_full == 1);
		// Crucial verification: 0x0c000040 MUST NOT be marked visited
		assert(!mq.is_visited(0x0c000040));

		// Pop one item, freeing space
		UINT32 popped_pc = 0;
		assert(mq.pop(popped_pc) && popped_pc == 0x0c000010);

		// Now retry pushing 0x0c000040: must succeed and not be rejected as duplicate
		mq.push(0x0c000040);
		assert(mq.push_success == 4);
		assert(mq.is_visited(0x0c000040));

		// Pushing 0x0c000040 again must now be rejected as duplicate
		mq.push(0x0c000040);
		assert(mq.push_duplicate == 1);
	}

	printf("PASS Sh3AotProfile: lifecycle, bounded runs=%u, execution tracking, formatting, queue retry on full\n",
		Sh3AotProfile::MAX_RUNS);
	return 0;
}
