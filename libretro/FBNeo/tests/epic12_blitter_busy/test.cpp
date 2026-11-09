#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

// Mock types and variables
typedef int INT32;
typedef unsigned int UINT32;
typedef unsigned short UINT16;
typedef unsigned char UINT8;

static INT32 s_sh3_burned_cycles = 0;
static INT32 s_sh3_remaining_cycles = 9000;
static INT32 s_sh4_timer_running = 0;
static INT32 s_sh4_timer_cycles = 0;

static void sh4_set_cave_blitter_delay_timer(int cycles) {
    s_sh4_timer_running = 1;
    s_sh4_timer_cycles = cycles;
}

static void sh4_clear_cave_blitter_delay_timer() {
    s_sh4_timer_running = 0;
    s_sh4_timer_cycles = 0;
}

static void Sh3BurnCycles(INT32 cycles) {
    s_sh3_burned_cycles += cycles;
    s_sh3_remaining_cycles -= cycles;
}

static INT32 Sh3GetRemainingCycles() {
    return s_sh3_remaining_cycles;
}

// Mock Thready
struct MockThready {
    bool pending;
    bool worker_complete;

    bool poll_complete() {
        if (!pending) return true;
        if (worker_complete) {
            pending = false;
            return true;
        }
        return false;
    }

    bool is_busy() {
        if (!pending) return false;
        if (poll_complete()) {
            pending = false;
            return false;
        }
        return true;
    }

    void notify_wait() {
        pending = false;
        worker_complete = true;
    }
};

static MockThready thready;
static int m_blitter_busy = 0;
static int sleep_on_busy = 1;
static int m_burn_cycles = 1024;

static UINT32 epic12_blitter_read(UINT32 offset) {
    switch (offset) {
        case 0x10:
            if (m_blitter_busy && !thready.is_busy()) {
                m_blitter_busy = 0;
                sh4_clear_cave_blitter_delay_timer();
            }

            if (m_blitter_busy) {
                if (sleep_on_busy) {
                    Sh3BurnCycles(m_burn_cycles);
                }
                return 0x00000000;
            } else {
                return 0x00000010;
            }
        default:
            return 0xffffffff;
    }
}

static void epic12_wait_blitterthread() {
    thready.notify_wait();
    m_blitter_busy = 0;
    sh4_clear_cave_blitter_delay_timer();
}

static void epic12_draw_screen() {
    thready.notify_wait();
    m_blitter_busy = 0;
    sh4_clear_cave_blitter_delay_timer();
}

static void gfx_exec_write_sim(INT32 cycles) {
    if (cycles > 0) {
        m_blitter_busy = 1;
        sh4_set_cave_blitter_delay_timer(cycles);
        Sh3BurnCycles(cycles);
    } else {
        m_blitter_busy = 0;
        sh4_clear_cave_blitter_delay_timer();
    }
    thready.pending = true;
    thready.worker_complete = false;
}

int main() {
    printf("Running epic12_blitter_busy unit test...\n");

    // Test 1: Initial state is idle
    assert(m_blitter_busy == 0);
    assert(epic12_blitter_read(0x10) == 0x00000010);
    printf("  PASS: Test 1 - Initial blitter read returns 0x10 (idle)\n");

    // Test 2: In-flight blit reports busy (0x00000000) and burns gentle quantum (m_burn_cycles)
    s_sh3_burned_cycles = 0;
    s_sh3_remaining_cycles = 9000;
    gfx_exec_write_sim(5000);
    assert(m_blitter_busy == 1);
    assert(s_sh4_timer_running == 1);
    
    // While worker is executing
    thready.worker_complete = false;
    UINT32 busy_read = epic12_blitter_read(0x10);
    assert(busy_read == 0x00000000);
    assert(s_sh3_burned_cycles == 5000 + m_burn_cycles); // 5000 from exec + 1024 from busy read
    // Guest slice budget was NOT wiped out by entire rem
    assert(s_sh3_remaining_cycles > 0);
    printf("  PASS: Test 2 - In-flight blitter read returns 0x00 (busy) with gentle m_burn_cycles burn\n");

    // Test 3: Worker thread finishes -> blitter_read automatically recovers idle and clears timer
    thready.worker_complete = true;
    UINT32 idle_read = epic12_blitter_read(0x10);
    assert(idle_read == 0x00000010);
    assert(m_blitter_busy == 0);
    assert(s_sh4_timer_running == 0);
    printf("  PASS: Test 3 - Asynchronous worker completion immediately unlocks 0x10 idle and stops delay timer\n");

    // Test 4: Zero-cycle blit does not engage blitter_busy or delay timer
    gfx_exec_write_sim(0);
    assert(m_blitter_busy == 0);
    assert(s_sh4_timer_running == 0);
    printf("  PASS: Test 4 - Zero-cycle blit maintains idle state\n");

    // Test 5: Frame boundary / wait_blitterthread clears any lingering busy state before VBlank
    s_sh3_remaining_cycles = 100000;
    gfx_exec_write_sim(50000); // Heavy blit
    assert(m_blitter_busy == 1);
    assert(s_sh4_timer_running == 1);
    
    // DrvFrame end calls epic12_wait_blitterthread() and epic12_draw_screen() before VBlank
    epic12_wait_blitterthread();
    assert(m_blitter_busy == 0);
    assert(s_sh4_timer_running == 0);
    assert(thready.pending == false);

    // Test 6: epic12_draw_screen also enforces blitter idle and clears timer
    gfx_exec_write_sim(20000);
    assert(m_blitter_busy == 1);
    epic12_draw_screen();
    assert(m_blitter_busy == 0);
    assert(s_sh4_timer_running == 0);

    // Verify remaining cycles
    assert(Sh3GetRemainingCycles() > 0);

    // VBlank interrupt handler reads 0x10
    UINT32 vblank_read = epic12_blitter_read(0x10);
    assert(vblank_read == 0x00000010); // bit 4 must be 1 for ddpdfk 0x0c1d14e0 to succeed
    printf("  PASS: Test 5 - Frame sync clears blitter busy, guaranteeing VBlank draw setup succeeds\n");

    printf("PASS epic12_blitter_busy: all tests passed successfully.\n");
    return 0;
}
