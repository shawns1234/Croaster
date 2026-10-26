#pragma once
// Plugs the controller into a CroasterApp using only Croaster's extension
// points: onCommand()/onJsonCommand() for control, core().onJsonData() to add
// heater/fan/setpoint/mode to every data payload. Also accepts yaeger's message
// format (flat keys via onOtherJson(), setBurner/setFan/get|setPreferences). Croaster keeps WiFi, WebSocket
// (port 81), BLE, OTA, display and RoR. Commands: see control/control_protocol.h.
//
// Also owns two network-side safety/housekeeping jobs (loop task):
//  - comms watchdog: heater on and no client command/poll for commsTimeoutMs ->
//    trip CommsLost (heater off, fan to the cooldown speed). Profile runs are
//    exempt (they are autonomous).
//  - persists PID gains after a successful autotune.
// Firmware updates use Croaster's own OTA (WebSocket/BLE); the control task
// turns the heater off while one runs (Update.isRunning()).
#include <Arduino.h>

#include "app/config_store.h"
#include "app/controller.h"
#include "control/control_protocol.h"

class CroasterApp;

struct ControlPluginConfig {
    uint32_t commsTimeoutMs = 10000; // 0 disables the watchdog
};

class ControlPlugin {
public:
    ControlPlugin(Controller& controller, ControlPluginConfig cfg)
        : controller_(controller)
        , cfg_(cfg) {}

    // Registers commands and the data hook. Call before app.begin(). `settings` are
    // the values loaded from NVS (ConfigStore::load) that the controller runs with.
    void attach(CroasterApp& app, const ControlSettings& settings);
    // Call after app.begin() (WiFi up), e.g. at the end of setup().
    void begin();
    // Call from loop() next to app.loop().
    void loop();

private:
    String handleNested(const JsonObject& json, const char* key);
    String handleLegacy(const JsonObject& json, const char* name);
    // Submits the parsed commands and persists gains / PID input / cooldown fan.
    void apply(const controlproto::Parsed& p);
    String handleBasic(const char* name);
    char unit() const;
    void touch() {
        lastActivityMs_ = millis();
    }

    Controller& controller_;
    ControlSettings settings_;
    ControlPluginConfig cfg_;
    CroasterApp* app_ = nullptr;
    uint32_t lastActivityMs_ = 0;
    uint32_t autotuneSeq_ = 0;
};
