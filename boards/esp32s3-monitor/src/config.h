#pragma once
// ============================================================================
// config.h: the ONE file to edit for your monitor.
//
// Pick the thermocouple chip, display, pins and features below. Every other
// file reads these values; src/ConfigCheck.h rejects invalid combinations at
// compile time with a message saying what to change.
//
// This file is force-included into every translation unit (platformio.ini:
// -include src/config.h) so it can switch Croaster library features such as
// BLE. Keep it to #defines: no includes, no code.
//
// Defaults below: same hardware as Croaster PID (boards/esp32s3-pid) without
// the heater and fan: Lonely Binary ESP32-S3 Gold Edition N16R8 (IPEX antenna),
// MAX31855 x2, 0.96" SSD1306 OLED (128x64, I2C, address 0x3C).
// Pins avoid: 0/3/45/46 (strapping), 19/20 (native USB), 26-34 (flash, not on
// the header), 35-37 (octal PSRAM, ON the header: never use), 43/44 (UART0 to
// the USB-UART bridge), 48 (RGB LED). All modules run from the 3V3 pin.
// ============================================================================

// ---- Identity -------------------------------------------------------------
// Board id reported in getDeviceInfo. The Croaster app (ICRM) picks OTA images
// by this id; lowercase per AGENTS.md, but NOT "esp32s3": that id belongs to
// the ES3C28P LCD image, and an app OTA would replace this firmware.
#define CMON_BOARD_NAME "esp32s3-monitor"
#define CMON_DUMMY_DATA 0 // 1 = report fake temperatures (no probes needed)
// Unit for the screen and telemetry: "C", "F" or "K". Applied at every boot;
// Croaster's {"command":{"tempUnit":"C"}} changes it until the next reboot.
#define CMON_TEMP_UNIT "F"

// ---- Thermocouple converters (ET + BT share SCK/MISO, one CS each) --------
#define CMON_TC_MAX31855 1 // 14-bit, -200..1350 C, fault bits, cold junction
#define CMON_TC_MAX6675 2  // 12-bit, 0..1023 C (Croaster's stock module)
#define CMON_THERMOCOUPLE CMON_TC_MAX31855

#define CMON_PIN_TC_SCK 12  // HSPI SCK
#define CMON_PIN_TC_MISO 13 // HSPI MISO (MAX31855 DO); MOSI unused
#define CMON_PIN_TC_CS_ET 10
#define CMON_PIN_TC_CS_BT 11

// ---- Display ----------------------------------------------------------------
#define CMON_DISPLAY_NONE 0
#define CMON_DISPLAY_SSD1306 1 // Croaster's shared screen (boards/common)
#define CMON_DISPLAY CMON_DISPLAY_SSD1306

#define CMON_PIN_I2C_SDA 8 // Arduino default Wire pins (also what Croaster's display uses)
#define CMON_PIN_I2C_SCL 9

// ---- Status LED (Croaster "blink" command) ----------------------------------
// LED_BUILTIN = the WS2812 on GPIO48; Arduino-ESP32 3.x drives it via digitalWrite
// (white on HIGH). -1 disables.
#define CMON_PIN_LED LED_BUILTIN
#define CMON_LED_ON_LEVEL HIGH

// ---- Features ---------------------------------------------------------------
#define CMON_ENABLE_BLE 1 // Croaster BLE (app + BLE OTA); 0 saves ~300 KB flash

// ---- Derived settings (do not edit) -----------------------------------------
#define CROASTER_HAS_BLE CMON_ENABLE_BLE // read by croaster/src/CroasterConstants.h
