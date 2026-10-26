#include "roaster_protocol.h"

#include <cmath>
#include <cstring>

namespace roasterproto {

const char* const kNestedKeys[] = {"burner", "fan", "setpoint", "mode", "pidInput", "pid", "cooldownFan", "profile"};
const size_t kNestedKeyCount = sizeof(kNestedKeys) / sizeof(kNestedKeys[0]);
const char* const kBasicNames[] = {"startProfile", "stop", "autotune", "clearTrip", "getRoaster"};
const size_t kBasicNameCount = sizeof(kBasicNames) / sizeof(kBasicNames[0]);

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
        const char* t = c["pidInput"];
        p.hasPidInput = eq(t, "ET") || eq(t, "BT") || eq(t, "MAX");
        p.pidInput = eq(t, "BT") ? PidInput::BT : (eq(t, "MAX") ? PidInput::MAX : PidInput::ET);
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
    if (want("profile") && !c["profile"].isNull()) {
        p.hasProfile = p.profile.loadJson(c["profile"].as<JsonArrayConst>(), unit);
        p.profileError = !p.hasProfile;
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
}

void writeRoasterInfo(const Snapshot& s, float cooldownFan, char unit, JsonObject o) {
    o["mode"] = modeName(s.mode);
    o["pidInput"] = pidInputName(s.pidInput);
    putNum(o["setpoint"], fromCelsius(s.setpoint, unit));
    putNum(o["burner"], s.heaterPct);
    putNum(o["fan"], s.fanPct);
    o["cooldownFan"] = cooldownFan;
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

} // namespace roasterproto
