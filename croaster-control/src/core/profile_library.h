#pragma once
// On-device profile library: named roast profiles kept as JSON files, one per
// profile, so the page can load one to the board or import and export them.
// Pure logic (ArduinoJson and the standard library); the files go through the
// LibraryStorage interface, LittleFS on the board (app/library_storage.h) and
// a fake in the host tests.
//
// File format v1 ("version" is required; a higher version is refused):
//   {"version":1,"name":"Light city","notes":"...","unit":"F",
//    "steps":[{"duration":s,"setpoint":T,"interpolation":"linear","fanValue":%},...],
//    "recipe":{"mode":"auto","steps":[{"trigger":{...},"actions":[...]},...]}}
// "steps" is the timed setpoint schedule (RoastProfile::loadJson, run by the
// Profile mode); "recipe" is the trigger-driven step list (core/recipe.h).
// Either may be left out, not both. Temperatures are in "unit" and are
// converted when the profile is loaded. Keys this version does not know are
// kept in the file as written; the board ignores them.
#include <ArduinoJson.h>

#include <cstddef>
#include <string>
#include <vector>

#include "control_types.h"
#include "recipe.h"
#include "roast_profile.h"

namespace proflib {

constexpr int kFormatVersion = 1;
constexpr size_t kMaxProfiles = 50;
constexpr size_t kMaxFileBytes = 8192; // serialized profile
constexpr size_t kMaxNameChars = 40;   // Unicode code points
constexpr size_t kMaxNotesChars = 1024;
constexpr size_t kMaxIdLen = 32; // [a-z0-9_-]

enum class Error {
    None,
    BadRequest,   // missing or malformed command fields
    BadId,        // id not 1..kMaxIdLen of a-z 0-9 _ -
    BadVersion,   // version missing or not a positive integer
    NewerVersion, // written by a newer firmware
    BadName,      // missing, empty, too long or with control characters
    BadNotes,     // not a string or too long
    BadUnit,      // not "C", "F" or "K"
    BadSteps,     // RoastProfile::loadJson would refuse them, or neither steps nor recipe
    BadRecipe,    // recipe::parse refuses it (reason says why)
    TooLarge,     // over kMaxFileBytes
    NotFound,
    Exists, // id taken and overwrite not set
    Full,   // kMaxProfiles reached
    Busy,   // heater on, profile or autotune running (busyReason)
    Storage,    // file system read or write failed
    Unavailable // file system not mounted
};
// Short code for replies: "badId", "notFound", ...
const char* errorCode(Error e);

bool validId(const char* id);
// Number of Unicode code points in UTF-8 text.
size_t utf8Length(const char* s);
// Checks a profile object against format v1. For BadRecipe, `reason` (when
// given) gets recipe::parse's reason.
Error validate(JsonObjectConst profile, const char** reason = nullptr);

// What a loaded profile held.
struct Loaded {
    bool schedule = false; // "steps" loaded into the RoastProfile
    bool recipe = false;   // "recipe" parsed into the Recipe
};
// Validates `profile` and converts its parts for the controller and the recipe
// runner (both in C).
Error loadParts(JsonObjectConst profile, RoastProfile& schedule, recipe::Recipe& rec, Loaded& what,
                const char** reason = nullptr);
// Lower-case id from a name: letters and digits kept, everything else one '-',
// at most 24 characters; "profile" when nothing is left.
std::string slug(const char* name);
// Why the library may not write now (flash writes pause both cores for tens of
// ms): "heater on", "profile running", "autotune running"; nullptr when idle.
const char* busyReason(const Snapshot& s);

struct Meta {
    std::string id;
    std::string name;
    char unit = 'C';
    size_t steps = 0;       // schedule steps
    size_t recipeSteps = 0; // recipe steps
    float durationS = 0;
    size_t bytes = 0;
    bool valid = true; // false: the file does not pass validate() (it can still be read and deleted)
};

// File access, one flat directory. Names are file names inside it ("x.json").
class LibraryStorage {
public:
    virtual ~LibraryStorage() = default;
    virtual bool list(std::vector<std::string>& names) = 0;
    virtual bool read(const std::string& name, std::string& out) = 0;
    // Creates or truncates.
    virtual bool write(const std::string& name, const std::string& data) = 0;
    // Replaces `to` if it exists, in one step (power-loss safe on LittleFS).
    virtual bool rename(const std::string& from, const std::string& to) = 0;
    virtual bool remove(const std::string& name) = 0;
};

class Library {
public:
    explicit Library(LibraryStorage& fs)
        : fs_(fs) {}

    // Deletes leftover temporary files from an interrupted save and indexes the
    // profiles. False if the directory cannot be listed.
    bool begin();
    bool ready() const {
        return ready_;
    }
    // Sorted by name (case-insensitive), then id.
    const std::vector<Meta>& list() const {
        return index_;
    }
    // The stored file text, as written.
    Error get(const char* id, std::string& out);
    // Validates and writes `profile` as is. Without an id, one is made from the
    // name. An existing id needs `overwrite`. Writes a temporary file and renames
    // it over the old one, so a power cut leaves either the old or the new file.
    Error save(const char* id, bool overwrite, JsonObjectConst profile, std::string& idOut, bool& created);
    Error remove(const char* id);
    // Reads, validates and loads the steps into `out` (setpoints converted to C).
    Error load(const char* id, RoastProfile& out);
    // Reads and loads both parts (loadParts); `recipeText` (when given) gets the
    // serialized "recipe" object, empty when there is none.
    Error load(const char* id, RoastProfile& schedule, recipe::Recipe& rec, Loaded& what,
               std::string* recipeText = nullptr, const char** reason = nullptr);

private:
    const Meta* find(const std::string& id) const;
    void put(Meta m);
    std::string uniqueId(const std::string& base) const;
    static Meta describe(const std::string& id, JsonObjectConst p, size_t bytes);

    LibraryStorage& fs_;
    std::vector<Meta> index_;
    bool ready_ = false;
};

} // namespace proflib
