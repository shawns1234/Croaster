#pragma once
// Vocabulary shared across the control seam: commands in, snapshots out.
#include <cmath>
#include <cstdint>

#include "pid.h"

enum class Mode : uint8_t { Manual, Pid, Autotune, Profile };
enum class PidInput : uint8_t { ET, BT, MAX }; // legacy "Target"

enum class SafetyTrip : uint8_t {
    None,
    SensorFault, // PID input unreadable outside Manual, or both probes while Manual heats
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

// Autotune refuses to start below this setpoint (100 C / 212 F). This rule is
// ours: yaeger's autotune has no setpoint gate.
constexpr float kAutotuneMinSetpointC = 100;

// Last autotune outcome, kept until the next one. Text: autotune_status.h.
enum class AutotuneStatus : uint8_t {
    Idle, // no autotune since boot
    Running,
    Done,         // new gains in use and saved
    Failed,       // autotuneReason: RelayAutotune::failure()
    Aborted,      // left by a command or a trip; autotuneReason may say why
    NeedSetpoint, // refused: setpoint below kAutotuneMinSetpointC
    NeedProbe,    // refused: PID input probe unreadable
};

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
        SetCorrection, // a, b = BT, ET correction offsets, C differences (CroasterCore::correctionBtCelsius)
    };
    Kind kind;
    float a = 0, b = 0, c = 0;
};

// One control-task sample of both probes (C), before any user correction.
struct ThermoSample {
    float et, bt;       // filtered (moving average), NaN when faulted
    float etRaw, btRaw; // this sample, NaN when faulted
    float ambient;      // MAX31855 cold-junction temp, BT chip first (NaN on MAX6675)
    uint8_t etFault, btFault;
};

// Probe temperatures here are uncorrected: CroasterCore adds the user's
// correction when it reads them through RoasterProbe.
struct Snapshot {
    float et = NAN, bt = NAN, ambient = NAN; // filtered; NaN on fault / before the first sample
    float etRaw = NAN, btRaw = NAN;          // this sample, NaN on fault
    uint8_t etFault = 0, btFault = 0;        // converter fault bits (see max31855.h / max6675.h)
    float heaterPct = 0;                     // requested 0..100 (PID output or manual)
    float fanPct = 0;
    float setpoint = 0;
    Mode mode = Mode::Manual;
    PidInput pidInput = PidInput::BT;
    PidGains gains;
    SafetyTrip trip = SafetyTrip::None;
    bool mainsOk = false;
    float profileElapsedS = 0;
    uint32_t autotuneSeq = 0; // increments when new gains were produced
    AutotuneStatus autotune = AutotuneStatus::Idle;
    const char* autotuneReason = ""; // string literal; "" when there is none
    uint32_t autotuneStatusSeq = 0;  // increments on every autotune status update
    uint32_t uptimeMs = 0;
};

// Heater output a new mode starts from (bumpless transfer: keep the current
// output). The autotune relay's 0/100 % is not an operating point, so a mode
// entered from Autotune starts from 0 instead.
inline float startOutputOnModeChange(Mode from, Mode to, float currentOutput) {
    return from == Mode::Autotune && to != Mode::Autotune ? 0.0f : currentOutput;
}

// What a step in the process value does to the running mode. Changing the PID
// input or a correction offset moves the PV at once. Autotune would measure the
// step as part of a cycle, so it stops (heater off); PID and Profile restart the
// PID bumpless so the derivative term does not kick; Manual ignores it.
enum class PvStepAction : uint8_t { None, AbortAutotune, ResetPid };
inline PvStepAction onPvStep(Mode m) {
    switch (m) {
    case Mode::Autotune:
        return PvStepAction::AbortAutotune;
    case Mode::Pid:
    case Mode::Profile:
        return PvStepAction::ResetPid;
    default:
        return PvStepAction::None;
    }
}

// An operator fan or heater command while autotune runs. The relay owns the
// heater and measures the cycle at the fan speed it started with, so any heater
// command, or a fan command that changes the speed, stops autotune (heater off),
// like a setpoint or offset change. Returns the abort reason, or nullptr. The
// fan rule at autotune start (below 30 % -> 60 %) is set by the controller, not
// a command, so it never counts.
inline const char* outputCommandStopsAutotune(Mode m, Command::Kind k, float requested, float currentFan) {
    if (m != Mode::Autotune)
        return nullptr;
    if (k == Command::Kind::SetHeater)
        return "heater changed";
    if (k == Command::Kind::SetFan) {
        const float fan = requested > 0 ? (requested < 100 ? requested : 100) : 0; // as the fan drivers clamp
        return fan != currentFan ? "fan changed" : nullptr;
    }
    return nullptr;
}

// The readings can't support the current heat; the controller trips
// SafetyTrip::SensorFault once this has held for 2 s. PID, Profile and Autotune
// need their input (pv). Manual needs at least one readable probe while the
// heater is on; with neither, the over-temperature trip is blind.
inline bool sensorFault(Mode m, float pv, const ThermoSample& s, float heaterPct) {
    if (m != Mode::Manual)
        return !std::isfinite(pv);
    return heaterPct > 0 && !std::isfinite(s.et) && !std::isfinite(s.bt);
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
