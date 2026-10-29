#include "unit_store.h"

#include <CroasterApp.h>
#include <Preferences.h>

#include "core/temp_unit.h"

namespace {
constexpr const char* kNs = "croaster"; // NVS namespace for Croaster settings shared by every board
constexpr const char* kKey = "unit";
String saved; // what NVS holds ("" until the first save)
} // namespace

void UnitStore::attach(CroasterApp& app, const char* fallback) {
    Preferences p;
    if (p.begin(kNs, true)) { // read-only open fails while the namespace does not exist yet
        if (p.isKey(kKey))
            saved = p.getString(kKey, "");
        p.end();
    }
    CroasterCore* core = &app.core();
    core->changeTemperatureUnit(tempunit::atBoot(saved.c_str(), fallback));

    app.commands().onJsonCommand("tempUnit", [core](const JsonObject& json) {
        const String current = core->temperatureUnit();
        const String requested = json["tempUnit"] | "";
        // Save only a request Croaster applied; an invalid unit leaves the current one in place.
        if (requested == current && tempunit::needsSave(saved.c_str(), current.c_str())) {
            Preferences w;
            if (w.begin(kNs, false)) {
                w.putString(kKey, current);
                w.end();
                saved = current;
            }
        }
        return String();
    });
}
