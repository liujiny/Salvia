#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Pitch-preserving slowdown for the Xbox FBNeo audio consumer. No allocation,
// locks, OS calls or floating point in the sample loop. The source is a stereo
// ring exposing available(), copy(offset, dst, frames), consume(frames).
// At tempo 1.0 overlapping samples are identical: playback is bit exact.
class AudioTempo {
public:
    enum { RATE = 48000, HOP = 768, OVERLAP = 384, SEARCH = 192,
           SEQUENCE = HOP + OVERLAP, WINDOW = SEQUENCE + SEARCH * 2 + 1,
           TARGET = 3072, ONE = 65536, MIN_TEMPO = ONE * 28 / 100 };
    struct Stats {
        unsigned callbacks, stretchedFrames, generatedFrames, shortages;
        unsigned minTempo, tempo;
    };

private:
    int16_t previous[OVERLAP * 2];
    int16_t window[WINDOW * 2];
    int16_t output[HOP * 2];
    size_t outputPos, outputCount;
    uint32_t nominalFx, tempoFx;
    bool primed, overlapReady;
    long previousAvailable;
    unsigned previousConsumed;
    unsigned arrivals[8], requests[8], observation, previousRequested;
    bool observed;
    Stats stats;

    unsigned errorAt(unsigned offset) const {
        unsigned error = 0;
        // Both channels contribute to alignment; antiphase stereo must not
        // collapse into silence in a mono correlation signal. Shift first to
        // keep the sum of squared differences within a 32-bit integer.
        for (unsigned i = 0; i < OVERLAP; i += 4) {
            int dl = (previous[i * 2] >> 6) - (window[(offset + i) * 2] >> 6);
            int dr = (previous[i * 2 + 1] >> 6) - (window[(offset + i) * 2 + 1] >> 6);
            error += (unsigned)(dl * dl + dr * dr);
        }
        return error;
    }

    void observe(size_t available, unsigned requested) {
        long arrived = (long)available - previousAvailable + previousConsumed;
        if (!observed) arrived = (long)available;
        if (arrived < 0) arrived = 0;
        if (arrived > 16384) arrived = 16384;
        arrivals[observation & 7] = (unsigned)arrived;
        requests[observation & 7] = observed ? previousRequested : 0;
        previousRequested = requested;
        observation++;
        previousAvailable = (long)available;
        previousConsumed = 0;
        observed = true;

        unsigned supplied = 0, demanded = 0;
        for (unsigned i = 0; i < 8; i++) {
            supplied += arrivals[i];
            demanded += requests[i];
        }
        int rate = ONE;
        if (observation >= 4 && demanded)
            rate = (int)(((supplied * 1024) / demanded) * 64);
        if (rate > ONE * 94 / 100) rate = ONE;
        if (rate < MIN_TEMPO) rate = MIN_TEMPO;

        // Source occupancy feedback also reacts before the supply
        // estimate catches a sudden slowdown. Target: 64 ms of source
        // audio, including the WSOLA lookahead (not an extra hidden FIFO).
        int fillError = (int)available - TARGET;
        // One synthesis hop of hysteresis avoids stretching healthy audio
        // merely because 800-frame core packets meet 1024-frame callbacks.
        if (fillError > HOP) fillError -= HOP;
        else if (fillError < -HOP) fillError += HOP;
        else fillError = 0;
        int correction = fillError * (ONE * 60 / 100) / TARGET;
        int wanted = rate + correction;
        if (wanted > ONE) wanted = ONE;
        if (wanted < MIN_TEMPO) wanted = MIN_TEMPO;
        if (!primed) tempoFx = (uint32_t)wanted;
        else {
            const int maxChange = ONE * 8 / 100;
            int change = wanted - (int)tempoFx;
            if (change > maxChange) change = maxChange;
            if (change < -maxChange) change = -maxChange;
            tempoFx = (uint32_t)((int)tempoFx + change);
        }
        // Avoid tiny alterations when the measured supply is healthy.
        if (tempoFx > ONE * 995 / 1000) tempoFx = ONE;
        stats.tempo = tempoFx;
    }

    template<class Source> bool generate(Source& source) {
        unsigned expected = nominalFx >> 16;
        unsigned needed = expected + (overlapReady ? SEARCH : 0) + SEQUENCE;
        if (source.available() < needed) return false;
        source.copy(0, window, needed);
        unsigned chosen = expected;
        if (overlapReady && tempoFx < ONE) {
            unsigned best = errorAt(expected);
            // Prefer the nominal position on ties; periodic music must not
            // accumulate timing drift. Search offsets never move nominalFx.
            for (unsigned distance = 4; distance <= SEARCH; distance += 4) {
                unsigned candidate = expected + distance;
                unsigned error = errorAt(candidate);
                if (error < best) { best = error; chosen = candidate; }
                if (expected >= distance) {
                    candidate = expected - distance;
                    error = errorAt(candidate);
                    if (error < best) { best = error; chosen = candidate; }
                }
            }
            unsigned coarse = chosen;
            for (int delta = -3; delta <= 3; delta++) {
                int candidate = (int)coarse + delta;
                if (candidate < 0 || candidate < (int)expected - SEARCH ||
                    candidate > (int)expected + SEARCH) continue;
                unsigned error = errorAt((unsigned)candidate);
                if (error < best) { best = error; chosen = (unsigned)candidate; }
            }
        }
        const int16_t* segment = window + chosen * 2;
        if (overlapReady) {
            for (unsigned i = 0; i < OVERLAP; i++) {
                int weight = (int)(i * 32768 / OVERLAP);
                for (unsigned channel = 0; channel < 2; channel++) {
                    int a = previous[i * 2 + channel];
                    int b = segment[i * 2 + channel];
                    output[i * 2 + channel] = (int16_t)(a + (((b - a) * weight) >> 15));
                }
            }
            memcpy(output + OVERLAP * 2, segment + OVERLAP * 2,
                   (HOP - OVERLAP) * 2 * sizeof(int16_t));
        } else {
            memcpy(output, segment, HOP * 2 * sizeof(int16_t));
        }
        memcpy(previous, segment + HOP * 2, sizeof(previous));
        overlapReady = true;
        nominalFx += tempoFx * HOP;
        unsigned consumed = nominalFx >> 16;
        // Keep SEARCH source frames behind the next nominal start. All
        // future reads remain owned by the ring until this consume call.
        consumed = consumed > SEARCH ? consumed - SEARCH : 0;
        nominalFx -= consumed << 16;
        source.consume(consumed);
        previousConsumed += consumed;
        outputPos = 0;
        outputCount = HOP;
        stats.generatedFrames += HOP;
        if (tempoFx < ONE) stats.stretchedFrames += HOP;
        if (tempoFx < stats.minTempo) stats.minTempo = tempoFx;
        return true;
    }

public:
    AudioTempo() { clear(); }

    void resetStream() {
        outputPos = outputCount = 0;
        nominalFx = 0;
        tempoFx = ONE;
        primed = overlapReady = observed = false;
        previousAvailable = 0;
        previousConsumed = observation = previousRequested = 0;
        memset(arrivals, 0, sizeof(arrivals));
        memset(requests, 0, sizeof(requests));
        stats.tempo = ONE;
    }

    void clear() {
        memset(&stats, 0, sizeof(stats));
        stats.minTempo = ONE;
        resetStream();
    }

    template<class Source> size_t read(Source& source, int16_t* dst, size_t frames) {
        stats.callbacks++;
        observe(source.available(), (unsigned)frames);
        if (!primed) {
            if (source.available() < TARGET) return 0;
            primed = true;
        }
        size_t done = 0;
        while (done < frames) {
            if (outputPos == outputCount && !generate(source)) {
                stats.shortages++;
                // Refill once after a long stall instead of alternating tiny
                // bursts of sound and silence on each incoming game frame.
                primed = overlapReady = false;
                nominalFx = 0;
                break;
            }
            size_t n = outputCount - outputPos;
            if (n > frames - done) n = frames - done;
            memcpy(dst + done * 2, output + outputPos * 2, n * 2 * sizeof(int16_t));
            outputPos += n;
            done += n;
        }
        return done;
    }

    // Read from the SDL callback, or with SDL_LockAudio held by the caller.
    Stats getStats() const { return stats; }
};
