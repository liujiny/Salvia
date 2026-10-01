#include "../../src/burn/devices/cv1k_review_profile.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    SalviaReviewProfile<3> p = {0};
    unsigned selected = 0;
    for (unsigned i = 0; i < 1000000; ++i) if (p.begin()) ++selected;
    CHECK(p.frames == 1000000 && selected > 14000 && selected < 17200);
    SalviaReviewTick good[4] = {100, 103, 110, 114};
    CHECK(p.record(good) && p.record(good));
    CHECK(p.samples == 2 && p.ticks[0] == 6 && p.ticks[1] == 14 && p.ticks[2] == 8);
    CHECK(p.total == 28 && p.peak == 14);
    SalviaReviewTick bad[4] = {100, 99, 110, 114};
    CHECK(!p.record(bad));
    bad[0] = 0;
    CHECK(!p.record(bad) && p.invalid == 2 && p.samples == 2 && p.total == 28);
    unsigned seed = p.rng;
    p.clear_window();
    CHECK(p.rng == seed && p.frames == 0 && p.samples == 0 && p.invalid == 0 && p.total == 0);
    CHECK(p.ticks[0] == 0 && p.ticks[1] == 0 && p.ticks[2] == 0);
    SalviaReviewProfile<4> core = {0};
    SalviaReviewTick phases[5] = {50, 51, 80, 84, 90};
    CHECK(core.record(phases) && core.total == 40 && core.ticks[1] == 29);
    SalviaReviewTail<4> tail = {0};
    // 1000 ticks/second: exact 25 ms is excluded, a 26 ms sample is included.
    SalviaReviewTick boundary[5] = {100, 101, 121, 122, 125};
    SalviaReviewTick cpuSlow[5] = {100, 101, 122, 123, 126};
    SalviaReviewTick drawSlow[5] = {100, 101, 103, 104, 148};
    tail.record_validated(boundary, 1, 25);
    CHECK(tail.slowSamples == 0 && tail.peak == 25 && tail.peakFrame == 1);
    tail.record_validated(cpuSlow, 2, 25);
    tail.record_validated(drawSlow, 3, 25);
    CHECK(tail.slowSamples == 2 && tail.slowTotal == 74);
    CHECK(tail.slowTicks[0] == 2 && tail.slowTicks[1] == 23 && tail.slowTicks[2] == 2 && tail.slowTicks[3] == 47);
    CHECK(tail.dominant[1] == 1 && tail.dominant[3] == 1);
    CHECK(tail.peakFrame == 3 && tail.peak == 48 && tail.peakTicks[1] == 2 && tail.peakTicks[3] == 44);
    // A CPU maximum in a shorter frame must not overwrite the worst frame.
    SalviaReviewTick shorter[5] = {200, 201, 230, 231, 232};
    tail.record_validated(shorter, 4, 25);
    CHECK(tail.peakFrame == 3 && tail.peakTicks[1] == 2 && tail.slowSamples == 3);
    SalviaReviewTick badCore[5] = {100, 101, 99, 102, 103};
    if (core.record(badCore)) tail.record_validated(badCore, 5, 25);
    CHECK(tail.slowSamples == 3 && tail.peakFrame == 3);
    tail.clear_window();
    CHECK(tail.slowSamples == 0 && tail.peakFrame == 0 && tail.slowTotal == 0 && tail.peakTicks[3] == 0);
    tail.record_validated(drawSlow, 6, 0);
    CHECK(tail.peak == 0 && tail.slowSamples == 0);
    // Large tick origins and totals must remain exact in 64 bits.
    SalviaReviewTick base = (SalviaReviewTick)1 << 40;
    SalviaReviewTick wide[5] = {base, base+1, base+2, base+3, base+((SalviaReviewTick)1<<33)};
    tail.record_validated(wide, 7, 25);
    CHECK(tail.slowTotal == ((SalviaReviewTick)1<<33) && tail.peak == tail.slowTotal);
    puts("PASS slow-frame threshold, mixed phase dominance, coherent peak frame, invalid exclusion, reset and 64-bit ticks");
    printf("PASS review profile: %u/1000000 samples; phase sums, clock guards, pause windows and 3/4-stage layouts\n", selected);
    return 0;
}
