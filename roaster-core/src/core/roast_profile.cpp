#include "roast_profile.h"

#include <cstring>

#include "roaster_types.h"

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
} // namespace

bool RoastProfile::load(const ProfileStep* steps, size_t n) {
    if (n == 0 || n > kMaxSteps)
        return false;
    for (size_t i = 0; i < n; i++) {
        if (!(steps[i].durationS > 0))
            return false;
        steps_[i] = steps[i];
    }
    n_ = n;
    return true;
}

bool RoastProfile::loadJson(JsonArrayConst arr, char unit) {
    if (arr.isNull())
        return false;
    ProfileStep tmp[kMaxSteps];
    size_t n = 0;
    for (JsonObjectConst s : arr) {
        if (n >= kMaxSteps)
            return false;
        tmp[n].durationS = s["duration"] | 0.0f;
        tmp[n].setpointC = toCelsius(s["setpoint"] | 0.0f, unit);
        tmp[n].interp = parseInterp(s["interpolation"] | "linear");
        tmp[n].fanPct = s["fanValue"].is<float>() ? s["fanValue"].as<float>() : NAN;
        n++;
    }
    return load(tmp, n);
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
