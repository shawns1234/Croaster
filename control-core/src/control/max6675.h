#pragma once
// Pure decoder for one 16-bit MAX6675 frame (datasheet table 2). Croaster's
// converter: 12-bit, 0..1023.75 C, 0.25 C/LSB, open-probe flag in D2.
// A new conversion takes up to 220 ms, so read at <= 4 Hz.
#include <cmath>
#include <cstdint>

#include "max31855.h" // ThermoReading, fault bit names

namespace max6675 {
inline ThermoReading decode(uint16_t raw) {
    ThermoReading r{};
    r.internalC = NAN; // no cold-junction output
    if (raw == 0 || raw == 0xFFFF)
        r.fault |= max31855::kFaultNoDevice;
    if (raw & 0x0004)
        r.fault |= max31855::kFaultOpen;
    r.ok = r.fault == 0;
    r.thermocoupleC = r.ok ? ((raw >> 3) & 0x0FFF) * 0.25f : NAN;
    return r;
}
} // namespace max6675
