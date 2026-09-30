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
    printf("PASS review profile: %u/1000000 samples; phase sums, clock guards, pause windows and 3/4-stage layouts\n", selected);
    return 0;
}
