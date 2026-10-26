#pragma once
// Vocabulary shared across the control seam: commands in, snapshots out.
#include <cmath>
#include <cstdint>

#include "pid.h"

enum class Mode : uint8_t { Manual, Pid, Autotune, Profile };
enum class PidInput : uint8_t { ET, BT, MAX }; // legacy "Target"

enum class SafetyTrip : uint8_t {
    None,
    SensorFault, // selected PID input (or both probes) unreadable
    OverTemp,    // any probe above limit
    NoMainsSync, // heater requested but dimmer sees no zero-cross (fan can't run)
    Firmware,    // an OTA update is writing flash
    CommsLost,   // remote manual control and no client traffic for the timeout
};

inline const char* tripName(SafetyTrip t) {
    switch (t) {
    case SafetyTrip::SensorFault:
        return "sensor";
    case SafetyTrip::OverTemp:
        return "overtemp";
    case SafetyTrip::NoMainsSync:
        return "nomains";
    case SafetyTrip::Firmware:
        return "ota";
    case SafetyTrip::CommsLost:
        return "comms";
    default:
        return "";
    }
}

// Comms watchdog: while heating outside a profile run, no client message for
// timeoutS seconds trips CommsLost. Defaults come from config.h; the
// {"command":{"watchdog":...}} command changes them and they are saved in NVS.
struct WatchdogSettings {
    static constexpr uint16_t kMinTimeoutS = 2;
    static constexpr uint16_t kMaxTimeoutS = 600;
    bool enabled = true;
    uint16_t timeoutS = 10;
};

// Fan % after a heater shutdown. Defaults come from config.h; the
// {"command":{"cooldownFan":...}} command changes them and they are saved in NVS.
struct CooldownFan {
    float stop = 65;  // after the stop command (yaeger's stored default)
    float trip = 100; // when a safety trip starts; over-temperature always uses 100
};

// Croaster reports temperatures in a user-selected unit ("C", "F" or "K"); the
// controller works in Celsius. These convert absolute temperatures.
inline float toCelsius(float v, char unit) {
    return unit == 'F' ? (v - 32.0f) * 5.0f / 9.0f : (unit == 'K' ? v - 273.15f : v);
}
inline float fromCelsius(float c, char unit) {
    return unit == 'F' ? c * 9.0f / 5.0f + 32.0f : (unit == 'K' ? c + 273.15f : c);
}

struct Command {
    enum class Kind : uint8_t {
        SetHeater,   // a = 0..100 % (manual request)
        SetFan,      // a = 0..100 %
        SetSetpoint, // a = C
        SetMode,     // a = (float)Mode  (Manual or Pid; others via Start*)
        SetPidInput, // a = (float)PidInput
        SetGains,    // a,b,c = kp,ki,kd
        StartAutotune,
        StartProfile, // profile must already be loaded via Controller::loadProfile
        Stop,         // heater 0, back to Manual (fan unchanged)
        EmergencyOff, // heater 0, fan 100, Manual
        SetTripFan,   // a = fan % applied when a safety trip starts
        Trip,         // a = (float)SafetyTrip; raised by the network side (comms, OTA)
        ClearTrip,
    };
    Kind kind;
    float a = 0, b = 0, c = 0;
};

struct Snapshot {
    float et = NAN, bt = NAN, ambient = NAN; // filtered; NaN on fault / before the first sample
    float etRaw = NAN, btRaw = NAN;          // this sample, NaN on fault
    uint8_t etFault = 0, btFault = 0;        // converter fault bits (see max31855.h / max6675.h)
    float heaterPct = 0;                     // requested 0..100 (PID output or manual)
    float fanPct = 0;
    float setpoint = 0;
    Mode mode = Mode::Manual;
    PidInput pidInput = PidInput::ET;
    PidGains gains;
    SafetyTrip trip = SafetyTrip::None;
    bool mainsOk = false;
    float profileElapsedS = 0;
    uint32_t autotuneSeq = 0; // increments when new gains were produced
    const char* autotuneMsg = "";
    uint32_t uptimeMs = 0;
};

// Heater output a new mode starts from (bumpless transfer: keep the current
// output). The autotune relay's 0/100 % is not an operating point, so a mode
// entered from Autotune starts from 0 instead.
inline float startOutputOnModeChange(Mode from, Mode to, float currentOutput) {
    return from == Mode::Autotune && to != Mode::Autotune ? 0.0f : currentOutput;
}

inline const char* modeName(Mode m) {
    switch (m) {
    case Mode::Pid:
        return "PID"; // legacy modeToChar() strings
    case Mode::Autotune:
        return "Tuning";
    case Mode::Profile:
        return "Profile";
    default:
        return "Manual";
    }
}
inline const char* pidInputName(PidInput t) {
    return t == PidInput::BT ? "BT" : (t == PidInput::MAX ? "MAX" : "ET");
}
