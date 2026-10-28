#include <Arduino.h>
#include <Wire.h>

// Everything configurable is in config.h (force-included by platformio.ini).
#include "ConfigCheck.h"

#include <CroasterApp.h>
#include <CroasterDeviceIdentity.h>
#include <app/MonitorProbes.h>
#include <hw/thermocouples.h>

// ---------------------------------------------------------------------------
// Board wiring: Croaster with the converter selected in config.h, no heater,
// fan or control task. The probes are read by croaster-control's
// hw/thermocouples (same reader and decoders as Croaster PID) and handed to
// Croaster through its constructor.
// ---------------------------------------------------------------------------

Thermocouples probes(CMON_THERMOCOUPLE == CMON_TC_MAX6675 ? TcChip::MAX6675 : TcChip::MAX31855, CMON_PIN_TC_SCK,
                     CMON_PIN_TC_MISO, CMON_PIN_TC_CS_ET, CMON_PIN_TC_CS_BT);
MonitorProbes monitor(probes);
CroasterCore core(CMON_DUMMY_DATA, monitor.bt(), monitor.et());

#if CMON_DISPLAY == CMON_DISPLAY_SSD1306
#    include <CroasterDisplaySSD1306.h>
CroasterDisplaySSD1306 display(core); // Croaster's shared screen (boards/common)
CroasterDisplay* const displayPtr = &display;
#else
CroasterDisplay* const displayPtr = nullptr;
#endif

CroasterApp app(core, displayPtr, CMON_PIN_LED, CMON_LED_ON_LEVEL);

void setup() {
    CroasterDeviceIdentity::setBoardName(CMON_BOARD_NAME);
    core.changeTemperatureUnit(CMON_TEMP_UNIT);
    Wire.setPins(CMON_PIN_I2C_SDA, CMON_PIN_I2C_SCL); // later Wire.begin() calls use these
    monitor.begin();
    app.begin(); // WiFi (WiFiManager portal on first boot), WebSocket :81, BLE, display
}

void loop() {
    app.loop();
}
