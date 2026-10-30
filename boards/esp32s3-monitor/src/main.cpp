#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>

// Everything configurable is in config.h (force-included by platformio.ini).
#include "ConfigCheck.h"

#include <CroasterApp.h>
#include <CroasterDeviceIdentity.h>
#include <app/MonitorProbes.h>
#include <app/unit_store.h>
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

#if CMON_ENABLE_WEBUI
#    include <WebUiServer.h>
WebUiServer::Options webUiOptions() {
    WebUiServer::Options o;
    o.port = CMON_WEBUI_PORT;
    o.controls = false; // monitoring only: this board has no heater or fan
    o.board = CMON_BOARD_NAME;
    return o;
}
WebUiServer webUi(webUiOptions()); // starts once WiFi is up (see WebUiServer::loop)
#endif

// Loop task stack: 16 KB instead of Arduino's 8 KB. Croaster handles WebSocket
// commands inside the WebSockets library's nested read callbacks (about 4 levels
// for a message over 125 bytes), so a command handler starts deep in the stack.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
    // Serial is the native USB port (HWCDC). Once a host has opened it, each log line
    // waits up to 20 x the TX timeout (100 ms by default, so about 2 s) when the host
    // stops reading, which stalls loop() and with it the WebSocket server. Croaster logs
    // every WebSocket command, so a PC holding the port open could freeze the board for
    // seconds at a time. With 0 the line is dropped instead.
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
    Serial.setTxTimeoutMs(0);
#endif
    CroasterDeviceIdentity::setBoardName(CMON_BOARD_NAME);
    UnitStore::attach(app, CMON_TEMP_UNIT);           // saved unit, else CMON_TEMP_UNIT
    Wire.setPins(CMON_PIN_I2C_SDA, CMON_PIN_I2C_SCL); // later Wire.begin() calls use these
    monitor.begin();
    app.begin(); // WiFi (WiFiManager portal on first boot), WebSocket :81, BLE, display

#if !CMON_WIFI_SLEEP
    // After app.begin(): WiFi and BLE are up. Arduino keeps the setting for later WiFi restarts.
    Serial.println(WiFi.setSleep(false) ? "# WiFi sleep: off" : "# WiFi sleep: could not turn off");
#endif
    Serial.printf("# Loop stack: %u B, %u B free\n", (unsigned)getArduinoLoopTaskStackSize(),
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}

void loop() {
    app.loop();
#if CMON_ENABLE_WEBUI
    webUi.loop();
#endif
}
