#include "roast_profile.h"

#include <cstring>

#include "control_types.h"

namespace {
float ease(float a, float b, float p, Interp k) {
    switch (k) {
    case Interp::EaseIn:
        return a + (b - a) * p * p;
    case Interp::EaseOut:
        return a + (b - a) * (1 - (1 - p) * (1 - p));
    case Interp::EaseInOut: {
        const float q = p < 0.5f ? 2 * p * p : 1 - std::pow(-2 * p + 2, 2.0f) / 2;
        return a + (b - a) * q;
    }
    case Interp::Linear:
    default:
        return a + (b - a) * p;
    }
}
Interp parseInterp(const char* s) {
    if (!s)
        return Interp::Linear;
    if (!strcmp(s, "ease-in"))
        return Interp::EaseIn;
    if (!strcmp(s, "ease-out"))
        return Interp::EaseOut;
    if (!strcmp(s, "ease-in-out"))
        return Interp::EaseInOut;
    return Interp::Linear;
}

bool validStep(const ProfileStep& s) {
    if (!(s.durationS > 0 && s.durationS <= RoastProfile::kMaxStepS))
        return false; // also rejects NaN
    if (!(s.setpointC >= RoastProfile::kMinSetpointC && s.setpointC <= RoastProfile::kMaxSetpointC))
        return false;
    return std::isnan(s.fanPct) || (s.fanPct >= 0 && s.fanPct <= 100);
}

// One JSON step; false if it is not a valid step.
bool parseStep(JsonVariantConst v, char unit, ProfileStep& out) {
    if (!v.is<JsonObjectConst>())
        return false;
    JsonObjectConst s = v.as<JsonObjectConst>();
    if (!s["duration"].is<float>() || !s["setpoint"].is<float>())
        return false;
    JsonVariantConst fan = s["fanValue"];
    if (!fan.isNull() && !fan.is<float>())
        return false;
    out.durationS = s["duration"].as<float>();
    out.setpointC = toCelsius(s["setpoint"].as<float>(), unit);
    out.interp = parseInterp(s["interpolation"].is<const char*>() ? s["interpolation"].as<const char*>() : nullptr);
    out.fanPct = fan.isNull() ? NAN : fan.as<float>();
    return validStep(out);
}
} // namespace

bool RoastProfile::load(const ProfileStep* steps, size_t n) {
    if (n == 0 || n > kMaxSteps)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!validStep(steps[i]))
            return false;
    for (size_t i = 0; i < n; i++)
        steps_[i] = steps[i];
    n_ = n;
    return true;
}

bool RoastProfile::loadJson(JsonArrayConst arr, char unit) {
    // Two passes instead of a ProfileStep[kMaxSteps] scratch copy (512 B of stack):
    // check every step, then parse them straight into steps_.
    if (arr.isNull() || arr.size() == 0 || arr.size() > kMaxSteps)
        return false;
    ProfileStep one;
    for (JsonVariantConst v : arr)
        if (!parseStep(v, unit, one))
            return false;
    size_t n = 0;
    for (JsonVariantConst v : arr)
        parseStep(v, unit, steps_[n++]);
    n_ = n;
    return true;
}

float RoastProfile::totalDurationS() const {
    float t = 0;
    for (size_t i = 0; i < n_; i++)
        t += steps_[i].durationS;
    return t;
}

ProfileTarget RoastProfile::at(float elapsed) const {
    ProfileTarget out;
    if (n_ == 0) {
        out.finished = true;
        return out;
    }
    float acc = 0;
    for (size_t i = 0; i < n_; i++) {
        const float start = acc;
        acc += steps_[i].durationS;
        if (elapsed <= acc) {
            const float p = elapsed <= start ? 0 : (elapsed - start) / steps_[i].durationS;
            // same as legacy JS: step 0 holds its own setpoint, later steps ramp from the previous one
            const float from = i == 0 ? steps_[0].setpointC : steps_[i - 1].setpointC;
            out.setpointC = std::floor(ease(from, steps_[i].setpointC, p, steps_[i].interp) * 10) / 10;
            out.fanPct = steps_[i].fanPct;
            return out;
        }
    }
    out.setpointC = steps_[n_ - 1].setpointC;
    out.fanPct = steps_[n_ - 1].fanPct;
    out.finished = true;
    return out;
}
