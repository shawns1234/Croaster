#pragma once
// Autotune status text for telemetry and the OLED header (design D17).
// The controller records what happened (AutotuneStatus in the Snapshot); these
// turn it into words in the active display unit. Pure, so it is host-tested.
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "control_types.h"

namespace autotunetext {

// Telemetry and getControl: "idle", "running", "done", "failed: timeout",
// "aborted", "aborted: setpoint changed", "need setpoint >= 212 F",
// "need a valid probe".
inline void full(const Snapshot& s, char unit, char* out, size_t n) {
    const char* r = s.autotuneReason;
    switch (s.autotune) {
    case AutotuneStatus::Running:
        snprintf(out, n, "running");
        break;
    case AutotuneStatus::Done:
        snprintf(out, n, "done");
        break;
    case AutotuneStatus::Failed:
        snprintf(out, n, "failed: %s", r);
        break;
    case AutotuneStatus::Aborted:
        snprintf(out, n, "aborted%s%s", *r ? ": " : "", r);
        break;
    case AutotuneStatus::NeedSetpoint:
        snprintf(out, n, "need setpoint >= %.0f %c", fromCelsius(kAutotuneMinSetpointC, unit), unit);
        break;
    case AutotuneStatus::NeedProbe:
        snprintf(out, n, "need a valid probe");
        break;
    default:
        snprintf(out, n, "idle");
        break;
    }
}

// OLED header, at most 20 characters: "AT running", "AT done",
// "AT fail: timeout", "AT aborted", "AT: SV<212F", "AT: no probe".
inline void brief(const Snapshot& s, char unit, char* out, size_t n) {
    // Short forms of RelayAutotune's and the controller's reasons.
    static const char* const kShort[][2] = {
        {"sensor fault", "sensor"},
        {"over temperature", "overtemp"},
        {"oscillation too small vs hysteresis", "small swing"},
        {"setpoint changed", "SV change"},
    };
    const char* r = s.autotuneReason;
    for (const auto& m : kShort)
        if (std::strcmp(r, m[0]) == 0)
            r = m[1];
    char line[48];
    switch (s.autotune) {
    case AutotuneStatus::Running:
        snprintf(line, sizeof line, "AT running");
        break;
    case AutotuneStatus::Done:
        snprintf(line, sizeof line, "AT done");
        break;
    case AutotuneStatus::Failed:
        snprintf(line, sizeof line, "AT fail: %s", r);
        break;
    case AutotuneStatus::Aborted:
        if (*r)
            snprintf(line, sizeof line, "AT abort: %s", r);
        else
            snprintf(line, sizeof line, "AT aborted");
        break;
    case AutotuneStatus::NeedSetpoint:
        snprintf(line, sizeof line, "AT: SV<%.0f%c", fromCelsius(kAutotuneMinSetpointC, unit), unit);
        break;
    case AutotuneStatus::NeedProbe:
        snprintf(line, sizeof line, "AT: no probe");
        break;
    default:
        line[0] = '\0';
        break;
    }
    if (n == 0)
        return;
    size_t len = std::strlen(line);
    if (len > 20) // header limit
        len = 20;
    if (len > n - 1)
        len = n - 1;
    std::memcpy(out, line, len);
    out[len] = '\0';
}

} // namespace autotunetext
