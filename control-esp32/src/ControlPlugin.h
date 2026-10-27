#pragma once
// Plugs the controller into a CroasterApp using only Croaster's extension
// points: onCommand()/onJsonCommand() for control, core().onJsonData() to add
// heater/fan/setpoint/mode to every data payload. Also accepts yaeger's named
// commands (setBurner, setFan, setPreferences, getPreferences). Croaster keeps
// WiFi, WebSocket (port 81), BLE, OTA, display and RoR. Commands: see control/control_protocol.h.
//
// Also owns two network-side safety/housekeeping jobs (loop task):
//  - comms watchdog: heating and no message through Croaster's command handler
//    (WebSocket or BLE, getArtisanData polls included) for the watchdog timeout
//    -> trip CommsLost (heater off, fan to the trip cooldown speed). Profile runs are
//    exempt (they are autonomous). Enable and timeout: ControlSettings::watchdog.
//  - persists PID gains after a successful autotune.
//  - passes Croaster's correction offsets (correctionBt/correctionEt) to the
//    controller, so PID works on the same corrected scale as the display.
// Firmware updates use Croaster's own OTA (WebSocket/BLE); the control task
// turns the heater off while one runs (Update.isRunning()).
#include <Arduino.h>

#include "app/config_store.h"
#include "app/controller.h"
#include "control/control_protocol.h"

class CroasterApp;

class ControlPlugin {
public:
    explicit ControlPlugin(Controller& controller)
        : controller_(controller) {}

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

    Controller& controller_;
    ControlSettings settings_;
    CroasterApp* app_ = nullptr;
    unsigned long watchdogArmedMs_ = 0;
    uint32_t autotuneSeq_ = 0;
    bool correctionSent_ = false;
    float correctionBtC_ = 0, correctionEtC_ = 0;
};
