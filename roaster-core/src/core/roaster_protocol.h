#pragma once
// Roaster commands carried by Croaster's command system (CroasterCommandHandler).
// Croaster routes {"command":"name"} to onCommand("name") and
// {"command":{"key":...}} to onJsonCommand("key"); this module turns those JSON
// objects into control Commands and formats the roaster's extra data fields.
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
//   {"command":{"profile":[{"duration":s,"setpoint":T,"interpolation":"ease-out","fanValue":%},...]}}
// Basic:
//   {"command":"startProfile"} {"command":"stop"} {"command":"autotune"}
//   {"command":"clearTrip"}    {"command":"getRoaster"}
// Several keys may be combined in one nested object.
#include <ArduinoJson.h>

#include <cstddef>

#include "roast_profile.h"
#include "roaster_types.h"

namespace roasterproto {

// Keys handled by parseNested(); register each one with onJsonCommand().
extern const char* const kNestedKeys[];
extern const size_t kNestedKeyCount;
// Names handled by basicCommand(); register each one with onCommand().
extern const char* const kBasicNames[];
extern const size_t kBasicNameCount;

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
};

// `cmd` is the object under "command"; `unit` is Croaster's unit ('C','F','K').
// With `onlyKey`, parses just that key: Croaster calls each onJsonCommand(key)
// callback with the whole object, so each callback handles its own key once.
Parsed parseNested(JsonObjectConst cmd, char unit, const char* onlyKey = nullptr);

// Maps a basic command name to a control command. "getRoaster" returns false
// (it is a query, answered with writeRoasterInfo()).
bool basicCommand(const char* name, Command& out);

// Adds BurnerVal, FanVal, SV (setpoint), Mode, trip to Croaster's "data" object.
void writeDataFields(const Snapshot& s, char unit, JsonObject data);

// Response body for {"command":"getRoaster"}.
void writeRoasterInfo(const Snapshot& s, float cooldownFan, char unit, JsonObject out);

} // namespace roasterproto
