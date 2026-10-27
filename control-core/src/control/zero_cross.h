#pragma once
// Mains detection for the AC dimmer. The zero-cross ISR stores the time of the
// last crossing; the control task compares it with the timer. The ISR runs on
// the same core and can fire between the two reads, so the stored time may be
// later than the control task's "now". That means a crossing just happened,
// not one 2^64 us ago.
#include <cstdint>

inline bool crossedWithin(uint64_t lastCrossUs, uint64_t nowUs, uint32_t windowUs) {
    return lastCrossUs >= nowUs || nowUs - lastCrossUs < windowUs;
}
