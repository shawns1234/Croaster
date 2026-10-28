#pragma once
// Grace period before a `nomains` trip. FanDimmer::mainsDetected() looks at
// the last 100 ms only, so a short dropout or a few missed zero-cross pulses
// would trip it at once. update() reports a trip only once the zero-cross has
// been missing for graceMs without a break while the heater is on. The
// zero-cross coming back, or the heater turning off, starts the count again.
#include <cstdint>

class MainsWatch {
public:
    bool update(bool heating, bool mainsOk, uint32_t nowMs, uint32_t graceMs) {
        if (!heating || mainsOk) {
            absent_ = false;
            return false;
        }
        if (!absent_) {
            absent_ = true;
            absentSinceMs_ = nowMs;
        }
        return nowMs - absentSinceMs_ >= graceMs; // unsigned: correct across millis() wrap
    }

private:
    bool absent_ = false; // a flag, so a dropout starting at millis() == 0 still counts
    uint32_t absentSinceMs_ = 0;
};
