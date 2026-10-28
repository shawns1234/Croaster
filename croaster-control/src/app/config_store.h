#pragma once
// Control settings in NVS. WiFi credentials are not here: Croaster's WiFiManager
// stores them. Namespace "control" (yaeger used "preferences-v1"; keys differ, so a
// yaeger board starts from defaults; set PID gains once or run autotune).
#include "core/control_types.h"

struct ControlSettings {
    PidGains gains;                   // defaults from config.h (FLEX_PID_KP/KI/KD)
    PidInput pidInput = PidInput::BT; // default from config.h (FLEX_PID_INPUT)
    CooldownFan cooldownFan;          // defaults from config.h (FLEX_COOLDOWN_FAN_STOP/TRIP)
    WatchdogSettings watchdog;        // defaults from config.h (FLEX_WATCHDOG_*)
};

namespace ConfigStore {
ControlSettings load(const ControlSettings& defaults);
void saveGains(const PidGains& g);
void savePidInput(PidInput t);
void saveCooldownFan(const CooldownFan& c);
void saveWatchdog(const WatchdogSettings& w);
} // namespace ConfigStore
