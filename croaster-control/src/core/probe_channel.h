#pragma once
// One thermocouple channel (ET or BT): a 4-sample moving average that rides
// through single glitches and reports NaN once the chip faults 3 reads in a
// row. Pure, so the fault behaviour is host-tested; hw/thermocouples feeds it
// one decoded reading per chip per sample.
#include <cstdint>

#include "max31855.h"
#include "moving_average.h"

class ProbeChannel {
public:
    static constexpr uint8_t kFaultsBeforeNaN = 3;

    // Returns the filtered temperature (C), NaN while the channel is faulted.
    float push(const ThermoReading& r) {
        bad_ = r.ok ? 0 : (bad_ < 255 ? bad_ + 1 : 255);
        if (bad_ >= kFaultsBeforeNaN)
            avg_.reset(); // a reconnected probe starts fresh, not from stale values
        return avg_.push(r.thermocoupleC);
    }

private:
    MovingAverage<4> avg_; // 4 samples @ 4 Hz = 1 s window
    uint8_t bad_ = 0;      // consecutive faults
};
