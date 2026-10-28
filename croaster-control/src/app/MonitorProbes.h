#pragma once
// Croaster monitor (boards/esp32s3-monitor: no heater, no control task):
// reads both converters through hw/Thermocouples (MAX31855 or MAX6675,
// decoded by core/) and hands the filtered values to CroasterCore, built with
// CroasterCore(dummy, monitor.bt(), monitor.et()). The loop task owns the SPI
// bus here. CroasterCore reads BT, then ET, every 250 ms; the first call reads
// both chips and the second returns the cached sample.
#include <Arduino.h>
#include <Thermocouple.h>

#include "hw/thermocouples.h"

class MonitorProbes {
public:
    explicit MonitorProbes(Thermocouples& chips)
        : chips_(chips)
        , bt_(*this, true)
        , et_(*this, false) {}
    void begin() {
        chips_.begin();
    }
    Thermocouple* bt() {
        return &bt_;
    }
    Thermocouple* et() {
        return &et_;
    }

private:
    static constexpr uint32_t kSampleMs = 250; // CroasterCore's read period; the MAX6675 needs 220 ms per conversion

    const ThermoSample& sample() {
        const uint32_t now = millis();
        if (!sampled_ || now - sampledAtMs_ >= kSampleMs) {
            sample_ = chips_.read();
            sampledAtMs_ = now;
            sampled_ = true;
        }
        return sample_;
    }

    class Channel : public Thermocouple {
    public:
        Channel(MonitorProbes& owner, bool bt)
            : owner_(owner)
            , bt_(bt) {}
        double readCelsius() override {
            const ThermoSample& s = owner_.sample();
            return bt_ ? s.bt : s.et; // NaN on fault; Croaster reports it as missing
        }
        double readKelvin() override {
            return readCelsius() + 273.15;
        }
        double readFahrenheit() override {
            return readCelsius() * 9.0 / 5.0 + 32.0;
        }

    private:
        MonitorProbes& owner_;
        bool bt_;
    };

    Thermocouples& chips_;
    Channel bt_, et_;
    ThermoSample sample_{};
    uint32_t sampledAtMs_ = 0;
    bool sampled_ = false;
};
