#pragma once
// Feeds Croaster's display/RoR/Artisan path from the control task's readings.
// In a roaster build the control task owns the thermocouple SPI bus; CroasterCore
// is built with CroasterCore(dummy, &btProbe, &etProbe) and reads these instead of
// touching the chips. Values are the control task's filtered temperatures.
#include <Thermocouple.h>

#include "app/roaster.h"

class RoasterProbe : public Thermocouple {
public:
    enum class Channel : uint8_t { ET, BT };
    RoasterProbe(Roaster& roaster, Channel ch)
        : roaster_(roaster)
        , ch_(ch) {}
    double readCelsius() override {
        const Snapshot s = roaster_.snapshot();
        return ch_ == Channel::BT ? s.bt : s.et; // NaN on fault; Croaster reports it as missing
    }
    double readKelvin() override {
        return readCelsius() + 273.15;
    }
    double readFahrenheit() override {
        return readCelsius() * 9.0 / 5.0 + 32.0;
    }

private:
    Roaster& roaster_;
    Channel ch_;
};
