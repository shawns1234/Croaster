#pragma once
// Control commands carried by Croaster's command system (CroasterCommandHandler).
// Croaster routes {"command":"name"} to onCommand("name") and
// {"command":{"key":...}} to onJsonCommand("key"); this module turns those JSON
// objects into control Commands and formats the control data fields.
// Pure (ArduinoJson only), so it is host-tested.
//
// Nested (values; temperatures in Croaster's current unit, see "temp" in data):
//   {"command":{"burner":0..100}}           heater % (manual mode)
//   {"command":{"fan":0..100}}              fan %
//   {"command":{"setpoint":T}}              PID setpoint
//   {"command":{"mode":"manual"|"pid"}}
//   {"command":{"pidInput":"ET"|"BT"|"MAX"}}
//   {"command":{"pid":{"kp":..,"ki":..,"kd":..}}}       saved to NVS
//   {"command":{"cooldownFan":0..100}}                  saved to NVS
//   {"command":{"watchdog":{"enabled":bool,"timeout":s}}} saved to NVS; either
//                                            field alone works; timeout 2..600 s
//   {"command":{"profile":[{"duration":s,"setpoint":T,"interpolation":"ease-out","fanValue":%},...]}}
// Basic:
//   {"command":"startProfile"} {"command":"stop"} {"command":"autotune"}
//   {"command":"clearTrip"}    {"command":"getControl"}
// Several keys may be combined in one nested object.
//
// yaeger compatibility (same wire format as yaeger's CommandLoop.cpp, so its web
// UI and its Artisan settings keep working; see docs/control-parity.md):
//   {"command":"setBurner","value":v}   {"command":"setFan","value":v}
//   {"command":"setPreferences","pidKp":..,"pidKi":..,"pidKd":..,"cooldownFanSpeed":..}
//   {"command":"getPreferences"}  -> {"id":..,"data":{"type":"preferences",...}}
#include <ArduinoJson.h>

#include <cstddef>

#include "control_types.h"
#include "roast_profile.h"

namespace controlproto {

// Keys handled by parseNested(); register each one with onJsonCommand().
extern const char* const kNestedKeys[];
extern const size_t kNestedKeyCount;
// Names handled by basicCommand(); register each one with onCommand().
extern const char* const kBasicNames[];
extern const size_t kBasicNameCount;

// yaeger command names handled by parseLegacy(); register each with onCommand().
extern const char* const kLegacyBasicNames[];
extern const size_t kLegacyBasicNameCount;

struct Parsed {
    static constexpr size_t kMax = 8;
    Command commands[kMax];
    size_t count = 0;
    bool hasProfile = false; // `profile` holds a valid schedule
    bool profileError = false;
    RoastProfile profile;
    bool hasGains = false; // persist these
    PidGains gains;
    bool hasPidInput = false;
    PidInput pidInput = PidInput::ET;
    bool hasCooldownFan = false;
    float cooldownFan = 0;
    bool hasWatchdogEnabled = false; // persist these
    bool watchdogEnabled = true;
    bool hasWatchdogTimeout = false;
    uint16_t watchdogTimeoutS = 0;
};

// `cmd` is the object under "command"; `unit` is Croaster's unit ('C','F','K').
// With `onlyKey`, parses just that key: Croaster calls each onJsonCommand(key)
// callback with the whole object, so each callback handles its own key once.
Parsed parseNested(JsonObjectConst cmd, char unit, const char* onlyKey = nullptr);

// Parses a whole message carrying one of yaeger's named commands
// (kLegacyBasicNames). Ignores everything else, so it is safe on any message.
// yaeger's flat keys ({"BurnerVal":60} without "command") are not supported:
// Croaster's handler only routes messages that have a "command" member (D8).
Parsed parseLegacy(JsonObjectConst msg);

// Maps a basic command name to a control command. "getControl" returns false
// (it is a query, answered with writeControlInfo()).
bool basicCommand(const char* name, Command& out);

// Adds BurnerVal, FanVal, SV (setpoint), Mode, trip and yaeger's Amb, Target,
// pidKp/pidKi/pidKd to Croaster's "data" object.
void writeDataFields(const Snapshot& s, char unit, JsonObject data);

// yaeger's preferences reply body: type, pidKp, pidKi, pidKd, cooldownFanSpeed.
void writePreferences(const PidGains& g, float cooldownFan, JsonObject data);

// Response body for {"command":"getControl"}.
void writeControlInfo(const Snapshot& s, float cooldownFan, const WatchdogSettings& wd, char unit, JsonObject out);

} // namespace controlproto
