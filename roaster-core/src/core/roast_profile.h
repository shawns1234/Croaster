#pragma once
// Setpoint (and optional fan) schedule. Port of followProfile() from the deleted
// miniweb/src/profiling.ts (git show 6f1f631^:miniweb/src/profiling.ts), moved
// into firmware so a profile keeps running if the browser tab closes.
// JSON shape: [{"duration":s,"setpoint":T,"interpolation":"linear|ease-in|ease-out|ease-in-out","fanValue":%}]
#include <ArduinoJson.h>
#include <cmath>
#include <cstddef>

enum class Interp { Linear, EaseIn, EaseOut, EaseInOut };

struct ProfileStep {
    float durationS = 0;
    float setpointC = 0;
    Interp interp = Interp::Linear;
    float fanPct = NAN; // NaN = leave fan alone
};

struct ProfileTarget {
    float setpointC = NAN;
    float fanPct = NAN;
    bool finished = false;
};

class RoastProfile {
public:
    static constexpr size_t kMaxSteps = 32;

    bool load(const ProfileStep* steps, size_t n);
    // Parses an array of steps; setpoints are in `unit` ('C', 'F', 'K'). False on error.
    bool loadJson(JsonArrayConst steps, char unit = 'C');
    ProfileTarget at(float elapsedS) const;
    size_t size() const {
        return n_;
    }
    float totalDurationS() const;

private:
    ProfileStep steps_[kMaxSteps];
    size_t n_ = 0;
};
