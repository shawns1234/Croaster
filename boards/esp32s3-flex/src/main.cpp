#include <Arduino.h>
#include <Wire.h>

// Everything configurable is in config.h (force-included by platformio.ini).
#include "ConfigCheck.h"

#include <CroasterApp.h>
#include <CroasterDeviceIdentity.h>

class Roaster; // RoasterControl; only defined in the roaster build

// ---------------------------------------------------------------------------
// Board wiring: build the parts selected in config.h and hand them to
// Croaster through its public extension points (constructor injection,
// CroasterDisplay, onCommand/onJsonCommand, onJsonData). Nothing in croaster/
// knows about heaters or fans.
// ---------------------------------------------------------------------------

#if ROASTER_HEATER != ROASTER_HEATER_NONE // ===== roaster build =====
#    include <RoasterPlugin.h>
#    include <RoasterProbe.h>
#    include <app/roaster.h>
#    include <hw/heater_output.h>
#    include <hw/thermocouples.h>
#    if ROASTER_FAN == ROASTER_FAN_AC_DIMMER
#        include <hw/fan_dimmer.h>
FanDimmer fan(ROASTER_PIN_ZERO_CROSS, ROASTER_PIN_FAN, ROASTER_MAINS_HZ);
#    else
#        include <hw/fan_pwm.h>
FanPwm fan(ROASTER_PIN_FAN, ROASTER_FAN_PWM_HZ);
#    endif

Thermocouples probes(ROASTER_THERMOCOUPLE == ROASTER_TC_MAX6675 ? TcChip::MAX6675 : TcChip::MAX31855,
                     ROASTER_PIN_TC_SCK, ROASTER_PIN_TC_MISO, ROASTER_PIN_TC_CS_ET, ROASTER_PIN_TC_CS_BT);
HeaterOutput heater(ROASTER_PIN_HEATER, ROASTER_HEATER_WINDOW_MS, ROASTER_HEATER_MAX_PERCENT,
                    ROASTER_HEATER_ACTIVE_HIGH);

RoasterLimits limits() {
    RoasterLimits l;
    l.overTempC = ROASTER_OVER_TEMP_C;
    l.maxSetpointC = ROASTER_MAX_SETPOINT_C;
    l.minFanWithHeat = ROASTER_MIN_FAN_WITH_HEAT;
    l.requireMains = ROASTER_REQUIRE_MAINS;
    return l;
}
Roaster roaster(probes, heater, fan, limits());

// The control task owns the SPI bus; Croaster reads its filtered values.
RoasterProbe btProbe(roaster, RoasterProbe::Channel::BT);
RoasterProbe etProbe(roaster, RoasterProbe::Channel::ET);
CroasterCore core(ROASTER_DUMMY_DATA, &btProbe, &etProbe);
Roaster* const roasterPtr = &roaster;

#else // ===== monitor build: plain Croaster with the selected converter =====
CroasterCore core(ROASTER_DUMMY_DATA,
                  CroasterPinConfig{ROASTER_PIN_TC_SCK, ROASTER_PIN_TC_MISO, ROASTER_PIN_TC_CS_BT,
                                    ROASTER_PIN_TC_CS_ET},
                  ROASTER_THERMOCOUPLE == ROASTER_TC_MAX6675 ? CroasterThermocoupleType::MAX6675
                                                             : CroasterThermocoupleType::MAX31855);
Roaster* const roasterPtr = nullptr;
#endif

#if ROASTER_DISPLAY == ROASTER_DISPLAY_CROASTER_SSD1306
#    include <CroasterDisplaySSD1306.h>
CroasterDisplaySSD1306 display(core);
CroasterDisplay* const displayPtr = &display;
#elif ROASTER_DISPLAY != ROASTER_DISPLAY_NONE
#    include "RoasterOledDisplay.h"
RoasterOledDisplay display(core, roasterPtr);
CroasterDisplay* const displayPtr = &display;
#else
CroasterDisplay* const displayPtr = nullptr;
#endif

CroasterApp app(core, displayPtr, ROASTER_PIN_LED, ROASTER_LED_ON_LEVEL);

#if ROASTER_HEATER != ROASTER_HEATER_NONE
RoasterSettings defaults() {
    RoasterSettings d;
    d.gains = {ROASTER_PID_KP, ROASTER_PID_KI, ROASTER_PID_KD};
    d.cooldownFan = ROASTER_COOLDOWN_FAN;
    return d;
}
RoasterPluginConfig pluginConfig() {
    RoasterPluginConfig c;
    c.commsTimeoutMs = ROASTER_COMMS_TIMEOUT_S * 1000UL;
    c.arduinoOta = ROASTER_ENABLE_ARDUINO_OTA;
    c.otaHostname = "croaster-flex";
    c.otaPassword = ROASTER_OTA_PASSWORD;
    return c;
}
RoasterPlugin plugin(roaster, pluginConfig());
#endif

void setup() {
    CroasterDeviceIdentity::setBoardName(ROASTER_BOARD_NAME);
    Wire.setPins(ROASTER_PIN_I2C_SDA, ROASTER_PIN_I2C_SCL); // later Wire.begin() calls use these

#if ROASTER_HEATER != ROASTER_HEATER_NONE
    // Outputs safe before anything else: heater off, fan off.
    heater.begin();
    fan.begin();
    probes.begin();
    const RoasterSettings settings = ConfigStore::load(defaults()); // NVS is ready from here on
    roaster.begin(settings.gains, settings.pidInput, ROASTER_CONTROL_CORE, configMAX_PRIORITIES - 3);
    plugin.attach(app, settings);
#endif

    app.begin(); // WiFi (WiFiManager portal on first boot), WebSocket :81, BLE, display

#if ROASTER_HEATER != ROASTER_HEATER_NONE
    plugin.begin();
#endif
}

void loop() {
    app.loop();
#if ROASTER_HEATER != ROASTER_HEATER_NONE
    plugin.loop();
#endif
}
