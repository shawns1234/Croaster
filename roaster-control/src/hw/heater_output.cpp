#include "heater_output.h"

#include <Arduino.h>

void HeaterOutput::begin() {
    digitalWrite(pin_, activeHigh_ ? LOW : HIGH); // off before the pin becomes an output
    pinMode(pin_, OUTPUT);
}

void HeaterOutput::set(float percent) {
    if (!(percent > 0))
        percent = 0;
    if (percent > 100)
        percent = 100;
    req_ = percent;
    tp_.setDuty(percent * maxPct_ / 100.0f);
}

void HeaterOutput::tick(uint32_t nowMs) {
    const bool on = tp_.isOn(nowMs);
    if (on != level_) {
        level_ = on;
        digitalWrite(pin_, on == activeHigh_ ? HIGH : LOW);
    }
}
