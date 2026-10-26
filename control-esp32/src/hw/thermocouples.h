#pragma once
// Two thermocouple converters (ET, BT) on one SPI bus: shared SCK/MISO, one CS each.
// Chip is chosen at construction: MAX31855 (32-bit frame, fault bits, cold junction)
// or MAX6675 (16-bit frame, open-probe bit). Read at <= 4 Hz (MAX6675 needs 220 ms
// per conversion). Only the control task calls read(), so it owns the bus.
#include <cstdint>

#include "control/max31855.h"
#include "control/probe_channel.h"

enum class TcChip : uint8_t { MAX31855, MAX6675 };

struct ThermoSample {
    float et, bt;       // filtered (moving average), NaN when faulted
    float etRaw, btRaw; // this sample, NaN when faulted
    float ambient;      // MAX31855 cold-junction temp, BT chip first (NaN on MAX6675)
    uint8_t etFault, btFault;
};

class Thermocouples {
public:
    Thermocouples(TcChip chip, int sck, int miso, int csEt, int csBt)
        : chip_(chip)
        , sck_(sck)
        , miso_(miso)
        , csEt_(csEt)
        , csBt_(csBt) {}
    void begin();
    ThermoSample read();

private:
    ThermoReading readChip(int cs);
    TcChip chip_;
    int sck_, miso_, csEt_, csBt_;
    ProbeChannel et_, bt_;
};
