#include "ControlPlugin.h"

#include <ArduinoJson.h>
#include <CroasterApp.h>
#include <WiFi.h>

#include <cstring>

namespace {
String reply(const char* command, const JsonDocument& response) {
    JsonDocument doc; // same envelope as Croaster's genResponseCommand()
    doc["command"] = command;
    doc["response"] = response;
    String out;
    serializeJson(doc, out);
    return out;
}
} // namespace

char ControlPlugin::unit() const {
    const String u = app_ ? app_->core().temperatureUnit() : String("C");
    return u.length() ? u[0] : 'C';
}

void ControlPlugin::attach(CroasterApp& app, const ControlSettings& settings) {
    app_ = &app;
    settings_ = settings;
    for (size_t i = 0; i < controlproto::kNestedKeyCount; i++) {
        const char* key = controlproto::kNestedKeys[i];
        app.commands().onJsonCommand(key, [this, key](const JsonObject& json) {
            return handleNested(json, key);
        });
    }
    for (size_t i = 0; i < controlproto::kBasicNameCount; i++) {
        const char* name = controlproto::kBasicNames[i];
        app.commands().onCommand(name, [this, name](const JsonObject&) {
            return handleBasic(name);
        });
    }
    for (size_t i = 0; i < controlproto::kLegacyBasicNameCount; i++) {
        const char* name = controlproto::kLegacyBasicNames[i];
        app.commands().onCommand(name, [this, name](const JsonObject& json) {
            return handleLegacy(json, name);
        });
    }
    // yaeger's flat keys ({"BurnerVal":50}, {"Mode":"PID"}, ...) carry no "command".
    app.commands().onOtherJson([this](const JsonObject& json) {
        return handleLegacy(json, nullptr);
    });
    app.core().onJsonData([this](JsonObject& data) {
        controlproto::writeDataFields(controller_.snapshot(), unit(), data);
        if (WiFi.status() == WL_CONNECTED)
            data["wifiStrength"] = WiFi.RSSI();
    });
}

String ControlPlugin::handleNested(const JsonObject& json, const char* key) {
    if (json[key].isNull())
        return "";
    controlproto::Parsed p = controlproto::parseNested(json, unit(), key);
    apply(p);
    if (p.hasProfile || p.profileError) {
        JsonDocument r;
        r["loaded"] = p.hasProfile && controller_.loadProfile(p.profile);
        r["steps"] = p.profile.size();
        return reply("profile", r);
    }
    return "";
}

String ControlPlugin::handleLegacy(const JsonObject& json, const char* name) {
    controlproto::Parsed p = controlproto::parseLegacy(json, unit());
    if (p.count == 0 && !p.hasCooldownFan && !name)
        return ""; // unrelated message without "command"
    apply(p);
    if (!name || std::strcmp(name, "setFan") == 0 || std::strcmp(name, "setBurner") == 0)
        return "";
    JsonDocument r; // yaeger answers set/getPreferences with the stored values
    r["id"] = json["id"] | 0;
    controlproto::writePreferences(settings_.gains, settings_.cooldownFan, r["data"].to<JsonObject>());
    String out;
    serializeJson(r, out);
    return out;
}

void ControlPlugin::apply(const controlproto::Parsed& p) {
    for (size_t i = 0; i < p.count; i++)
        controller_.submit(p.commands[i]);
    if (p.hasGains) {
        settings_.gains = p.gains;
        ConfigStore::saveGains(p.gains);
    }
    if (p.hasPidInput) {
        settings_.pidInput = p.pidInput;
        ConfigStore::savePidInput(p.pidInput);
    }
    if (p.hasCooldownFan) {
        settings_.cooldownFan = p.cooldownFan;
        ConfigStore::saveCooldownFan(p.cooldownFan);
    }
    if (p.hasWatchdogEnabled || p.hasWatchdogTimeout) {
        if (p.hasWatchdogEnabled)
            settings_.watchdog.enabled = p.watchdogEnabled;
        if (p.hasWatchdogTimeout)
            settings_.watchdog.timeoutS = p.watchdogTimeoutS;
        ConfigStore::saveWatchdog(settings_.watchdog);
        watchdogArmedMs_ = millis();
    }
}

String ControlPlugin::handleBasic(const char* name) {
    Command c;
    if (controlproto::basicCommand(name, c)) {
        controller_.submit(c);
        if (c.kind == Command::Kind::Stop && settings_.cooldownFan >= 0) {
            Command fan{Command::Kind::SetFan};
            fan.a = settings_.cooldownFan;
            controller_.submit(fan);
        }
        return "";
    }
    JsonDocument r; // getControl
    controlproto::writeControlInfo(controller_.snapshot(), settings_.cooldownFan, settings_.watchdog, unit(),
                                   r.to<JsonObject>());
    return reply(name, r);
}

void ControlPlugin::begin() {
    autotuneSeq_ = controller_.snapshot().autotuneSeq;
    watchdogArmedMs_ = millis();
}

void ControlPlugin::loop() {
    const Snapshot s = controller_.snapshot();

    if (s.autotuneSeq != autotuneSeq_) { // autotune finished: keep the new gains
        autotuneSeq_ = s.autotuneSeq;
        settings_.gains = s.gains;
        ConfigStore::saveGains(s.gains);
    }

    // Quiet time = since the last message Croaster's command handler accepted
    // (WebSocket or BLE, any command) or since the watchdog was (re)armed.
    const unsigned long now = millis();
    const unsigned long sinceCommand = now - app_->commands().lastCommandMillis();
    const unsigned long sinceArmed = now - watchdogArmedMs_;
    const unsigned long quietMs = sinceCommand < sinceArmed ? sinceCommand : sinceArmed;
    const bool heating = s.heaterPct > 0 || s.mode == Mode::Pid || s.mode == Mode::Autotune;
    if (settings_.watchdog.enabled && heating && s.mode != Mode::Profile && s.trip == SafetyTrip::None &&
        quietMs > settings_.watchdog.timeoutS * 1000UL) {
        Command trip{Command::Kind::Trip};
        trip.a = static_cast<float>(SafetyTrip::CommsLost);
        controller_.submit(trip);
        if (settings_.cooldownFan >= 0) { // yaeger: client gone -> heater 0, fan 100
            Command fan{Command::Kind::SetFan};
            fan.a = settings_.cooldownFan;
            controller_.submit(fan);
        }
        watchdogArmedMs_ = now; // one trip per timeout, not one per loop
    }
}
