#if defined(CPID_ENABLE_LIBRARY) && CPID_ENABLE_LIBRARY
#    include "LibraryPlugin.h"

#    include <CroasterApp.h>

#    include "RecipePlugin.h"

#    include <cmath>
#    include <memory>

namespace {
// Same envelope as Croaster's genResponseCommand(): {"command":name,"response":{...}}.
// The response is written straight into the envelope, never copied from another
// document (T54: document copies recurse on the loop task stack).
JsonObject beginReply(JsonDocument& doc, const char* command) {
    doc["command"] = command;
    return doc["response"].to<JsonObject>();
}
String finishReply(const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    return out;
}
String failure(const char* command, proflib::Error e, const char* reason = nullptr) {
    JsonDocument doc;
    JsonObject r = beginReply(doc, command);
    r["ok"] = false;
    r["error"] = proflib::errorCode(e);
    if (reason)
        r["reason"] = reason;
    return finishReply(doc);
}
const char* idOf(const JsonObject& json) {
    return json["id"].is<const char*>() ? json["id"].as<const char*>() : "";
}
} // namespace

void LibraryPlugin::begin() {
    const uint32_t t0 = millis();
    const bool ok = storage_.mount() && library_.begin();
    const uint32_t ms = millis() - t0;
    if (ok)
        Serial.printf("# Library: LittleFS on \"%s\" in %u ms, %u of %u KB used, %u profiles\n",
                      LittleFsLibraryStorage::kPartition, (unsigned)ms, (unsigned)(storage_.usedBytes() / 1024),
                      (unsigned)(storage_.totalBytes() / 1024), (unsigned)library_.list().size());
    else
        Serial.printf("# Library: unavailable (LittleFS on \"%s\" did not mount, %u ms)\n",
                      LittleFsLibraryStorage::kPartition, (unsigned)ms);
}

void LibraryPlugin::attach(CroasterApp& app) {
    CroasterCommandHandler& c = app.commands();
    c.onCommand("libraryList", [this](const JsonObject&) { return list(); });
    c.onCommand("libraryGet", [this](const JsonObject& json) { return get(json); });
    c.onCommand("librarySave", [this](const JsonObject& json) { return save(json); });
    c.onCommand("libraryDelete", [this](const JsonObject& json) { return remove(json); });
    c.onCommand("libraryLoad", [this](const JsonObject& json) { return load(json); });
}

String LibraryPlugin::list() {
    if (!library_.ready())
        return failure("libraryList", proflib::Error::Unavailable);
    JsonDocument doc;
    JsonObject r = beginReply(doc, "libraryList");
    r["ok"] = true;
    r["version"] = proflib::kFormatVersion;
    r["max"] = proflib::kMaxProfiles;
    r["maxBytes"] = proflib::kMaxFileBytes;
    r["usedBytes"] = storage_.usedBytes();
    r["totalBytes"] = storage_.totalBytes();
    JsonArray a = r["profiles"].to<JsonArray>();
    for (const proflib::Meta& m : library_.list()) {
        JsonObject o = a.add<JsonObject>();
        o["id"] = m.id.c_str(); // copied: the index may change before the reply is sent
        o["name"] = m.name.c_str();
        const char unit[2] = {m.unit, '\0'};
        o["unit"] = unit;
        o["steps"] = m.steps;
        o["recipeSteps"] = m.recipeSteps;
        o["duration"] = std::lround(m.durationS);
        o["bytes"] = m.bytes;
        o["valid"] = m.valid;
    }
    return finishReply(doc);
}

String LibraryPlugin::get(const JsonObject& json) {
    std::string text;
    const char* id = idOf(json);
    const proflib::Error e = library_.get(id, text);
    if (e != proflib::Error::None)
        return failure("libraryGet", e);
    // The file was validated when it was saved: splice it in as text instead of
    // parsing it into a document. The id is [a-z0-9_-], so it needs no escaping.
    String out;
    out.reserve(text.size() + 80);
    out += "{\"command\":\"libraryGet\",\"response\":{\"ok\":true,\"id\":\"";
    out += id;
    out += "\",\"profile\":";
    out += text.c_str();
    out += "}}";
    return out;
}

String LibraryPlugin::save(const JsonObject& json) {
    if (const char* busy = proflib::busyReason(controller_.snapshot()))
        return failure("librarySave", proflib::Error::Busy, busy);
    if (!json["profile"].is<JsonObject>())
        return failure("librarySave", proflib::Error::BadRequest);
    std::string id;
    bool created = false;
    const proflib::Error e =
        library_.save(idOf(json), json["overwrite"] | false, json["profile"].as<JsonObjectConst>(), id, created);
    if (e != proflib::Error::None) {
        const char* reason = nullptr;
        if (e == proflib::Error::BadRecipe)
            proflib::validate(json["profile"].as<JsonObjectConst>(), &reason);
        return failure("librarySave", e, reason);
    }
    JsonDocument doc;
    JsonObject r = beginReply(doc, "librarySave");
    r["ok"] = true;
    r["id"] = id.c_str();
    r["created"] = created;
    return finishReply(doc);
}

String LibraryPlugin::remove(const JsonObject& json) {
    if (const char* busy = proflib::busyReason(controller_.snapshot()))
        return failure("libraryDelete", proflib::Error::Busy, busy);
    const char* id = idOf(json);
    const proflib::Error e = library_.remove(id);
    if (e != proflib::Error::None)
        return failure("libraryDelete", e);
    JsonDocument doc;
    JsonObject r = beginReply(doc, "libraryDelete");
    r["ok"] = true;
    r["id"] = id;
    return finishReply(doc);
}

String LibraryPlugin::load(const JsonObject& json) {
    if (recipes_ && recipes_->running())
        return failure("libraryLoad", proflib::Error::Busy, "recipe running");
    const char* id = idOf(json);
    // About 520 B and 2.3 KB: on the heap, not the loop stack.
    std::unique_ptr<RoastProfile> p(new RoastProfile());
    std::unique_ptr<recipe::Recipe> rec(new recipe::Recipe());
    proflib::Loaded what;
    std::string text;
    const char* reason = nullptr;
    const proflib::Error e = library_.load(id, *p, *rec, what, &text, &reason);
    if (e != proflib::Error::None)
        return failure("libraryLoad", e, reason);
    const proflib::Meta* m = nullptr;
    for (const proflib::Meta& x : library_.list())
        if (x.id == id)
            m = &x;
    JsonDocument doc;
    JsonObject r = beginReply(doc, "libraryLoad");
    r["ok"] = true;
    r["id"] = id;
    r["loaded"] = what.schedule && controller_.loadProfile(*p);
    r["steps"] = what.schedule ? p->size() : 0;
    r["recipe"] = what.recipe && recipes_ && recipes_->load(*rec, text, m ? m->name.c_str() : id, m ? m->unit : 'C');
    return finishReply(doc);
}
#endif
