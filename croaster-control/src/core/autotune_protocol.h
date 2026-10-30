#pragma once
// Autotune commands and the getAutotune reply (docs/autotune/implementation.md).
//   {"command":"autotune"}                        start (control_protocol.h)
//   {"command":{"autotune":"apply"|"discard"}}    pending results -> gains, or drop them
//   {"command":"getAutotune"}                     plan, progress, results and log
// Temperatures and swings are in Croaster's current unit; Ku and the gains stay
// in the controller's units (% per C), the same as the pid command.
// Pure (ArduinoJson only), so it is host-tested.
#include <ArduinoJson.h>

#include "control_types.h"

namespace autotuneproto {

// "apply" -> ApplyAutotune, "discard" -> DiscardAutotune; false otherwise.
bool parseAction(JsonVariantConst v, Command& out);

// One log entry as text: "heating to 350 F", "cycle 2: swing 6.1 F, period 131 s".
void logText(const autotune::LogEntry& e, const autotune::Report& r, char unit, char* out, size_t n);

// Body of the getAutotune reply. `plan` is the configured run (points, cycle
// counts, rule); `estimateS` the expected length from the current PV.
void writeReport(const Snapshot& s, const autotune::Report& r, const autotune::Config& plan, float estimateS,
                 char unit, JsonObject o);

} // namespace autotuneproto
