#pragma once
// AC fan via RobotDyn-style dimmer (zero-cross detector + triac), phase-angle control.
// Not LEDC PWM: the triac must be fired once per mains half-cycle at a delay after
// the zero crossing. Implemented on a GPTimer (1 MHz) + GPIO interrupt, so there is
// no dependency on RBDdimmer (AVR-era) or rbdimmerESP32. One instance per firmware.
#include <cstdint>

#include "fan_output.h"

class FanDimmer : public FanOutput {
public:
    // gatePin must be < 32 (fast register path); mainsHz 50 or 60.
    FanDimmer(int zeroCrossPin, int gatePin, uint8_t mainsHz)
        : zc_(zeroCrossPin)
        , gate_(gatePin)
        , hz_(mainsHz) {}
    bool begin() override;
    void set(float percent) override;
    float get() const override {
        return pct_;
    }
    bool mainsDetected() const override;
    uint32_t zeroCrossCount() const;

private:
    int zc_, gate_;
    uint8_t hz_;
    float pct_ = 0;
    bool ok_ = false;
};
