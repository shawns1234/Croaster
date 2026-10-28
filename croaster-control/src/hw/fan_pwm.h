#pragma once
// DC fan (or DC motor driver / SSR-fed DC supply) on an LEDC PWM pin.
#include <cstdint>

#include "fan_output.h"

class FanPwm : public FanOutput {
public:
    FanPwm(int pin, uint32_t freqHz = 25000, uint8_t bits = 10)
        : pin_(pin)
        , freq_(freqHz)
        , bits_(bits) {}
    bool begin() override;
    void set(float percent) override;
    float get() const override {
        return pct_;
    }
    bool mainsDetected() const override {
        return true;
    } // cannot tell

private:
    int pin_;
    uint32_t freq_;
    uint8_t bits_;
    float pct_ = 0;
};
