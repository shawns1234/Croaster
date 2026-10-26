#pragma once
// Fixed-window moving average over the last N samples.
// Replaces sebnil/Moving-Avarage-Filter (legacy sensor.h). Differences on purpose:
//  - seeded with the first sample (legacy started from zeros, so the PID input
//    ramped up from 0 for the first N samples after boot),
//  - non-finite samples (NaN from a thermocouple fault) are ignored.
#include <cmath>
#include <cstddef>

template <size_t N>
class MovingAverage {
    static_assert(N > 0, "window must be > 0");

public:
    // Adds a sample and returns the current average (NaN until the first valid sample).
    float push(float x) {
        if (!std::isfinite(x))
            return value();
        if (!seeded_) {
            for (auto& v : buf_)
                v = x;
            seeded_ = true;
        } else {
            buf_[idx_] = x;
            idx_ = (idx_ + 1) % N;
        }
        return value();
    }

    float value() const {
        if (!seeded_)
            return NAN;
        float s = 0;
        for (float v : buf_)
            s += v;
        return s / N;
    }

    void reset() {
        seeded_ = false;
        idx_ = 0;
    }

private:
    float buf_[N] = {};
    size_t idx_ = 0;
    bool seeded_ = false;
};
