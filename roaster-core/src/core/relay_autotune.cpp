#include "relay_autotune.h"

#include <cmath>

namespace {
constexpr float kPi = 3.14159265f;
}

void RelayAutotune::start(const Config& c) {
    *this = RelayAutotune();
    cfg_ = c;
    state_ = State::Running;
    high_ = true;
    out_ = cfg_.outputHigh;
}

float RelayAutotune::update(float pv) {
    if (state_ != State::Running)
        return cfg_.outputLow;
    t_ += cfg_.dtSeconds;
    if (!std::isfinite(pv)) {
        fail("sensor fault");
        return out_;
    }
    if (pv > cfg_.abortAboveC) {
        fail("over temperature");
        return out_;
    }
    if (t_ > cfg_.timeoutSeconds) {
        fail("timeout");
        return out_;
    }

    if (pv > pvMax_)
        pvMax_ = pv;
    if (pv < pvMin_)
        pvMin_ = pv;

    const bool switchLow = high_ && pv > cfg_.setpoint + cfg_.hysteresis;
    const bool switchHigh = !high_ && pv < cfg_.setpoint - cfg_.hysteresis;
    if (switchLow || switchHigh) {
        high_ = !high_;
        out_ = high_ ? cfg_.outputHigh : cfg_.outputLow;
        halfCycles_++;
        // A full period is high->low to high->low. Skip the initial heat-up and first cycle.
        if (switchLow) {
            if (lastSwitchT_ >= 0 && halfCycles_ >= 5) {
                const float a = (pvMax_ - pvMin_) / 2.0f;
                sumAmp_ += a;
                sumPeriod_ += t_ - lastSwitchT_;
                measured_++;
            }
            lastSwitchT_ = t_;
            pvMax_ = -1e9f;
            pvMin_ = 1e9f;
        }
        if (measured_ >= cfg_.cycles) {
            const float a = sumAmp_ / measured_;
            const float h = cfg_.hysteresis;
            const float d = (cfg_.outputHigh - cfg_.outputLow) / 2.0f;
            if (a <= h * 1.05f) {
                fail("oscillation too small vs hysteresis");
                return out_;
            }
            ku_ = 4.0f * d / (kPi * std::sqrt(a * a - h * h));
            tu_ = sumPeriod_ / measured_;
            gains_ = gainsFor(cfg_.rule, ku_, tu_);
            state_ = State::Done;
            out_ = cfg_.outputLow;
        }
    }
    return out_;
}

PidGains RelayAutotune::gainsFor(Rule r, float ku, float tu) {
    float kp, ti, td;
    switch (r) {
    case Rule::ZieglerNichols:
        kp = 0.6f * ku;
        ti = tu / 2.0f;
        td = tu / 8.0f;
        break;
    case Rule::ZieglerNicholsNoOvershoot:
        kp = 0.2f * ku;
        ti = tu / 2.0f;
        td = tu / 3.0f;
        break;
    case Rule::TyreusLuyben:
    default:
        kp = ku / 2.2f;
        ti = 2.2f * tu;
        td = tu / 6.3f;
        break;
    }
    PidGains g;
    g.kp = kp;
    g.ki = kp / ti;
    g.kd = kp * td;
    return g;
}
