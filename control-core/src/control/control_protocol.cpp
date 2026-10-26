#include "control_protocol.h"

#include <cmath>
#include <cstring>

namespace controlproto {

const char* const kNestedKeys[] = {"burner", "fan",         "setpoint", "mode",    "pidInput",
                                   "pid",    "cooldownFan", "profile",  "watchdog"};
const size_t kNestedKeyCount = sizeof(kNestedKeys) / sizeof(kNestedKeys[0]);
const char* const kBasicNames[] = {"startProfile", "stop", "autotune", "clearTrip", "getControl"};
const size_t kBasicNameCount = sizeof(kBasicNames) / sizeof(kBasicNames[0]);
const char* const kLegacyBasicNames[] = {"setBurner", "setFan", "setPreferences", "getPreferences"};
const size_t kLegacyBasicNameCount = sizeof(kLegacyBasicNames) / sizeof(kLegacyBasicNames[0]);

namespace {
void add(Parsed& p, Command::Kind k, float a = 0, float b = 0, float c = 0) {
    if (p.count >= Parsed::kMax)
        return;
    Command cmd{k};
    cmd.a = a;
    cmd.b = b;
    cmd.c = c;
    p.commands[p.count++] = cmd;
}
bool eq(const char* a, const char* b) {
    if (!a)
        return false;
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return false;
    }
    return *a == *b;
}
template <typename T>
void putNum(T&& v, float x, float scale = 10.0f) { // JSON has no NaN
    if (std::isfinite(x))
        v = std::round(x * scale) / scale;
    else
        v = nullptr;
}
} // namespace

namespace {
bool parsePidInput(const char* t, PidInput& out) {
    if (!(eq(t, "ET") || eq(t, "BT") || eq(t, "MAX")))
        return false;
    out = eq(t, "BT") ? PidInput::BT : (eq(t, "MAX") ? PidInput::MAX : PidInput::ET);
    return true;
}
} // namespace

Parsed parseNested(JsonObjectConst c, char unit, const char* only) {
    Parsed p;
    auto want = [only](const char* k) {
        return !only || std::strcmp(only, k) == 0;
    };
    if (want("mode") && c["mode"].is<const char*>()) { // mode before values so {"mode":"pid","setpoint":200} works
        const char* m = c["mode"];
        if (eq(m, "pid"))
            add(p, Command::Kind::SetMode, (float)Mode::Pid);
        if (eq(m, "manual"))
            add(p, Command::Kind::SetMode, (float)Mode::Manual);
    }
    if (want("pidInput") && c["pidInput"].is<const char*>()) {
        p.hasPidInput = parsePidInput(c["pidInput"], p.pidInput);
        if (p.hasPidInput)
            add(p, Command::Kind::SetPidInput, (float)p.pidInput);
    }
    if (want("pid") && c["pid"].is<JsonObjectConst>()) {
        JsonObjectConst g = c["pid"];
        if (g["kp"].is<float>() && g["ki"].is<float>() && g["kd"].is<float>()) {
            p.hasGains = true;
            p.gains = {g["kp"].as<float>(), g["ki"].as<float>(), g["kd"].as<float>()};
            add(p, Command::Kind::SetGains, p.gains.kp, p.gains.ki, p.gains.kd);
        }
    }
    if (want("setpoint") && c["setpoint"].is<float>())
        add(p, Command::Kind::SetSetpoint, toCelsius(c["setpoint"].as<float>(), unit));
    if (want("fan") && c["fan"].is<float>())
        add(p, Command::Kind::SetFan, c["fan"].as<float>());
    if (want("burner") && c["burner"].is<float>())
        add(p, Command::Kind::SetHeater, c["burner"].as<float>());
    if (want("cooldownFan") && c["cooldownFan"].is<float>()) {
        p.hasCooldownFan = true;
        p.cooldownFan = c["cooldownFan"].as<float>();
    }
    if (want("watchdog") && c["watchdog"].is<JsonObjectConst>()) {
        JsonObjectConst w = c["watchdog"];
        if (w["enabled"].is<bool>()) {
            p.hasWatchdogEnabled = true;
            p.watchdogEnabled = w["enabled"].as<bool>();
        }
        if (w["timeout"].is<float>()) {
            const float t = w["timeout"].as<float>();
            if (t >= WatchdogSettings::kMinTimeoutS && t <= WatchdogSettings::kMaxTimeoutS) {
                p.hasWatchdogTimeout = true;
                p.watchdogTimeoutS = static_cast<uint16_t>(std::lround(t));
            }
        }
    }
    if (want("profile") && !c["profile"].isNull()) {
        p.hasProfile = p.profile.loadJson(c["profile"].as<JsonArrayConst>(), unit);
        p.profileError = !p.hasProfile;
    }
    return p;
}

Parsed parseLegacy(JsonObjectConst m, char unit) {
    Parsed p;
    // Same order as yaeger's CommandLoop: Mode, BurnerVal, Setpoint, FanVal, Target.
    if (m["Mode"].is<const char*>()) {
        const char* s = m["Mode"];
        if (eq(s, "manual"))
            add(p, Command::Kind::SetMode, (float)Mode::Manual);
        if (eq(s, "pid"))
            add(p, Command::Kind::SetMode, (float)Mode::Pid);
    }
    if (m["BurnerVal"].is<float>())
        add(p, Command::Kind::SetHeater, m["BurnerVal"].as<float>());
    if (m["Setpoint"].is<float>())
        add(p, Command::Kind::SetSetpoint, toCelsius(m["Setpoint"].as<float>(), unit));
    if (m["FanVal"].is<float>())
        add(p, Command::Kind::SetFan, m["FanVal"].as<float>());
    if (m["Target"].is<const char*>()) {
        p.hasPidInput = parsePidInput(m["Target"], p.pidInput);
        if (p.hasPidInput)
            add(p, Command::Kind::SetPidInput, (float)p.pidInput);
    }
    const char* cmd = m["command"].is<const char*>() ? m["command"].as<const char*>() : nullptr;
    if (eq(cmd, "setBurner") && m["value"].is<float>())
        add(p, Command::Kind::SetHeater, m["value"].as<float>());
    if (eq(cmd, "setFan") && m["value"].is<float>())
        add(p, Command::Kind::SetFan, m["value"].as<float>());
    if (eq(cmd, "setPreferences")) {
        if (m["pidKp"].is<float>() && m["pidKi"].is<float>() && m["pidKd"].is<float>()) {
            p.hasGains = true;
            p.gains = {m["pidKp"].as<float>(), m["pidKi"].as<float>(), m["pidKd"].as<float>()};
            add(p, Command::Kind::SetGains, p.gains.kp, p.gains.ki, p.gains.kd);
        }
        if (m["cooldownFanSpeed"].is<float>()) {
            p.hasCooldownFan = true;
            p.cooldownFan = m["cooldownFanSpeed"].as<float>();
        }
    }
    return p;
}

bool basicCommand(const char* name, Command& out) {
    if (eq(name, "startProfile"))
        out = Command{Command::Kind::StartProfile};
    else if (eq(name, "stop"))
        out = Command{Command::Kind::Stop};
    else if (eq(name, "autotune"))
        out = Command{Command::Kind::StartAutotune};
    else if (eq(name, "clearTrip"))
        out = Command{Command::Kind::ClearTrip};
    else
        return false;
    return true;
}

void writeDataFields(const Snapshot& s, char unit, JsonObject d) {
    putNum(d["BurnerVal"], s.heaterPct);
    putNum(d["FanVal"], s.fanPct);
    putNum(d["SV"], fromCelsius(s.setpoint, unit));
    d["Mode"] = modeName(s.mode);
    d["trip"] = tripName(s.trip);
    putNum(d["Amb"], fromCelsius(s.ambient, unit));
    d["Target"] = pidInputName(s.pidInput);
    d["pidKp"] = s.gains.kp;
    d["pidKi"] = s.gains.ki;
    d["pidKd"] = s.gains.kd;
}

void writePreferences(const PidGains& g, float cooldownFan, JsonObject d) {
    d["type"] = "preferences";
    d["pidKp"] = g.kp;
    d["pidKi"] = g.ki;
    d["pidKd"] = g.kd;
    d["cooldownFanSpeed"] = cooldownFan;
}

void writeControlInfo(const Snapshot& s, float cooldownFan, const WatchdogSettings& wd, char unit, JsonObject o) {
    o["mode"] = modeName(s.mode);
    o["pidInput"] = pidInputName(s.pidInput);
    putNum(o["setpoint"], fromCelsius(s.setpoint, unit));
    putNum(o["burner"], s.heaterPct);
    putNum(o["fan"], s.fanPct);
    o["cooldownFan"] = cooldownFan;
    JsonObject w = o["watchdog"].to<JsonObject>();
    w["enabled"] = wd.enabled;
    w["timeout"] = wd.timeoutS;
    JsonObject g = o["pid"].to<JsonObject>();
    g["kp"] = s.gains.kp;
    g["ki"] = s.gains.ki;
    g["kd"] = s.gains.kd;
    o["autotune"] = s.autotuneMsg;
    o["trip"] = tripName(s.trip);
    o["mainsOk"] = s.mainsOk;
    putNum(o["profileElapsed"], s.profileElapsedS, 1.0f);
    putNum(o["ambient"], fromCelsius(s.ambient, unit));
}

} // namespace controlproto
