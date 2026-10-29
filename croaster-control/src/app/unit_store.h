#pragma once
// Saves the temperature unit set by Croaster's {"command":{"tempUnit":"C"}} to NVS
// and applies it at boot. The build default (CMON_TEMP_UNIT) is used until a unit
// has been saved. Croaster's own handler still does the conversion; this only adds
// an onJsonCommand("tempUnit") callback, which runs after it.
class CroasterApp;

namespace UnitStore {
// Call in setup(), before app.begin(): NVS is not usable from global constructors.
void attach(CroasterApp& app, const char* fallback);
} // namespace UnitStore
