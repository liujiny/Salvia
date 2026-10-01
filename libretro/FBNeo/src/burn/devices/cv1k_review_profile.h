// Main-thread, pause-flushed sampling. No allocation, file I/O or extra fences.
#ifndef SALVIA_CV1K_REVIEW_PROFILE_H
#define SALVIA_CV1K_REVIEW_PROFILE_H
#include <string.h>
#define SALVIA_CV1K_REVIEW_BUILD "cv1k-dmul-native-20261001-r1"
// Use the existing PRNG result. Workload-count frames and timing frames are disjoint.
static inline bool salvia_review_work_sample(unsigned randomWord, bool timingSample) {
    return !timingSample && (randomWord & 255u) == 1u;
}
#if defined(_MSC_VER)
typedef unsigned __int64 SalviaReviewTick;
#else
typedef unsigned long long SalviaReviewTick;
#endif

template<unsigned N> struct SalviaReviewProfile {
    unsigned rng, frames, samples, invalid;
    SalviaReviewTick ticks[N], total, peak;

    // Zero-initialized static storage is valid; preserve the random stream at pause.
    void clear_window() {
        unsigned seed = rng ? rng : 0x6d2b79f5u;
        memset(this, 0, sizeof(*this));
        rng = seed;
    }
    bool begin() {
        ++frames;
        unsigned x = rng ? rng : 0x6d2b79f5u;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        rng = x;
        return !(x & 63u);
    }
    bool record(const SalviaReviewTick (&stamp)[N + 1]) {
        if (!stamp[0]) { ++invalid; return false; }
        for (unsigned i = 0; i < N; ++i)
            if (stamp[i + 1] < stamp[i]) { ++invalid; return false; }
        for (unsigned i = 0; i < N; ++i) ticks[i] += stamp[i + 1] - stamp[i];
        SalviaReviewTick elapsed = stamp[N] - stamp[0];
        total += elapsed;
        if (elapsed > peak) peak = elapsed;
        ++samples;
        return true;
    }
};

#ifdef _XBOX
#include <xtl.h>
// Defined by the statically linked frontend; accessed only on the emulation thread.
extern "C" { extern unsigned salvia_cv1k_work_sample_frame; }
static inline SalviaReviewTick salvia_review_clock() {
    LARGE_INTEGER now;
    if (!QueryPerformanceCounter(&now) || now.QuadPart <= 0) return 0;
    return (SalviaReviewTick)now.QuadPart;
}
#endif
#endif
