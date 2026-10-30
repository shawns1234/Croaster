#pragma once
// Runs recipes (core/recipe.h) on Croaster PID: steps that fire on time since
// CHARGE, BT, ET, BT RoR or a roast event and set the heater power, fan or
// setpoint. Ticked from the loop task (loop()), on the readings Croaster shows
// (CroasterCore tempBt/tempEt/rorBt, correction included). Every action is an
// ordinary controller command, so all interlocks apply. Aborted by a safety
// trip (comms watchdog included), the stop commands and an OTA update.
// Registered with Croaster's public onCommand() hook (no change to croaster/).
//
//   {"command":"roastEvent","event":"CHARGE","undo":bool?}
//       Events: CHARGE TP DRY_END FC_START FC_END SC_START SC_END DROP. CHARGE
//       starts the roast clock (time triggers) and clears the other events.
//       -> response {"ok":true,"event":"CHARGE","sinceCharge":s|null}
//   {"command":"recipeLoad","profile":{library profile, format v1}}
//       Loads its "recipe" (and its "steps" schedule, like libraryLoad).
//       -> response {"ok":true,"recipe":bool,"schedule":bool}
//   {"command":"recipeStart"} / {"command":"recipeAbort"}
//   {"command":"recipeConfirm","step":i}   guided mode: apply the prompted step
//   {"command":"recipeSkip","step":i?}     dismiss the prompt, or skip the next step
//       -> response {"ok":bool,"error"?:code} (then getRecipe for the detail)
//   {"command":"getRecipe"}
//       -> response {"ok":true,"state","reason","name","unit","mode","next","prompt",
//          "follow","sinceCharge","events":{"CHARGE":0,...},
//          "fired":[{"step","at","how"}],"recipe":{as loaded}}
// Every data payload gets "recipe":{"state","next","prompt","fired","seq","follow"}
// once a recipe is loaded; the page fetches getRecipe when "seq" changes.
#include <Arduino.h>
#include <ArduinoJson.h>

#include <memory>
#include <string>

#include "ControlPlugin.h"
#include "controller.h"
#include "core/profile_library.h"
#include "core/recipe.h"

class CroasterApp;

class RecipePlugin {
public:
    static constexpr uint32_t kTickMs = 250;

    RecipePlugin(Controller& controller, ControlPlugin& control)
        : controller_(controller)
        , control_(control) {}

    // Registers the commands, the stop hook and the data fields. Call before app.begin().
    void attach(CroasterApp& app);
    // Call from loop().
    void loop();

    // Replaces the loaded recipe (already parsed, in C). `text` is its JSON as
    // written, `unit` the file's unit ('C', 'F' or 'K'). Returns nullptr when
    // loaded, else the reason it was refused (bad unit, or a recipe running).
    const char* load(const recipe::Recipe& r, const std::string& text, const char* name, char unit);
    bool running() const {
        return runner_.state() == recipe::State::Running;
    }

private:
    recipe::Inputs inputs() const;
    void submit(const recipe::Output& out);
    String event(const JsonObject& json);
    String loadCommand(const JsonObject& json);
    String simple(const char* command, bool ok, const char* error = nullptr);
    String status();
    void writeData(JsonObject& data);

    Controller& controller_;
    ControlPlugin& control_;
    CroasterApp* app_ = nullptr;
    recipe::Runner runner_;
    recipe::RoastClock clock_;
    std::string text_; // the loaded recipe object, as JSON
    char name_[proflib::kMaxNameChars * 4 + 1] = ""; // UTF-8, owned copy
    char unit_ = 'C';                                // validated by load()
    uint32_t lastTickMs_ = 0;
};
