#pragma once
// Astrom-Hagglund relay-feedback autotune.
// The relay switches on the *measured* temperature crossing setpoint +/- hysteresis
// (the vendored AutoTunePID toggled on a fixed 2 s timer and assumed the process
// amplitude equals the relay amplitude, so it always returned Ku=4/pi, Tu=1.8 s).
// Here Ku = 4d / (pi * sqrt(a^2 - h^2)) and Tu are measured from real cycles.
#include "pid.h"

class RelayAutotune {
public:
    enum class Rule { TyreusLuyben, ZieglerNichols, ZieglerNicholsNoOvershoot };
    enum class State { Idle, Running, Done, Failed };

    struct Config {
        float setpoint = 180;
        float outputHigh = 100, outputLow = 0;
        float hysteresis = 1.0f; // C, > sensor noise
        int cycles = 4;          // measured cycles (after one warm-up cycle)
        float dtSeconds = 0.25f; // must match update() rate
        float timeoutSeconds = 1800;
        float abortAboveC = 260;
        Rule rule = Rule::TyreusLuyben;
    };

    void start(const Config& c);
    // Feed one sample per dtSeconds; returns the heater output to apply.
    float update(float pv);
    void abort() {
        if (state_ == State::Running)
            fail("aborted");
    }

    State state() const {
        return state_;
    }
    PidGains result() const {
        return gains_;
    }
    float ku() const {
        return ku_;
    }
    float tuSeconds() const {
        return tu_;
    }
    const char* failure() const {
        return failure_;
    }

    static PidGains gainsFor(Rule r, float ku, float tu);

private:
    void fail(const char* why) {
        state_ = State::Failed;
        failure_ = why;
        out_ = cfg_.outputLow;
    }
    Config cfg_;
    State state_ = State::Idle;
    bool high_ = true;
    float out_ = 0, t_ = 0, lastSwitchT_ = -1;
    float pvMax_ = -1e9f, pvMin_ = 1e9f;
    int halfCycles_ = 0, measured_ = 0;
    float sumAmp_ = 0, sumPeriod_ = 0;
    float ku_ = 0, tu_ = 0;
    PidGains gains_;
    const char* failure_ = "";
};
