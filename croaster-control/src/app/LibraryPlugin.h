#pragma once
// Profile library commands for Croaster PID, registered with Croaster's public
// onCommand() hook (no change to croaster/). Profiles live in LittleFS on the
// "spiffs" partition (library_storage.h); the format and the rules are in
// core/profile_library.h. Built when CPID_ENABLE_LIBRARY is 1.
//
//   {"command":"libraryList"}
//       -> {"command":"libraryList","response":{"ok":true,"version":1,"max":50,"maxBytes":8192,
//           "usedBytes":..,"totalBytes":..,"profiles":[{"id","name","unit","steps","recipeSteps","duration","bytes","valid"}]}}
//   {"command":"libraryGet","id":"x"}            -> response {"ok":true,"id":"x","profile":{file as stored}}
//   {"command":"librarySave","profile":{...},"id":"x"?,"overwrite":bool?}
//                                                 -> response {"ok":true,"id":"x","created":bool}
//   {"command":"libraryDelete","id":"x"}         -> response {"ok":true,"id":"x"}
//   {"command":"libraryLoad","id":"x"}
//       -> response {"ok":true,"id":"x","loaded":bool,"steps":n,"recipe":bool}
//       Loads the "steps" schedule like {"command":{"profile":[...]}} ("startProfile"
//       runs it) and the "recipe" into the recipe runner ("recipeStart" runs it).
//       Refused with "busy" while a recipe runs.
// Errors: response {"ok":false,"error":code,...} with the codes of proflib::errorCode();
// "busy" adds "reason". Save and delete are refused while the heater is on or a
// profile or autotune runs (flash writes pause both cores); reads always work.
#if defined(CPID_ENABLE_LIBRARY) && CPID_ENABLE_LIBRARY
#    include <Arduino.h>
#    include <ArduinoJson.h>

#    include "controller.h"
#    include "library_storage.h"

class CroasterApp;
class RecipePlugin;

class LibraryPlugin {
public:
    explicit LibraryPlugin(Controller& controller)
        : controller_(controller)
        , library_(storage_) {}

    // Mounts LittleFS and indexes the profiles; logs the time it took. Call in
    // setup() once the outputs are safe.
    void begin();
    // Registers the commands. Call before app.begin().
    void attach(CroasterApp& app);
    // Where libraryLoad sends a profile's recipe.
    void setRecipes(RecipePlugin* recipes) {
        recipes_ = recipes;
    }

private:
    String list();
    String get(const JsonObject& json);
    String save(const JsonObject& json);
    String remove(const JsonObject& json);
    String load(const JsonObject& json);

    Controller& controller_;
    LittleFsLibraryStorage storage_;
    proflib::Library library_;
    RecipePlugin* recipes_ = nullptr;
};
#endif
