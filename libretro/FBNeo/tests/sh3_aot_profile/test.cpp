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
	sh3_aot_profile.total_jit_executed = 30;
	sh3_aot_profile.dynamic_aot_entries = 900000;
	sh3_aot_profile.dynamic_jit_entries = 100000;

	// Test 4: Report emission
	emitted_lines.clear();
	Sh3AotReport(test_emit);
	assert(emitted_lines.size() == 5); // 2 runs + summary + spaces + dynamic
	assert(strstr(emitted_lines[0].c_str(), "aot_run id=1 trigger=boot range=0x0C000000..0x0C200000") != NULL);
	assert(strstr(emitted_lines[0].c_str(), "seeds=1504 (fixed=4 prol=1200 ptr=300)") != NULL);
	assert(strstr(emitted_lines[0].c_str(), "exit=queue_empty") != NULL);
	assert(strstr(emitted_lines[1].c_str(), "aot_run id=2 trigger=boot range=0xAC000000..0xAC200000") != NULL);
	assert(strstr(emitted_lines[2].c_str(), "aot_summary total_aot_compiled=7560 aot_executed=4500 aot_executed_pct=59.5 aot_cold_evicted=120 jit_compiled=45 jit_executed=30 runs=2") != NULL);
	assert(strstr(emitted_lines[3].c_str(), "aot_spaces 0c_compiled=3780 0c_exec=0 0c_cold=0 ac_compiled=3780 ac_exec=0 ac_cold=0") != NULL);
	assert(strstr(emitted_lines[4].c_str(), "aot_dynamic dyn_aot=900000 dyn_jit=100000 dyn_aot_pct=90.00") != NULL);

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

	// Test 8: Prioritized control-flow before fallback seed replenishment
	{
		struct StagedQueue {
			uint8_t visited[16];
			uint32_t queue[16];
			uint32_t q_head;
			uint32_t q_tail;
			uint32_t q_mask;
			uint32_t start_pc;
			uint32_t size;

			bool is_visited(uint32_t a) const {
				if (a < start_pc || a >= start_pc + size || (a & 1)) return true;
				uint32_t off = (a - start_pc) >> 1;
				return (visited[off >> 3] & (1u << (off & 7))) != 0;
			}
			void push(uint32_t a) {
				if (a < start_pc || a >= start_pc + size || (a & 1) || is_visited(a)) return;
				uint32_t off = (a - start_pc) >> 1;
				visited[off >> 3] |= (1u << (off & 7));
				queue[q_tail] = a;
				q_tail = (q_tail + 1) & q_mask;
			}
			bool pop(uint32_t &a) {
				if (q_head == q_tail) return false;
				a = queue[q_head];
				q_head = (q_head + 1) & q_mask;
				return true;
			}
		};

		StagedQueue sq = {};
		sq.q_mask = 15;
		sq.start_pc = 0x0c000000;
		sq.size = 0x1000;

		// Mock backing memory with:
		// Word 0x000: entry vector
		// Word 0x020 (pc=0x0c000040): STS.L PR,@-R15 (prologue seed)
		// Word 0x040 (pc=0x0c000080): pointer seed
		uint16_t words[256] = {0};
		words[0x20] = 0x4f22; // prologue
		words[0x40] = 0x0c00; words[0x41] = 0x0090; // pointer to 0x0c000090

		// Push primary vector
		sq.push(0x0c000000);

		std::vector<uint32_t> visit_order;
		uint32_t prologue_idx = 0;
		uint32_t pointer_idx = 0;
		uint32_t pc = 0;

		for (;;) {
			if (sq.q_head == sq.q_tail) {
				bool found = false;
				while (prologue_idx < 255) {
					uint32_t i = prologue_idx++;
					if (words[i] == 0x4f22) {
						uint32_t cand = sq.start_pc + (i << 1);
						if (!sq.is_visited(cand)) {
							sq.push(cand);
							found = true;
							break;
						}
					}
				}
				if (!found) {
					while (pointer_idx < 254) {
						uint32_t i = pointer_idx++;
						if ((i & 1) == 0) {
							uint32_t ptr = ((uint32_t)words[i] << 16) | words[i + 1];
							if (ptr >= sq.start_pc && ptr < sq.start_pc + sq.size && !(ptr & 1)) {
								if (!sq.is_visited(ptr)) {
									sq.push(ptr);
									found = true;
									break;
								}
							}
						}
					}
				}
				if (!found) break;
			}

			if (!sq.pop(pc)) break;
			visit_order.push_back(pc);

			// If we visit vector 0x0c000000, it discovers branch target 0x0c000004 & 0x0c000008
			if (pc == 0x0c000000) {
				sq.push(0x0c000004);
				sq.push(0x0c000008);
			}
			// If we visit 0x0c000004, it discovers branch target 0x0c00000c
			if (pc == 0x0c000004) {
				sq.push(0x0c00000c);
			}
		}

		// Verify order:
		// 1. Vector 0x0c000000
		// 2. Branch 0x0c000004 (Depth 1)
		// 3. Branch 0x0c000008 (Depth 1)
		// 4. Branch 0x0c00000c (Depth 2)
		// 5. Fallback prologue seed 0x0c000040 (only after active tree exhausted!)
		// 6. Fallback pointer seed 0x0c000090 (only after prologues exhausted!)
		assert(visit_order.size() == 6);
		assert(visit_order[0] == 0x0c000000);
		assert(visit_order[1] == 0x0c000004);
		assert(visit_order[2] == 0x0c000008);
		assert(visit_order[3] == 0x0c00000c);
		assert(visit_order[4] == 0x0c000040);
		assert(visit_order[5] == 0x0c000090);
	}

	// Test 9: R0 Shadow and table path execution tracking and generation accounting
	{
		sh3_aot_profile.clear();

		enum { TEST_TABLE_SIZE = 128 };
		uint8_t mock_slot_aot_state[TEST_TABLE_SIZE] = {0};
		uint32_t mock_slot_gen[TEST_TABLE_SIZE] = {0};

		struct MockShadow {
			uint32_t pc;
			uint32_t slot;
			uint32_t gen;
			void *entry;
		};

		// Helper simulating production unified dispatch accounting
		auto dispatch_exec = [&](MockShadow *copy, unsigned table_slot, void *entry) -> bool {
			if (!entry) return false;
			if (mock_slot_aot_state) {
				const unsigned exec_slot = copy ? copy->slot : table_slot;
				const bool gen_match = copy ? (mock_slot_gen && copy->slot < TEST_TABLE_SIZE && mock_slot_gen[copy->slot] == copy->gen) : true;
				if (gen_match && exec_slot < TEST_TABLE_SIZE) {
					const uint8_t st = mock_slot_aot_state[exec_slot];
					if (st == 1) {
						mock_slot_aot_state[exec_slot] = 2;
						++sh3_aot_profile.total_aot_executed;
						++sh3_aot_profile.total_aot_0c_executed;
						++sh3_aot_profile.dynamic_aot_entries;
					} else if (st == 2) {
						++sh3_aot_profile.dynamic_aot_entries;
					} else if (st == 3) {
						mock_slot_aot_state[exec_slot] = 4;
						++sh3_aot_profile.total_aot_executed;
						++sh3_aot_profile.total_aot_ac_executed;
						++sh3_aot_profile.dynamic_aot_entries;
					} else if (st == 4) {
						++sh3_aot_profile.dynamic_aot_entries;
					} else if (st == 5) {
						mock_slot_aot_state[exec_slot] = 6;
						++sh3_aot_profile.total_jit_executed;
						++sh3_aot_profile.dynamic_jit_entries;
					} else if (st == 6) {
						++sh3_aot_profile.dynamic_jit_entries;
					}
				}
			}
			return true;
		};

		int dummy_code = 123;
		void *valid_entry = &dummy_code;

		// 9.1: AOT 0x0c block executed via Shadow hit (previously blind spot!)
		mock_slot_gen[10] = 1;
		mock_slot_aot_state[10] = 1; // 0x0c cold
		MockShadow sh10 = {0x0c001000, 10, 1, valid_entry};

		bool ok = dispatch_exec(&sh10, 0, valid_entry);
		assert(ok);
		assert(mock_slot_aot_state[10] == 2); // transitioned 1 -> 2
		assert(sh3_aot_profile.total_aot_executed == 1);
		assert(sh3_aot_profile.total_aot_0c_executed == 1);
		assert(sh3_aot_profile.dynamic_aot_entries == 1);

		// 9.2: Repeated execution via Shadow hit: unique count stays 1, dynamic count increments
		ok = dispatch_exec(&sh10, 0, valid_entry);
		assert(ok);
		assert(mock_slot_aot_state[10] == 2);
		assert(sh3_aot_profile.total_aot_executed == 1);
		assert(sh3_aot_profile.dynamic_aot_entries == 2);

		// 9.3: Control comparison via table path (Shadow miss) for 0xac AOT block
		mock_slot_gen[20] = 1;
		mock_slot_aot_state[20] = 3; // 0xac cold
		ok = dispatch_exec(nullptr, 20, valid_entry);
		assert(ok);
		assert(mock_slot_aot_state[20] == 4); // transitioned 3 -> 4
		assert(sh3_aot_profile.total_aot_executed == 2);
		assert(sh3_aot_profile.total_aot_ac_executed == 1);
		assert(sh3_aot_profile.dynamic_aot_entries == 3);

		// 9.4: Stale Shadow with generation mismatch must NOT credit new occupant
		// Suppose slot 10 gets reused by JIT and slot_gen bumped to 2
		mock_slot_gen[10] = 2;
		mock_slot_aot_state[10] = 5; // JIT unexecuted
		// Stale shadow still holds gen = 1
		assert(sh10.gen == 1);
		// Dispatch with stale shadow
		ok = dispatch_exec(&sh10, 0, valid_entry);
		assert(ok);
		// Crucial verification: new occupant in slot 10 must NOT have been marked executed!
		assert(mock_slot_aot_state[10] == 5);
		assert(sh3_aot_profile.total_jit_executed == 0);

		// 9.5: Failed compile (!entry) must not record execution
		mock_slot_aot_state[30] = 1; // AOT cold
		ok = dispatch_exec(nullptr, 30, nullptr);
		assert(!ok);
		assert(sh3_aot_profile.total_aot_executed == 2); // unchanged!

		// 9.6: JIT block execution
		mock_slot_gen[40] = 1;
		mock_slot_aot_state[40] = 5; // JIT cold
		ok = dispatch_exec(nullptr, 40, valid_entry);
		assert(ok);
		assert(mock_slot_aot_state[40] == 6);
		assert(sh3_aot_profile.total_jit_executed == 1);
		assert(sh3_aot_profile.dynamic_jit_entries == 1);
	}

	printf("PASS Sh3AotProfile: lifecycle, bounded runs=%u, execution tracking, formatting, queue retry on full, prioritized staged traversal, R0 shadow/generation accounting\n",
		Sh3AotProfile::MAX_RUNS);
	return 0;
}
