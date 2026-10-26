#include <Arduino.h>
#include <Wire.h>

// Everything configurable is in config.h (force-included by platformio.ini).
#include "ConfigCheck.h"

#include <CroasterApp.h>
#include <CroasterDeviceIdentity.h>

class Controller; // ControlEsp32; only defined in the control build

// ---------------------------------------------------------------------------
// Board wiring: build the parts selected in config.h and hand them to
// Croaster through its public extension points (constructor injection,
// CroasterDisplay, onCommand/onJsonCommand, onJsonData). Nothing in croaster/
// knows about heaters or fans.
// ---------------------------------------------------------------------------

#if FLEX_HEATER != FLEX_HEATER_NONE // ===== control build =====
#    include <ControlPlugin.h>
#    include <RoasterProbe.h>
#    include <app/controller.h>
#    include <hw/heater_output.h>
#    include <hw/thermocouples.h>
#    if FLEX_FAN == FLEX_FAN_AC_DIMMER
#        include <hw/fan_dimmer.h>
FanDimmer fan(FLEX_PIN_ZERO_CROSS, FLEX_PIN_FAN, FLEX_MAINS_HZ);
#    else
#        include <hw/fan_pwm.h>
FanPwm fan(FLEX_PIN_FAN, FLEX_FAN_PWM_HZ);
#    endif

Thermocouples probes(FLEX_THERMOCOUPLE == FLEX_TC_MAX6675 ? TcChip::MAX6675 : TcChip::MAX31855, FLEX_PIN_TC_SCK,
                     FLEX_PIN_TC_MISO, FLEX_PIN_TC_CS_ET, FLEX_PIN_TC_CS_BT);
HeaterOutput heater(FLEX_PIN_HEATER, FLEX_HEATER_WINDOW_MS, FLEX_HEATER_MAX_PERCENT, FLEX_HEATER_ACTIVE_HIGH);

SafetyLimits limits() {
    SafetyLimits l;
    l.overTempC = FLEX_OVER_TEMP_C;
    l.maxSetpointC = FLEX_MAX_SETPOINT_C;
    l.minFanWithHeat = FLEX_MIN_FAN_WITH_HEAT;
    l.requireMains = FLEX_REQUIRE_MAINS;
    return l;
}
Controller controller(probes, heater, fan, limits());

// The control task owns the SPI bus; Croaster reads its filtered values.
RoasterProbe btProbe(controller, RoasterProbe::Channel::BT);
RoasterProbe etProbe(controller, RoasterProbe::Channel::ET);
CroasterCore core(FLEX_DUMMY_DATA, &btProbe, &etProbe);
Controller* const controllerPtr = &controller;

#else // ===== monitor build: plain Croaster with the selected converter =====
CroasterCore
    core(FLEX_DUMMY_DATA, CroasterPinConfig{FLEX_PIN_TC_SCK, FLEX_PIN_TC_MISO, FLEX_PIN_TC_CS_BT, FLEX_PIN_TC_CS_ET},
         FLEX_THERMOCOUPLE == FLEX_TC_MAX6675 ? CroasterThermocoupleType::MAX6675 : CroasterThermocoupleType::MAX31855);
Controller* const controllerPtr = nullptr;
#endif

#if FLEX_DISPLAY == FLEX_DISPLAY_SSD1306
#    include <CroasterDisplaySSD1306.h>
CroasterDisplaySSD1306 display(core); // Croaster's shared screen (boards/common)
CroasterDisplay* const displayPtr = &display;
#else
CroasterDisplay* const displayPtr = nullptr;
#endif

CroasterApp app(core, displayPtr, FLEX_PIN_LED, FLEX_LED_ON_LEVEL);

#if FLEX_HEATER != FLEX_HEATER_NONE
ControlSettings defaults() {
    ControlSettings d;
    d.gains = {FLEX_PID_KP, FLEX_PID_KI, FLEX_PID_KD};
    d.cooldownFan = {FLEX_COOLDOWN_FAN_STOP, FLEX_COOLDOWN_FAN_TRIP};
    d.watchdog = {FLEX_WATCHDOG_ENABLED != 0, FLEX_WATCHDOG_TIMEOUT_S};
    return d;
}
ControlPlugin plugin(controller);

// Header line for the SSD1306, at most 20 characters: "H50 Fan60 PID" or "TRIP overtemp Fan100".
// "Fan" is spelled out so it can't be read as a Fahrenheit temperature.
String statusLine(const Snapshot& s) {
    char line[24];
    if (s.trip != SafetyTrip::None)
        snprintf(line, sizeof line, "TRIP %s Fan%.0f", tripName(s.trip), s.fanPct);
    else
        snprintf(line, sizeof line, "H%.0f Fan%.0f %s", s.heaterPct, s.fanPct, modeName(s.mode));
    return String(line);
}
#endif

void setup() {
    CroasterDeviceIdentity::setBoardName(FLEX_BOARD_NAME);
    core.changeTemperatureUnit(FLEX_TEMP_UNIT); // Croaster converts BT/ET/RoR; the plugin converts SV and setpoints
    Wire.setPins(FLEX_PIN_I2C_SDA, FLEX_PIN_I2C_SCL); // later Wire.begin() calls use these

#if FLEX_HEATER != FLEX_HEATER_NONE
    // Outputs safe before anything else: heater off, fan off.
    heater.begin();
    fan.begin();
    probes.begin();
    const ControlSettings settings = ConfigStore::load(defaults()); // NVS is ready from here on
    controller.begin(settings.gains, settings.pidInput, settings.cooldownFan.trip, FLEX_CONTROL_CORE,
                     configMAX_PRIORITIES - 3);
    plugin.attach(app, settings);
#    if FLEX_DISPLAY == FLEX_DISPLAY_SSD1306
    display.setHeaderText([]() {
        return statusLine(controller.snapshot());
    }); // heater/fan/mode or trip
#    endif
#endif

    app.begin(); // WiFi (WiFiManager portal on first boot), WebSocket :81, BLE, display

#if FLEX_HEATER != FLEX_HEATER_NONE
    plugin.begin();
#endif
}

void loop() {
    app.loop();
#if FLEX_HEATER != FLEX_HEATER_NONE
    plugin.loop();
#endif
}
