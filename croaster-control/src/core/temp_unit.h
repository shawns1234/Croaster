#pragma once
// Temperature unit kept across reboots. Croaster's tempUnit command changes the
// unit at run time; app/unit_store saves it to NVS and applies it at boot. These
// helpers hold the rules so the host tests cover them.
#include <cstring>

namespace tempunit {

// "C", "F" or "K": the units CroasterCore::changeTemperatureUnit() accepts.
constexpr bool valid(const char* u) {
    return u != nullptr && (u[0] == 'C' || u[0] == 'F' || u[0] == 'K') && u[1] == '\0';
}

// Unit to apply at boot: the saved one when valid, else the build default (CMON_TEMP_UNIT).
inline const char* atBoot(const char* saved, const char* fallback) {
    return valid(saved) ? saved : fallback;
}

// True when the current unit is valid and differs from what NVS holds (saves only real changes).
inline bool needsSave(const char* saved, const char* current) {
    return valid(current) && (!valid(saved) || std::strcmp(saved, current) != 0);
}

} // namespace tempunit
