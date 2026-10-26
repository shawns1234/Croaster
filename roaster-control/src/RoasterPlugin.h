#pragma once
// Plugs the roaster controller into a CroasterApp using only Croaster's extension
// points: onCommand()/onJsonCommand() for control, core().onJsonData() to add
// heater/fan/setpoint/mode to every data payload. Croaster keeps WiFi, WebSocket
// (port 81), BLE, OTA, display and RoR. Commands: see core/roaster_protocol.h.
//
// Also owns three network-side safety/housekeeping jobs (loop task):
//  - comms watchdog: heater on and no client command/poll for commsTimeoutMs ->
//    trip CommsLost (heater off). Profile runs are exempt (they are autonomous).
//  - persists PID gains after a successful autotune.
//  - optional ArduinoOTA (espota / `pio run -t upload` over WiFi).
#include <Arduino.h>

#include "app/config_store.h"
#include "app/roaster.h"

class CroasterApp;

struct RoasterPluginConfig {
    uint32_t commsTimeoutMs = 30000; // 0 disables the watchdog
    bool arduinoOta = false;
    const char* otaHostname = "roaster";
    const char* otaPassword = "";
};

class RoasterPlugin {
public:
    RoasterPlugin(Roaster& roaster, RoasterPluginConfig cfg)
        : roaster_(roaster)
        , cfg_(cfg) {}

    // Registers commands and the data hook. Call before app.begin(). `settings` are
    // the values loaded from NVS (ConfigStore::load) that the roaster runs with.
    void attach(CroasterApp& app, const RoasterSettings& settings);
    // Call after app.begin() (WiFi up), e.g. at the end of setup().
    void begin();
    // Call from loop() next to app.loop().
    void loop();

private:
    String handleNested(const JsonObject& json, const char* key);
    String handleBasic(const char* name);
    char unit() const;
    void touch() {
        lastActivityMs_ = millis();
    }

    Roaster& roaster_;
    RoasterSettings settings_;
    RoasterPluginConfig cfg_;
    CroasterApp* app_ = nullptr;
    uint32_t lastActivityMs_ = 0;
    uint32_t autotuneSeq_ = 0;
};
