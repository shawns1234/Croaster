#include "config_store.h"

#include <Preferences.h>

namespace {
constexpr const char* kNs = "control";
}

ControlSettings ConfigStore::load(const ControlSettings& d) {
    Preferences p;
    ControlSettings s = d;
    if (!p.begin(kNs, true))
        return s; // namespace absent on first boot
    s.gains.kp = p.getFloat("kp", d.gains.kp);
    s.gains.ki = p.getFloat("ki", d.gains.ki);
    s.gains.kd = p.getFloat("kd", d.gains.kd);
    s.pidInput = static_cast<PidInput>(p.getUChar("in", static_cast<uint8_t>(d.pidInput)));
    s.cooldownFan = p.getFloat("cool", d.cooldownFan);
    p.end();
    return s;
}

void ConfigStore::saveGains(const PidGains& g) {
    Preferences p;
    p.begin(kNs, false);
    p.putFloat("kp", g.kp);
    p.putFloat("ki", g.ki);
    p.putFloat("kd", g.kd);
    p.end();
}

void ConfigStore::savePidInput(PidInput t) {
    Preferences p;
    p.begin(kNs, false);
    p.putUChar("in", static_cast<uint8_t>(t));
    p.end();
}

void ConfigStore::saveCooldownFan(float pct) {
    Preferences p;
    p.begin(kNs, false);
    p.putFloat("cool", pct);
    p.end();
}
