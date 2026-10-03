// Exercise the real Z80 core with batching enabled/disabled, without game ROMs.
#include "burnint.h"
#include "z80.h"
#include <assert.h>
#include <stdio.h>

static INT32 quiet(INT32, TCHAR *, ...) { return 0; }
INT32 (__cdecl *bprintf)(INT32, TCHAR *, ...) = quiet;
INT32 (__cdecl *BurnAcb)(BurnArea *) = NULL;
INT32 z80daisy_has_ctc = 0;
void z80daisy_exit() {}
void z80daisy_scan(int) {}
void z80daisy_reset(const z80_irq_daisy_chain *) { abort(); }
int z80daisy_update_irq_state(const z80_irq_daisy_chain *) { abort(); }
int z80daisy_call_ack_device(const z80_irq_daisy_chain *) { abort(); }
void z80daisy_call_reti_device(const z80_irq_daisy_chain *) { abort(); }
void z80ctc_timer_update(int) { abort(); }

static UINT8 memory[65536], separate[256];
static UINT8 *ops[256], *args[256];
static UINT8 status;
static int busy_until, reads, stop_on_read, irq_on_read;
static int irq_ack(int) { return 0xff; }
static UINT8 __fastcall read_memory(unsigned int a) { return memory[a & 65535]; }
static void __fastcall write_memory(unsigned int a, UINT8 v) { memory[a & 65535] = v; }
static UINT8 __fastcall read_op(unsigned int a) { return ops[(a >> 8) & 255][a & 255]; }
static UINT8 __fastcall read_arg(unsigned int a) { return args[(a >> 8) & 255][a & 255]; }
static UINT8 __fastcall read_port(unsigned int) {
    reads++;
    if (stop_on_read) Z80StopExecute();
    if (irq_on_read) Z80SetIrqLine(0, Z80_ASSERT_LINE);
    return status | (z80TotalCycles() < busy_until ? 0x80 : 0);
}
static void __fastcall write_port(unsigned int, UINT8) {}

struct Result { Z80_Regs regs; int cycles, tstates, reads, lastop; };
static Result run(const Z80_Regs &seed, int budget, const Z80StableStatusPoll *cfg) {
    Z80Reset(); Z80SetContext((void *)&seed); Z80SetStableStatusPoll(cfg);
    reads = 0;
    Result result = {};
    result.cycles = Z80Execute(budget);
    Z80GetContext(&result.regs);
    result.tstates = z80TstateCounter();
    result.lastop = ActiveZ80GetLastOp();
    result.reads = reads;
    return result;
}

static Z80StableStatusPoll cfg;
static Z80_Regs seed;
static unsigned reduced, checked;
static void compare(int budget, bool must_not_batch = false) {
    UINT8 before[65536], after[65536];
    memcpy(before, memory, sizeof(memory));
    Result a = run(seed, budget, NULL);
    memcpy(after, memory, sizeof(memory));
    memcpy(memory, before, sizeof(memory));
    Result b = run(seed, budget, &cfg);
    assert(!memcmp(&a.regs, &b.regs, sizeof(a.regs)));
    assert(!memcmp(after, memory, sizeof(memory)));
    assert(a.cycles == b.cycles && a.tstates == b.tstates && a.lastop == b.lastop);
    if (must_not_batch && a.reads != b.reads) {
        fprintf(stderr, "unexpected batch case=%u pc=%x status=%x budget=%d reads=%d/%d\n",
                checked, seed.pc.d, status, budget, a.reads, b.reads);
        assert(a.reads == b.reads);
    }
    reduced += a.reads > b.reads;
    checked++;
    memcpy(memory, before, sizeof(memory));
}

static void program(unsigned pc) {
    memset(memory, 0, sizeof(memory));
    const UINT8 bytes[] = {0x3e, 0xff, 0xdb, 0x80, 0x1f, 0xd2,
                          (UINT8)pc, (UINT8)(pc >> 8)};
    memcpy(memory + pc, bytes, sizeof(bytes));
    for (int i = 0; i < 256; i++) ops[i] = args[i] = memory + i * 256;
    Z80Reset(); Z80GetContext(&seed);
    seed.pc.d = pc; seed.sp.d = 0xfff0; seed.af.b.h = 0xff;
    seed.irq_callback = irq_ack;
    cfg.op_map = ops; cfg.arg_map = args;
    cfg.first = 0; cfg.last = 0x1fff; cfg.port = 0xff80;
    busy_until = stop_on_read = irq_on_read = 0;
    status = 0;
}

int main() {
    Z80Init();
    Z80SetProgramReadHandler(read_memory); Z80SetProgramWriteHandler(write_memory);
    Z80SetCPUOpReadHandler(read_op); Z80SetCPUOpArgReadHandler(read_arg);
    Z80SetIOReadHandler(read_port); Z80SetIOWriteHandler(write_port);
    // Every flags byte, refresh wrap, cycle remainder and both stable values.
    for (int flags = 0; flags < 256; flags++) for (int budget = 1; budget < 160; budget++) {
        program(0xcb); seed.af.b.l = flags; seed.r = (flags + budget) & 255;
        status = (budget & 1) * 2;
        compare(budget);
    }
    // Entry in the middle of the loop, including the actual IN and JP.
    for (int start = 0; start < 4; start++) for (int budget = 1; budget < 512; budget++) {
        program(0xcb); const int offsets[] = {0, 2, 4, 5};
        seed.pc.d += offsets[start]; seed.af.b.l = budget & 255;
        compare(budget);
    }
    // Busy can clear inside a segment: only the subsequent stable read may batch.
    for (int deadline = 1; deadline < 256; deadline++) {
        program(0xcb); busy_until = deadline; compare(512);
    }
    // An external timer changes status between Execute calls; the next real IN
    // must observe it, leave the loop, and preserve a partial instruction tail.
    for (int cut = 1; cut < 128; cut++) {
        program(0xcb);
        Result a = run(seed, cut, NULL), b = run(seed, cut, &cfg);
        assert(!memcmp(&a.regs, &b.regs, sizeof(a.regs)));
        seed = b.regs; status = 1; compare(128, true);
    }
    for (int value = 0; value < 256; value++) if (value != 0 && value != 2) {
        program(0xcb); status = value; compare(512, true);
    }
    program(0xcb); cfg.first = 0xcc; compare(512, true);
    program(0xcb); cfg.last = 0xd1; compare(512, true);
    program(0xcb); cfg.port ^= 0x100; compare(512, true);
    program(0xcb); cfg.op_map = NULL; compare(512, true);
    program(0xcb); memcpy(separate, memory, 256); args[0] = separate; compare(512, true);
    program(0xf9); compare(512, true); // instruction pattern crosses a map page
    program(0xcb); stop_on_read = 1; compare(512, true);
    program(0xcb); irq_on_read = 1; compare(512, true);
    program(0xcb); seed.irq_state = Z80_ASSERT_LINE; compare(512, true);
    // Already-pending IRQ and NMI are delivered by the normal interpreter.
    program(0xcb); seed.irq_state = Z80_ASSERT_LINE; seed.iff1 = 1; compare(512, true);
    // NMI clears its pending flag on entry; after vector code reaches the
    // loop again, batching may resume. Registers/cycles still must match.
    program(0xcb); seed.nmi_pending = 1; compare(512);
    program(0xcb); seed.after_ei = 1; seed.irq_state = Z80_ASSERT_LINE; compare(512, true);
    program(0xcb); seed.after_retn = 1; seed.iff2 = 1; compare(512);
    // Live ROM patches must invalidate the pattern, without a cached PC match.
    for (int byte = 0; byte < 8; byte++) {
        program(0xcb); memory[0xcb + byte] ^= 1; compare(512, true);
    }
    program(0xcb); memory[0xca] = 0xdd; seed.pc.d = 0xca; compare(512);
    z80_set_cycle_tables_msx();
    program(0xcb); compare(512, true); // different instruction timing disables batching
    assert(reduced > 10000);
    Z80SetStableStatusPoll(NULL); Z80Exit();
    printf("PASS Z80: %u real-core differential cases, %u with fewer port reads\n", checked, reduced);
}
