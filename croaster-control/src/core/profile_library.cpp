#include "profile_library.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace proflib {

namespace {
const char* const kJson = ".json";
const char* const kTmp = ".tmp";

bool endsWith(const std::string& s, const char* tail) {
    const size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}
char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
}
bool lessByName(const Meta& a, const Meta& b) {
    const size_t n = std::min(a.name.size(), b.name.size());
    for (size_t i = 0; i < n; i++) {
        const char x = lower(a.name[i]), y = lower(b.name[i]);
        if (x != y)
            return x < y;
    }
    if (a.name.size() != b.name.size())
        return a.name.size() < b.name.size();
    return a.id < b.id;
}
bool hasControlChar(const char* s) {
    for (; *s; s++)
        if (static_cast<unsigned char>(*s) < 0x20)
            return true;
    return false;
}
bool hasVisibleChar(const char* s) {
    for (; *s; s++)
        if (*s != ' ')
            return true;
    return false;
}
} // namespace

const char* errorCode(Error e) {
    switch (e) {
    case Error::None:
        return "";
    case Error::BadRequest:
        return "badRequest";
    case Error::BadId:
        return "badId";
    case Error::BadVersion:
        return "badVersion";
    case Error::NewerVersion:
        return "newerVersion";
    case Error::BadName:
        return "badName";
    case Error::BadNotes:
        return "badNotes";
    case Error::BadUnit:
        return "badUnit";
    case Error::BadSteps:
        return "badSteps";
    case Error::BadRecipe:
        return "badRecipe";
    case Error::TooLarge:
        return "tooLarge";
    case Error::NotFound:
        return "notFound";
    case Error::Exists:
        return "exists";
    case Error::Full:
        return "full";
    case Error::Busy:
        return "busy";
    case Error::Storage:
        return "storage";
    case Error::Unavailable:
    default:
        return "unavailable";
    }
}

bool validId(const char* id) {
    if (!id || !*id)
        return false;
    size_t n = 0;
    for (const char* c = id; *c; c++, n++) {
        const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_';
        if (!ok || n >= kMaxIdLen)
            return false;
    }
    return true;
}

size_t utf8Length(const char* s) {
    size_t n = 0;
    for (; *s; s++)
        if ((static_cast<unsigned char>(*s) & 0xC0) != 0x80)
            n++;
    return n;
}

Error validate(JsonObjectConst p, const char** reason) {
    if (p.isNull())
        return Error::BadRequest;
    JsonVariantConst v = p["version"];
    if (!v.is<int>() || v.as<int>() < 1) // 1.5 or "1" fail is<int>()
        return Error::BadVersion;
    if (v.as<int>() > kFormatVersion)
        return Error::NewerVersion;
    if (!p["name"].is<const char*>())
        return Error::BadName;
    const char* name = p["name"];
    const size_t nameLen = utf8Length(name);
    if (nameLen == 0 || nameLen > kMaxNameChars || hasControlChar(name) || !hasVisibleChar(name))
        return Error::BadName;
    JsonVariantConst notes = p["notes"];
    if (!notes.isNull() && (!notes.is<const char*>() || utf8Length(notes.as<const char*>()) > kMaxNotesChars))
        return Error::BadNotes;
    const char* unit = p["unit"].is<const char*>() ? p["unit"].as<const char*>() : "";
    if (!((unit[0] == 'C' || unit[0] == 'F' || unit[0] == 'K') && unit[1] == '\0'))
        return Error::BadUnit;
    JsonVariantConst steps = p["steps"], rec = p["recipe"];
    if (steps.isNull() && rec.isNull())
        return Error::BadSteps;
    if (!steps.isNull() && !RoastProfile::validJson(steps.as<JsonArrayConst>(), unit[0]))
        return Error::BadSteps;
    if (!rec.isNull()) {
        std::unique_ptr<recipe::Recipe> r(new recipe::Recipe()); // 2 KB: off the loop stack
        const char* why = recipe::parse(rec.as<JsonObjectConst>(), unit[0], *r);
        if (*why) {
            if (reason)
                *reason = why;
            return Error::BadRecipe;
        }
    }
    return Error::None;
}

Error loadParts(JsonObjectConst p, RoastProfile& schedule, recipe::Recipe& rec, Loaded& what, const char** reason) {
    what = Loaded();
    const Error v = validate(p, reason);
    if (v != Error::None)
        return v;
    const char unit = p["unit"].as<const char*>()[0];
    if (!p["steps"].isNull()) {
        if (!schedule.loadJson(p["steps"].as<JsonArrayConst>(), unit))
            return Error::BadSteps;
        what.schedule = true;
    }
    if (!p["recipe"].isNull()) {
        const char* why = recipe::parse(p["recipe"].as<JsonObjectConst>(), unit, rec);
        if (*why) {
            if (reason)
                *reason = why;
            return Error::BadRecipe;
        }
        what.recipe = true;
    }
    return Error::None;
}

std::string slug(const char* name) {
    std::string out;
    bool dash = false;
    for (const char* c = name ? name : ""; *c && out.size() < 24; c++) {
        const char l = lower(*c);
        if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) {
            if (dash && !out.empty())
                out += '-';
            out += l;
            dash = false;
        } else {
            dash = true;
        }
    }
    return out.empty() ? std::string("profile") : out;
}

const char* busyReason(const Snapshot& s) {
    if (s.mode == Mode::Profile)
        return "profile running";
    if (s.mode == Mode::Autotune)
        return "autotune running";
    if (s.heaterPct > 0)
        return "heater on";
    return nullptr;
}

Meta Library::describe(const std::string& id, JsonObjectConst p, size_t bytes) {
    Meta m;
    m.id = id;
    m.bytes = bytes;
    m.valid = validate(p) == Error::None;
    m.name = p["name"].is<const char*>() ? p["name"].as<const char*>() : id.c_str();
    const char* unit = p["unit"].is<const char*>() ? p["unit"].as<const char*>() : "C";
    m.unit = unit[0] ? unit[0] : 'C';
    JsonArrayConst steps = p["steps"].as<JsonArrayConst>();
    m.steps = steps.size();
    for (JsonVariantConst s : steps)
        m.durationS += s["duration"] | 0.0f;
    m.recipeSteps = p["recipe"]["steps"].as<JsonArrayConst>().size();
    return m;
}

const Meta* Library::find(const std::string& id) const {
    for (const Meta& m : index_)
        if (m.id == id)
            return &m;
    return nullptr;
}

void Library::put(Meta m) {
    for (auto it = index_.begin(); it != index_.end(); ++it)
        if (it->id == m.id) {
            index_.erase(it);
            break;
        }
    index_.insert(std::upper_bound(index_.begin(), index_.end(), m, lessByName), std::move(m));
}

std::string Library::uniqueId(const std::string& base) const {
    if (!find(base))
        return base;
    for (unsigned n = 2;; n++) {
        const std::string id = base + "-" + std::to_string(n);
        if (!find(id))
            return id;
    }
}

bool Library::begin() {
    index_.clear();
    ready_ = false;
    std::vector<std::string> names;
    if (!fs_.list(names))
        return false;
    std::string text;
    for (const std::string& name : names) {
        if (endsWith(name, kTmp)) { // an interrupted save; the .json beside it is the last good copy
            fs_.remove(name);
            continue;
        }
        if (!endsWith(name, kJson))
            continue;
        const std::string id = name.substr(0, name.size() - std::strlen(kJson));
        if (!validId(id.c_str()) || index_.size() >= kMaxProfiles || !fs_.read(name, text))
            continue;
        JsonDocument doc;
        if (deserializeJson(doc, text) || !doc.is<JsonObject>()) {
            Meta m;
            m.id = id;
            m.name = id;
            m.bytes = text.size();
            m.valid = false;
            put(std::move(m));
            continue;
        }
        put(describe(id, doc.as<JsonObjectConst>(), text.size()));
    }
    ready_ = true;
    return true;
}

Error Library::get(const char* id, std::string& out) {
    if (!ready_)
        return Error::Unavailable;
    if (!validId(id))
        return Error::BadId;
    if (!find(id))
        return Error::NotFound;
    return fs_.read(std::string(id) + kJson, out) ? Error::None : Error::Storage;
}

Error Library::save(const char* reqId, bool overwrite, JsonObjectConst p, std::string& idOut, bool& created) {
    created = false;
    if (!ready_)
        return Error::Unavailable;
    const Error e = validate(p);
    if (e != Error::None)
        return e;
    const size_t bytes = measureJson(p);
    if (bytes > kMaxFileBytes)
        return Error::TooLarge;
    std::string id;
    if (reqId && *reqId) {
        if (!validId(reqId))
            return Error::BadId;
        id = reqId;
    } else {
        id = uniqueId(slug(p["name"]));
    }
    const bool exists = find(id) != nullptr;
    if (exists && !overwrite)
        return Error::Exists;
    if (!exists && index_.size() >= kMaxProfiles)
        return Error::Full;

    std::string text;
    text.reserve(bytes);
    serializeJson(p, text);
    const std::string tmp = id + kTmp, file = id + kJson;
    if (!fs_.write(tmp, text) || !fs_.rename(tmp, file)) {
        fs_.remove(tmp);
        return Error::Storage;
    }
    put(describe(id, p, text.size()));
    idOut = id;
    created = !exists;
    return Error::None;
}

Error Library::remove(const char* id) {
    if (!ready_)
        return Error::Unavailable;
    if (!validId(id))
        return Error::BadId;
    if (!find(id))
        return Error::NotFound;
    if (!fs_.remove(std::string(id) + kJson))
        return Error::Storage;
    for (auto it = index_.begin(); it != index_.end(); ++it)
        if (it->id == id) {
            index_.erase(it);
            break;
        }
    return Error::None;
}

Error Library::load(const char* id, RoastProfile& out) {
    std::string text;
    const Error e = get(id, text);
    if (e != Error::None)
        return e;
    JsonDocument doc;
    if (deserializeJson(doc, text) || !doc.is<JsonObject>())
        return Error::Storage; // unreadable file
    JsonObjectConst p = doc.as<JsonObjectConst>();
    const Error v = validate(p);
    if (v != Error::None)
        return v;
    if (p["steps"].isNull())
        return Error::BadSteps; // a recipe-only profile has no schedule to load
    return out.loadJson(p["steps"].as<JsonArrayConst>(), p["unit"].as<const char*>()[0]) ? Error::None
                                                                                         : Error::BadSteps;
}

Error Library::load(const char* id, RoastProfile& schedule, recipe::Recipe& rec, Loaded& what,
                    std::string* recipeText, const char** reason) {
    what = Loaded();
    std::string text;
    const Error e = get(id, text);
    if (e != Error::None)
        return e;
    JsonDocument doc;
    if (deserializeJson(doc, text) || !doc.is<JsonObject>())
        return Error::Storage; // unreadable file
    JsonObjectConst p = doc.as<JsonObjectConst>();
    const Error v = loadParts(p, schedule, rec, what, reason);
    if (v == Error::None && recipeText) {
        recipeText->clear();
        if (what.recipe)
            serializeJson(p["recipe"], *recipeText);
    }
    return v;
}

} // namespace proflib
