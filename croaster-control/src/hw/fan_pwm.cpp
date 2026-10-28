#include "fan_pwm.h"

#include <Arduino.h>

bool FanPwm::begin() {
    return ledcAttach(pin_, freq_, bits_);
}

void FanPwm::set(float pct) {
    if (!(pct > 0))
        pct = 0;
    if (pct > 100)
        pct = 100;
    pct_ = pct;
    ledcWrite(pin_, static_cast<uint32_t>(((1u << bits_) - 1) * pct / 100.0f + 0.5f));
}
