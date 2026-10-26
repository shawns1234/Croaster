// Host tests for control-core. Run: pio test -e native
#include <unity.h>

#include <cmath>
#include <cstring>

#include <ArduinoJson.h>

#include "control/control_protocol.h"
#include "control/max31855.h"
#include "control/max6675.h"
#include "control/moving_average.h"
#include "control/pid.h"
#include "control/probe_channel.h"
#include "control/relay_autotune.h"
#include "control/roast_profile.h"
#include "control/time_proportional.h"

void setUp() {}
void tearDown() {}

// ---------- MovingAverage ----------
void test_moving_average_seeds_with_first_sample() {
    MovingAverage<4> f;
    TEST_ASSERT_TRUE(std::isnan(f.value()));
    TEST_ASSERT_EQUAL_FLOAT(200.0f, f.push(200.0f)); // legacy filter would return 40
    f.push(NAN);                                     // ignored
    TEST_ASSERT_EQUAL_FLOAT(200.0f, f.value());
    f.push(204);
    f.push(204);
    f.push(204);
    f.push(204);
    TEST_ASSERT_EQUAL_FLOAT(204.0f, f.value());
}

// ---------- MAX31855 ----------
void test_max31855_decode() {
    // +100.25 C thermocouple (401 LSB), +25.0625 C internal (401 LSB)
    uint32_t raw = (401u << 18) | (401u << 4);
    auto r = max31855::decode(raw);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_FLOAT(100.25f, r.thermocoupleC);
    TEST_ASSERT_EQUAL_FLOAT(25.0625f, r.internalC);
    // -0.25 C thermocouple: 14-bit two's complement 0x3FFF
    r = max31855::decode((0x3FFFu << 18) | (401u << 4));
    TEST_ASSERT_EQUAL_FLOAT(-0.25f, r.thermocoupleC);
    // open thermocouple: D16 + D0
    r = max31855::decode((401u << 4) | 0x10000u | 0x1u);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(std::isnan(r.thermocoupleC));
    TEST_ASSERT_EQUAL_UINT8(max31855::kFaultOpen, r.fault);
    // nothing connected
    TEST_ASSERT_FALSE(max31855::decode(0).ok);
    TEST_ASSERT_FALSE(max31855::decode(0xFFFFFFFFu).ok);
}

void test_max31855_fault_bits() {
    const uint32_t room = (100u << 18) | (400u << 4);  // 25 C, 25 C
    auto r = max31855::decode(room | 0x10000u | 0x2u); // short to GND
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_UINT8(max31855::kFaultShortGnd, r.fault);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, r.internalC);  // cold junction still valid
    r = max31855::decode(room | 0x10000u | 0x4u); // short to VCC
    TEST_ASSERT_EQUAL_UINT8(max31855::kFaultShortVcc, r.fault);
    r = max31855::decode(room | 0x10000u); // D16 alone still means "fault"
    TEST_ASSERT_FALSE(r.ok);
    r = max31855::decode((4000u << 18) | (400u << 4)); // +1000 C, top of range
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, r.thermocoupleC);
    r = max31855::decode((100u << 18) | (0xF00u << 4)); // internal -16 C
    TEST_ASSERT_EQUAL_FLOAT(-16.0f, r.internalC);
}

// ---------- Two probes (ET + BT on separate CS pins) ----------
static ThermoReading reading(float c) {
    return max31855::decode((static_cast<uint32_t>(c * 4) << 18) | (400u << 4));
}
static ThermoReading openProbe() {
    return max31855::decode((400u << 4) | 0x10000u | max31855::kFaultOpen);
}

void test_probe_channels_independent_faults() {
    ProbeChannel et, bt;
    TEST_ASSERT_EQUAL_FLOAT(150.0f, et.push(reading(150)));
    TEST_ASSERT_EQUAL_FLOAT(120.0f, bt.push(reading(120)));
    // ET glitches twice: it holds its last average; BT is untouched
    TEST_ASSERT_EQUAL_FLOAT(150.0f, et.push(openProbe()));
    TEST_ASSERT_EQUAL_FLOAT(150.0f, et.push(openProbe()));
    TEST_ASSERT_EQUAL_FLOAT(120.0f, bt.push(reading(120)));
    // third consecutive fault: ET reports NaN (Croaster shows it as missing,
    // the controller trips if ET is the PID input)
    TEST_ASSERT_TRUE(std::isnan(et.push(openProbe())));
    TEST_ASSERT_EQUAL_FLOAT(120.0f, bt.push(reading(120)));
    // probe reconnected: starts from the new value, not the stale 150
    TEST_ASSERT_EQUAL_FLOAT(160.0f, et.push(reading(160)));
    // one good read between glitches resets the count
    et.push(openProbe());
    et.push(openProbe());
    et.push(reading(160));
    TEST_ASSERT_FALSE(std::isnan(et.push(openProbe())));
}

// ---------- TimeProportional ----------
void test_time_proportional_window() {
    TimeProportional tp(1000, 20);
    tp.setDuty(25);
    int on = 0;
    for (uint32_t t = 0; t < 1000; t++)
        on += tp.isOn(t);
    TEST_ASSERT_EQUAL_INT(250, on);
    tp.setDuty(1); // 10 ms < min pulse -> off
    on = 0;
    for (uint32_t t = 1000; t < 2000; t++)
        on += tp.isOn(t);
    TEST_ASSERT_EQUAL_INT(0, on);
    tp.setDuty(99); // 990 ms, 10 ms off < min pulse -> full on
    on = 0;
    for (uint32_t t = 2000; t < 3000; t++)
        on += tp.isOn(t);
    TEST_ASSERT_EQUAL_INT(1000, on);
}

// ---------- PID ----------
void test_pid_integrator_does_not_wind_up() {
    Pid::Config c;
    c.dtSeconds = 0.25f;
    Pid pid(c);
    pid.setGains({2.0f, 0.5f, 0.0f});
    for (int i = 0; i < 4000; i++)
        pid.update(200, 20); // huge error, saturated for 1000 s
    TEST_ASSERT_TRUE(pid.integralTerm() <= 100.0f);
    // once PV passes setpoint the output must come off the rail quickly
    float u = 100;
    for (int i = 0; i < 8 && u > 0; i++)
        u = pid.update(200, 210);
    TEST_ASSERT_TRUE(u < 100.0f);
}

void test_pid_bumpless_reset() {
    Pid pid(Pid::Config{});
    pid.setGains({3.0f, 0.2f, 1.0f});
    pid.reset(180, 175, 42.0f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 42.0f, pid.update(180, 175));
}

// ---------- Relay autotune on a simulated roaster ----------
// First-order-plus-dead-time plant: tau dT/dt = K*u - (T - Tamb), dead time L.
struct Fopdt {
    float T = 25, amb = 25, K = 2.6f /*C per % at steady state*/, tau = 90, L = 6, dt = 0.25f;
    float hist[256] = {};
    int head = 0;
    float step(float u) {
        const int n = (int)(L / dt);
        hist[head % 256] = u;
        const float delayed = head >= n ? hist[(head - n) % 256] : 0;
        head++;
        T += dt / tau * (K * delayed - (T - amb));
        return T;
    }
};

void test_relay_autotune_identifies_and_controls() {
    Fopdt plant;
    RelayAutotune at;
    RelayAutotune::Config c;
    c.setpoint = 180;
    c.outputHigh = 100;
    c.outputLow = 0;
    c.hysteresis = 0.5f;
    at.start(c);
    float pv = plant.T;
    for (int i = 0; i < 40000 && at.state() == RelayAutotune::State::Running; i++)
        pv = plant.step(at.update(pv));
    TEST_ASSERT_EQUAL(RelayAutotune::State::Done, at.state());
    TEST_ASSERT_TRUE(at.ku() > 0 && at.tuSeconds() > 5 && at.tuSeconds() < 120);

    // closed loop with the tuned gains: step from 150 to 200 C
    Fopdt p2;
    p2.T = 150;
    Pid::Config pc;
    pc.dtSeconds = 0.25f;
    Pid pid(pc);
    pid.setGains(at.result());
    pid.reset(150, 150, (150 - 25) / p2.K);
    float maxT = 0, T = p2.T;
    for (int i = 0; i < 4 * 900; i++) { // 15 min
        T = p2.step(pid.update(200, T));
        if (T > maxT)
            maxT = T;
    }
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 200.0f, T); // settled
    TEST_ASSERT_TRUE(maxT < 208.0f);           // modest overshoot
}

void test_legacy_autotune_math_is_plant_independent() {
    // Documents the vendored AutoTunePID result: Tu=18s/10, Ku=4/pi, ZN with Ti=0.34Tu, Td=0.085Tu
    const float tu = 18.0f / 10.0f, ku = 4.0f / 3.14159265f;
    const float kp = 0.6f * ku, ki = kp / (2 * 0.17f * tu), kd = kp * (0.17f * tu / 2);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.764f, kp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.248f, ki);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.117f, kd);
}

// ---------- MAX6675 ----------
void test_max6675_decode() {
    auto r = max6675::decode(static_cast<uint16_t>(801u << 3)); // 200.25 C
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_FLOAT(200.25f, r.thermocoupleC);
    r = max6675::decode(static_cast<uint16_t>((801u << 3) | 0x4)); // open probe
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(std::isnan(r.thermocoupleC));
    TEST_ASSERT_FALSE(max6675::decode(0xFFFF).ok); // no chip / MISO floating high
}

// ---------- RoastProfile ----------
static bool loadProfile(RoastProfile& p, const char* json, char unit = 'C') {
    JsonDocument d;
    if (deserializeJson(d, json))
        return false;
    return p.loadJson(d.as<JsonArrayConst>(), unit);
}

void test_profile_matches_readme_example() {
    RoastProfile p;
    TEST_ASSERT_TRUE(loadProfile(p, R"([{"duration":10,"setpoint":40,"interpolation":"linear"},
                                       {"duration":360,"setpoint":217,"interpolation":"ease-out"}])"));
    TEST_ASSERT_EQUAL_FLOAT(40.0f, p.at(5).setpointC);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, p.at(10).setpointC);
    // halfway through ease-out: 40 + 177 * 0.75 = 172.75
    TEST_ASSERT_FLOAT_WITHIN(0.11f, 172.7f, p.at(190).setpointC);
    TEST_ASSERT_TRUE(p.at(400).finished);
    TEST_ASSERT_EQUAL_FLOAT(217.0f, p.at(400).setpointC);
}

void test_profile_in_fahrenheit() {
    RoastProfile p;
    TEST_ASSERT_TRUE(loadProfile(p, R"([{"duration":60,"setpoint":392}])", 'F'));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f, p.at(60).setpointC);
}

// ---------- Controller protocol (Croaster nested commands) ----------
static controlproto::Parsed parse(const char* json, char unit = 'C', const char* only = nullptr) {
    JsonDocument d;
    deserializeJson(d, json);
    return controlproto::parseNested(d["command"].as<JsonObjectConst>(), unit, only);
}

void test_protocol_values() {
    auto p = parse(R"({"command":{"burner":65}})");
    TEST_ASSERT_EQUAL(1, (int)p.count);
    TEST_ASSERT_TRUE(p.commands[0].kind == Command::Kind::SetHeater);
    TEST_ASSERT_EQUAL_FLOAT(65.0f, p.commands[0].a);

    p = parse(R"({"command":{"setpoint":428,"mode":"pid"}})", 'F'); // mode applied first
    TEST_ASSERT_EQUAL(2, (int)p.count);
    TEST_ASSERT_TRUE(p.commands[0].kind == Command::Kind::SetMode);
    TEST_ASSERT_EQUAL_FLOAT((float)Mode::Pid, p.commands[0].a);
    TEST_ASSERT_TRUE(p.commands[1].kind == Command::Kind::SetSetpoint);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 220.0f, p.commands[1].a); // 428 F -> 220 C

    p = parse(R"({"command":{"pid":{"kp":5,"ki":0.1,"kd":2},"pidInput":"BT","cooldownFan":80}})");
    TEST_ASSERT_TRUE(p.hasGains && p.hasPidInput && p.hasCooldownFan);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, p.gains.kp);
    TEST_ASSERT_TRUE(p.pidInput == PidInput::BT);
    TEST_ASSERT_EQUAL_FLOAT(80.0f, p.cooldownFan);
}

void test_protocol_watchdog() {
    auto p = parse(R"({"command":{"watchdog":{"enabled":false}}})");
    TEST_ASSERT_TRUE(p.hasWatchdogEnabled);
    TEST_ASSERT_FALSE(p.watchdogEnabled);
    TEST_ASSERT_FALSE(p.hasWatchdogTimeout);
    TEST_ASSERT_EQUAL(0, (int)p.count); // a setting, not a control command

    p = parse(R"({"command":{"watchdog":{"enabled":true,"timeout":20}}})", 'C', "watchdog");
    TEST_ASSERT_TRUE(p.hasWatchdogEnabled && p.watchdogEnabled);
    TEST_ASSERT_TRUE(p.hasWatchdogTimeout);
    TEST_ASSERT_EQUAL_UINT16(20, p.watchdogTimeoutS);

    p = parse(R"({"command":{"watchdog":{"timeout":0}}})"); // out of range: ignored
    TEST_ASSERT_FALSE(p.hasWatchdogTimeout);
    p = parse(R"({"command":{"watchdog":{"timeout":601}}})");
    TEST_ASSERT_FALSE(p.hasWatchdogTimeout);

    Snapshot s;
    WatchdogSettings wd{false, 30};
    JsonDocument d;
    controlproto::writeControlInfo(s, 100, wd, 'C', d.to<JsonObject>());
    TEST_ASSERT_FALSE(d["watchdog"]["enabled"].as<bool>());
    TEST_ASSERT_EQUAL(30, d["watchdog"]["timeout"].as<int>());
}

void test_protocol_only_key() {
    // Croaster calls each onJsonCommand(key) callback with the whole object:
    // each callback must handle only its own key, or values apply twice.
    auto p = parse(R"({"command":{"burner":40,"fan":70}})", 'C', "fan");
    TEST_ASSERT_EQUAL(1, (int)p.count);
    TEST_ASSERT_TRUE(p.commands[0].kind == Command::Kind::SetFan);
    p = parse(R"({"command":{"tempUnit":"F"}})", 'C', "burner"); // not ours
    TEST_ASSERT_EQUAL(0, (int)p.count);
}

void test_protocol_profile() {
    auto p = parse(
        R"({"command":{"profile":[{"duration":60,"setpoint":150},{"duration":120,"setpoint":200,"fanValue":70}]}})");
    TEST_ASSERT_TRUE(p.hasProfile);
    TEST_ASSERT_EQUAL(2, (int)p.profile.size());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f, p.profile.totalDurationS());
    p = parse(R"({"command":{"profile":"nope"}})");
    TEST_ASSERT_TRUE(p.profileError);
}

void test_protocol_basic_and_data() {
    Command c;
    TEST_ASSERT_TRUE(controlproto::basicCommand("startProfile", c));
    TEST_ASSERT_TRUE(c.kind == Command::Kind::StartProfile);
    TEST_ASSERT_TRUE(controlproto::basicCommand("STOP", c));
    TEST_ASSERT_TRUE(c.kind == Command::Kind::Stop);
    TEST_ASSERT_FALSE(controlproto::basicCommand("getControl", c));

    Snapshot s;
    s.heaterPct = 55;
    s.fanPct = 70;
    s.setpoint = 200;
    s.mode = Mode::Pid;
    JsonDocument d;
    JsonObject data = d["data"].to<JsonObject>();
    controlproto::writeDataFields(s, 'F', data);
    char buf[384];
    serializeJson(d, buf, sizeof buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"Target\":\"ET\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"pidKp\":0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"Amb\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"BurnerVal\":55"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"FanVal\":70"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"SV\":392"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"Mode\":\"PID\""));
    s.setpoint = NAN;
    controlproto::writeDataFields(s, 'C', data);
    serializeJson(d, buf, sizeof buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"SV\":null"));
}

static controlproto::Parsed parseLegacy(const char* json, char unit = 'C') {
    JsonDocument d;
    deserializeJson(d, json);
    return controlproto::parseLegacy(d.as<JsonObjectConst>(), unit);
}

void test_protocol_yaeger_compat() {
    // yaeger's Artisan sliders/buttons send flat keys plus Artisan's id/roasterID
    auto p = parseLegacy(R"({"BurnerVal":60,"id":123,"roasterID":0})");
    TEST_ASSERT_EQUAL(1, (int)p.count);
    TEST_ASSERT_TRUE(p.commands[0].kind == Command::Kind::SetHeater);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, p.commands[0].a);

    p = parseLegacy(R"({"Setpoint":428,"Mode":"PID","FanVal":55,"Target":"MAX"})", 'F');
    TEST_ASSERT_EQUAL(4, (int)p.count); // Mode first, as in yaeger's CommandLoop
    TEST_ASSERT_TRUE(p.commands[0].kind == Command::Kind::SetMode);
    TEST_ASSERT_EQUAL_FLOAT((float)Mode::Pid, p.commands[0].a);
    TEST_ASSERT_TRUE(p.commands[1].kind == Command::Kind::SetSetpoint);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 220.0f, p.commands[1].a);
    TEST_ASSERT_TRUE(p.commands[2].kind == Command::Kind::SetFan);
    TEST_ASSERT_TRUE(p.hasPidInput && p.pidInput == PidInput::MAX);

    p = parseLegacy(R"({"command":"setFan","value":80})");
    TEST_ASSERT_EQUAL(1, (int)p.count);
    TEST_ASSERT_TRUE(p.commands[0].kind == Command::Kind::SetFan);

    p = parseLegacy(R"({"command":"setPreferences","pidKp":4,"pidKi":0.2,"pidKd":1,"cooldownFanSpeed":70})");
    TEST_ASSERT_TRUE(p.hasGains && p.hasCooldownFan);
    TEST_ASSERT_EQUAL_FLOAT(0.2f, p.gains.ki);
    TEST_ASSERT_EQUAL_FLOAT(70.0f, p.cooldownFan);

    p = parseLegacy(R"({"command":"getData","id":5})"); // not a yaeger control message
    TEST_ASSERT_EQUAL(0, (int)p.count);
    TEST_ASSERT_FALSE(p.hasGains || p.hasCooldownFan || p.hasPidInput);

    JsonDocument d;
    controlproto::writePreferences(PidGains{4, 0.5f, 1}, 70, d.to<JsonObject>());
    char buf[160];
    serializeJson(d, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING(R"({"type":"preferences","pidKp":4,"pidKi":0.5,"pidKd":1,"cooldownFanSpeed":70})", buf);
}

void test_unit_conversion() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, toCelsius(212.0f, 'F'));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, toCelsius(273.15f, 'K'));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 212.0f, fromCelsius(100.0f, 'F'));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, fromCelsius(toCelsius(50.0f, 'C'), 'C'));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_moving_average_seeds_with_first_sample);
    RUN_TEST(test_max31855_decode);
    RUN_TEST(test_max31855_fault_bits);
    RUN_TEST(test_probe_channels_independent_faults);
    RUN_TEST(test_max6675_decode);
    RUN_TEST(test_time_proportional_window);
    RUN_TEST(test_pid_integrator_does_not_wind_up);
    RUN_TEST(test_pid_bumpless_reset);
    RUN_TEST(test_relay_autotune_identifies_and_controls);
    RUN_TEST(test_legacy_autotune_math_is_plant_independent);
    RUN_TEST(test_profile_matches_readme_example);
    RUN_TEST(test_profile_in_fahrenheit);
    RUN_TEST(test_protocol_values);
    RUN_TEST(test_protocol_watchdog);
    RUN_TEST(test_protocol_only_key);
    RUN_TEST(test_protocol_profile);
    RUN_TEST(test_protocol_basic_and_data);
    RUN_TEST(test_protocol_yaeger_compat);
    RUN_TEST(test_unit_conversion);
    return UNITY_END();
}
