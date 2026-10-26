#include "RoasterPlugin.h"

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
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

char RoasterPlugin::unit() const {
    const String u = app_ ? app_->core().temperatureUnit() : String("C");
    return u.length() ? u[0] : 'C';
}

void RoasterPlugin::attach(CroasterApp& app, const RoasterSettings& settings) {
    app_ = &app;
    settings_ = settings;
    for (size_t i = 0; i < roasterproto::kNestedKeyCount; i++) {
        const char* key = roasterproto::kNestedKeys[i];
        app.commands().onJsonCommand(key, [this, key](const JsonObject& json) {
            return handleNested(json, key);
        });
    }
    for (size_t i = 0; i < roasterproto::kBasicNameCount; i++) {
        const char* name = roasterproto::kBasicNames[i];
        app.commands().onCommand(name, [this, name](const JsonObject&) {
            return handleBasic(name);
        });
    }
    for (size_t i = 0; i < roasterproto::kLegacyBasicNameCount; i++) {
        const char* name = roasterproto::kLegacyBasicNames[i];
        app.commands().onCommand(name, [this, name](const JsonObject& json) {
            return handleLegacy(json, name);
        });
    }
    // yaeger's flat keys ({"BurnerVal":50}, {"Mode":"PID"}, ...) carry no "command".
    app.commands().onOtherJson([this](const JsonObject& json) {
        return handleLegacy(json, nullptr);
    });
    // Same payload as Croaster's built-in getArtisanData, but counts as client
    // activity for the watchdog. The supplied Artisan .aset polls with this.
    app.commands().onCommand("getData", [this](const JsonObject& json) {
        touch();
        return app_->core().getJsonData(json["id"] | 0);
    });
    app.core().onJsonData([this](JsonObject& data) {
        roasterproto::writeDataFields(roaster_.snapshot(), unit(), data);
        if (WiFi.status() == WL_CONNECTED)
            data["wifiStrength"] = WiFi.RSSI();
    });
}

String RoasterPlugin::handleNested(const JsonObject& json, const char* key) {
    if (json[key].isNull())
        return "";
    touch();
    roasterproto::Parsed p = roasterproto::parseNested(json, unit(), key);
    apply(p);
    if (p.hasProfile || p.profileError) {
        JsonDocument r;
        r["loaded"] = p.hasProfile && roaster_.loadProfile(p.profile);
        r["steps"] = p.profile.size();
        return reply("profile", r);
    }
    return "";
}

String RoasterPlugin::handleLegacy(const JsonObject& json, const char* name) {
    roasterproto::Parsed p = roasterproto::parseLegacy(json, unit());
    if (p.count == 0 && !p.hasCooldownFan && !name)
        return ""; // unrelated message without "command"
    touch();
    apply(p);
    if (!name || std::strcmp(name, "setFan") == 0 || std::strcmp(name, "setBurner") == 0)
        return "";
    JsonDocument r; // yaeger answers set/getPreferences with the stored values
    r["id"] = json["id"] | 0;
    roasterproto::writePreferences(settings_.gains, settings_.cooldownFan, r["data"].to<JsonObject>());
    String out;
    serializeJson(r, out);
    return out;
}

void RoasterPlugin::apply(const roasterproto::Parsed& p) {
    for (size_t i = 0; i < p.count; i++)
        roaster_.submit(p.commands[i]);
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
}

String RoasterPlugin::handleBasic(const char* name) {
    touch();
    Command c;
    if (roasterproto::basicCommand(name, c)) {
        roaster_.submit(c);
        if (c.kind == Command::Kind::Stop && settings_.cooldownFan >= 0) {
            Command fan{Command::Kind::SetFan};
            fan.a = settings_.cooldownFan;
            roaster_.submit(fan);
        }
        return "";
    }
    JsonDocument r; // getRoaster
    roasterproto::writeRoasterInfo(roaster_.snapshot(), settings_.cooldownFan, unit(), r.to<JsonObject>());
    return reply(name, r);
}

void RoasterPlugin::begin() {
    autotuneSeq_ = roaster_.snapshot().autotuneSeq;
    touch();
    if (!cfg_.arduinoOta)
        return;
    ArduinoOTA.setHostname(cfg_.otaHostname);
    if (cfg_.otaPassword && *cfg_.otaPassword)
        ArduinoOTA.setPassword(cfg_.otaPassword);
    ArduinoOTA.onStart([this]() {
        Command trip{Command::Kind::Trip};
        trip.a = static_cast<float>(SafetyTrip::Firmware);
        roaster_.submit(trip);
    });
    ArduinoOTA.begin();
}

void RoasterPlugin::loop() {
    if (cfg_.arduinoOta)
        ArduinoOTA.handle();
    const Snapshot s = roaster_.snapshot();

    if (s.autotuneSeq != autotuneSeq_) { // autotune finished: keep the new gains
        autotuneSeq_ = s.autotuneSeq;
        settings_.gains = s.gains;
        ConfigStore::saveGains(s.gains);
    }

    const bool heating = s.heaterPct > 0 || s.mode == Mode::Pid || s.mode == Mode::Autotune;
    if (cfg_.commsTimeoutMs && heating && s.mode != Mode::Profile && s.trip == SafetyTrip::None &&
        millis() - lastActivityMs_ > cfg_.commsTimeoutMs) {
        Command trip{Command::Kind::Trip};
        trip.a = static_cast<float>(SafetyTrip::CommsLost);
        roaster_.submit(trip);
        if (settings_.cooldownFan >= 0) { // yaeger: client gone -> heater 0, fan 100
            Command fan{Command::Kind::SetFan};
            fan.a = settings_.cooldownFan;
            roaster_.submit(fan);
        }
        touch(); // one trip per timeout, not one per loop
    }
}
