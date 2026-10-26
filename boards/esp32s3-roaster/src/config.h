#pragma once
// ============================================================================
// config.h: the ONE file to edit for your roaster.
//
// Pick the thermocouple chip, heater, fan, display, pins and features below.
// Every other file reads these values; src/ConfigCheck.h rejects invalid
// combinations at compile time with a message saying what to change.
//
// This file is force-included into every translation unit (platformio.ini:
// -include src/config.h) so it can switch Croaster library features such as
// BLE. Keep it to #defines: no includes, no code.
//
// Defaults below: Lonely Binary ESP32-S3 Gold Edition N16R8 (IPEX antenna),
// MAX31855 x2, RobotDyn AC dimmer (fan), SSR-40DA (heater), 0.96" SSD1306 OLED.
// Pins avoid: 0/3/45/46 (strapping), 19/20 (native USB), 26-34 (flash, not on
// the header), 35-37 (octal PSRAM, ON the header: never use), 43/44 (UART0 to
// the USB-UART bridge), 48 (RGB LED). All modules run from the 3V3 pin:
// power the dimmer's VCC from 3V3 too, its Z-C pull-up drives the ESP32 input.
// ============================================================================

// ---- Identity -------------------------------------------------------------
// Board id reported in getDeviceInfo. The Croaster app (ICRM) picks OTA images
// by this id; lowercase per AGENTS.md, but NOT "esp32s3": that id belongs to
// the ES3C28P LCD image, and an app OTA would replace the roaster firmware.
#define ROASTER_BOARD_NAME "esp32s3-roaster"
#define ROASTER_DUMMY_DATA 0 // 1 = report fake temperatures (no probes needed)

// ---- Thermocouple converters (ET + BT share SCK/MISO, one CS each) --------
#define ROASTER_TC_MAX31855 1 // 14-bit, -200..1350 C, fault bits, cold junction
#define ROASTER_TC_MAX6675 2  // 12-bit, 0..1023 C (Croaster's stock module)
#define ROASTER_THERMOCOUPLE ROASTER_TC_MAX31855

#define ROASTER_PIN_TC_SCK 12  // HSPI SCK
#define ROASTER_PIN_TC_MISO 13 // HSPI MISO (MAX31855 DO); MOSI unused
#define ROASTER_PIN_TC_CS_ET 10
#define ROASTER_PIN_TC_CS_BT 11

// ---- Heater output ----------------------------------------------------------
#define ROASTER_HEATER_NONE 0 // temperature monitor only (plain Croaster + your probes)
#define ROASTER_HEATER_SSR 1  // SSR, time-proportional window
#define ROASTER_HEATER ROASTER_HEATER_SSR

#define ROASTER_PIN_HEATER 4          // SSR-40DA input + (3-32 VDC, on > 2.4 V)
#define ROASTER_HEATER_ACTIVE_HIGH 1  // 0 if your SSR driver turns on with a LOW pin
#define ROASTER_HEATER_WINDOW_MS 1000 // SSR period; zero-cross SSR: 1000 ms = 120 half-cycles at 60 Hz
#define ROASTER_HEATER_MAX_PERCENT 90 // hard ceiling, applied after PID/manual (yaeger: 90)

// ---- Fan output -------------------------------------------------------------
#define ROASTER_FAN_NONE 0
#define ROASTER_FAN_AC_DIMMER 1 // RobotDyn-style zero-cross + triac (AC universal/shaded-pole fan)
#define ROASTER_FAN_PWM 2       // DC fan or DC motor driver on an LEDC PWM pin
#define ROASTER_FAN ROASTER_FAN_AC_DIMMER

#define ROASTER_PIN_FAN 5        // dimmer PSM/DIM input, or PWM output (AC dimmer: GPIO < 32)
#define ROASTER_PIN_ZERO_CROSS 6 // dimmer Z-C output (AC dimmer only)
#define ROASTER_MAINS_HZ 60      // 50 or 60 (AC dimmer only); US mains = 60
#define ROASTER_FAN_PWM_HZ 25000 // PWM fan only
#define ROASTER_REQUIRE_MAINS 1  // AC dimmer: refuse heat when no zero-cross is seen

// ---- Display ----------------------------------------------------------------
#define ROASTER_DISPLAY_NONE 0
#define ROASTER_DISPLAY_CROASTER_SSD1306 1 // Croaster's shared screen (boards/common): BT/ET + RoR
#define ROASTER_DISPLAY_ROASTER_SSD1306 2  // roaster screen: temps + heater/fan/mode (U8g2)
#define ROASTER_DISPLAY_ROASTER_SH1106 3   // same, SH1106 controller (some 1.3" GME12864)
#define ROASTER_DISPLAY ROASTER_DISPLAY_ROASTER_SSD1306

#define ROASTER_PIN_I2C_SDA 8 // Arduino default Wire pins (also what Croaster's display uses)
#define ROASTER_PIN_I2C_SCL 9

// ---- Status LED (Croaster "blink" command) ----------------------------------
// LED_BUILTIN = the WS2812 on GPIO48; Arduino-ESP32 3.x drives it via digitalWrite
// (white on HIGH). -1 disables.
#define ROASTER_PIN_LED LED_BUILTIN
#define ROASTER_LED_ON_LEVEL HIGH

// ---- Features ---------------------------------------------------------------
#define ROASTER_ENABLE_BLE 1         // Croaster BLE (app + BLE OTA); 0 saves ~300 KB flash
#define ROASTER_ENABLE_ARDUINO_OTA 1 // espota / `pio run -e esp32s3-roaster-ota -t upload`
#define ROASTER_OTA_PASSWORD ""      // ArduinoOTA password ("" = none)

// ---- Control and safety (Celsius; the app may display F) --------------------
#define ROASTER_PID_KP 1.0f // defaults until set over WebSocket or autotuned (then NVS)
#define ROASTER_PID_KI 0.1f
#define ROASTER_PID_KD 0.01f
#define ROASTER_OVER_TEMP_C 275 // any probe above: heater off, fan 100 %
#define ROASTER_MAX_SETPOINT_C 250
#define ROASTER_MIN_FAN_WITH_HEAT 30 // fan forced to at least this while heating
#define ROASTER_COOLDOWN_FAN 100     // fan % after "stop" (changeable, stored in NVS)
#define ROASTER_COMMS_TIMEOUT_S 30   // heater off if no client traffic while heating; 0 = off
#define ROASTER_CONTROL_CORE 1       // control task core; WiFi/BLE stacks run on core 0

// ---- Derived settings (do not edit) -----------------------------------------
#define CROASTER_HAS_BLE ROASTER_ENABLE_BLE // read by croaster/src/CroasterConstants.h
