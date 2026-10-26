#pragma once
// Roaster settings in NVS. WiFi credentials are not here: Croaster's WiFiManager
// stores them. Namespace "roaster" (yaeger used "preferences-v1"; keys differ, so a
// yaeger board starts from defaults; set PID gains once or run autotune).
#include "core/roaster_types.h"

struct RoasterSettings {
    PidGains gains; // defaults from config.h (ROASTER_PID_KP/KI/KD)
    PidInput pidInput = PidInput::ET;
    float cooldownFan = 100;
};

namespace ConfigStore {
RoasterSettings load(const RoasterSettings& defaults);
void saveGains(const PidGains& g);
void savePidInput(PidInput t);
void saveCooldownFan(float pct);
} // namespace ConfigStore
