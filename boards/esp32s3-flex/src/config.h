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
// MAX31855 x2, RobotDyn AC dimmer (fan), SSR-40DA (heater), 0.96" SSD1306 OLED
// (128x64, I2C, address 0x3C; confirmed by Shawn 2026-10-26).
// Pins avoid: 0/3/45/46 (strapping), 19/20 (native USB), 26-34 (flash, not on
// the header), 35-37 (octal PSRAM, ON the header: never use), 43/44 (UART0 to
// the USB-UART bridge), 48 (RGB LED). All modules run from the 3V3 pin:
// power the dimmer's VCC from 3V3 too, its Z-C pull-up drives the ESP32 input.
// ============================================================================

// ---- Identity -------------------------------------------------------------
// Board id reported in getDeviceInfo. The Croaster app (ICRM) picks OTA images
// by this id; lowercase per AGENTS.md, but NOT "esp32s3": that id belongs to
// the ES3C28P LCD image, and an app OTA would replace the roaster firmware.
#define FLEX_BOARD_NAME "esp32s3-flex"
#define FLEX_DUMMY_DATA 0 // 1 = report fake temperatures (no probes needed)
// Unit for the screen, telemetry ("temp" field) and incoming setpoints: "C",
// "F" or "K". Applied at every boot; Croaster's {"command":{"tempUnit":"C"}}
// changes it until the next reboot. Control limits below stay in Celsius.
// Artisan's .aset must match: artisan/croaster-flex.aset is set up for "F".
#define FLEX_TEMP_UNIT "F"

// ---- Thermocouple converters (ET + BT share SCK/MISO, one CS each) --------
#define FLEX_TC_MAX31855 1 // 14-bit, -200..1350 C, fault bits, cold junction
#define FLEX_TC_MAX6675 2  // 12-bit, 0..1023 C (Croaster's stock module)
#define FLEX_THERMOCOUPLE FLEX_TC_MAX31855

#define FLEX_PIN_TC_SCK 12  // HSPI SCK
#define FLEX_PIN_TC_MISO 13 // HSPI MISO (MAX31855 DO); MOSI unused
#define FLEX_PIN_TC_CS_ET 10
#define FLEX_PIN_TC_CS_BT 11

// ---- Heater output ----------------------------------------------------------
#define FLEX_HEATER_NONE 0 // temperature monitor only (plain Croaster + your probes)
#define FLEX_HEATER_SSR 1  // SSR, time-proportional window
#define FLEX_HEATER FLEX_HEATER_SSR

#define FLEX_PIN_HEATER 4          // SSR-40DA input + (3-32 VDC, on > 2.4 V)
#define FLEX_HEATER_ACTIVE_HIGH 1  // 0 if your SSR driver turns on with a LOW pin
#define FLEX_HEATER_WINDOW_MS 1000 // SSR period; zero-cross SSR: 1000 ms = 120 half-cycles at 60 Hz
#define FLEX_HEATER_MAX_PERCENT 90 // hard ceiling, applied after PID/manual (yaeger: 90)

// ---- Fan output -------------------------------------------------------------
#define FLEX_FAN_NONE 0
#define FLEX_FAN_AC_DIMMER 1 // RobotDyn-style zero-cross + triac (AC universal/shaded-pole fan)
#define FLEX_FAN_PWM 2       // DC fan or DC motor driver on an LEDC PWM pin
#define FLEX_FAN FLEX_FAN_AC_DIMMER

#define FLEX_PIN_FAN 5        // dimmer PSM/DIM input, or PWM output (AC dimmer: GPIO < 32)
#define FLEX_PIN_ZERO_CROSS 6 // dimmer Z-C output (AC dimmer only)
#define FLEX_MAINS_HZ 60      // 50 or 60 (AC dimmer only); US mains = 60
#define FLEX_FAN_PWM_HZ 25000 // PWM fan only
#define FLEX_REQUIRE_MAINS 0  // AC dimmer: refuse heat when no zero-cross is seen
// FLEX_REQUIRE_MAINS: trip only after mains has been missing this long while heating
#define FLEX_MAINS_GRACE_MS 2000

// ---- Display ----------------------------------------------------------------
#define FLEX_DISPLAY_NONE 0
#define FLEX_DISPLAY_SSD1306 1            // Croaster's shared screen (boards/common); header shows heater/fan/mode
#define FLEX_DISPLAY FLEX_DISPLAY_SSD1306 // Shawn's 0.96" 128x64 SSD1306 (confirmed)

#define FLEX_PIN_I2C_SDA 8 // Arduino default Wire pins (also what Croaster's display uses)
#define FLEX_PIN_I2C_SCL 9

// ---- Status LED (Croaster "blink" command) ----------------------------------
// LED_BUILTIN = the WS2812 on GPIO48; Arduino-ESP32 3.x drives it via digitalWrite
// (white on HIGH). -1 disables.
#define FLEX_PIN_LED LED_BUILTIN
#define FLEX_LED_ON_LEVEL HIGH

// ---- Features ---------------------------------------------------------------
#define FLEX_ENABLE_BLE 1 // Croaster BLE (app + BLE OTA); 0 saves ~300 KB flash

// ---- Control and safety (always Celsius, whatever FLEX_TEMP_UNIT is) --------
// PID input: the probe the firmware PID and autotune regulate. First-boot
// default; {"command":{"pidInput":"ET"}} changes it at runtime and saves it in
// NVS, and a saved value wins over this default. Telemetry "Target" shows the
// active input. BT needs a BT probe: without one, PID mode trips "sensor".
#define FLEX_PID_INPUT_ET 0
#define FLEX_PID_INPUT_BT 1
#define FLEX_PID_INPUT_MAX 2 // the hotter of ET and BT
#define FLEX_PID_INPUT FLEX_PID_INPUT_BT
// Gains: defaults until set over WebSocket or autotuned (then NVS). These are
// yaeger's values, chosen for ET. BT lags the heater by tens of seconds, so run
// autotune with the PID input on BT before relying on PID control.
#define FLEX_PID_KP 1.0f
#define FLEX_PID_KI 0.1f
#define FLEX_PID_KD 0.01f
// 1: PID and Profile modes are refused ("autotune required") until the board has
// tuned gains in NVS, from a completed autotune or the pid command. Manual and
// autotune always work. 0: the default gains above are usable at once.
#define FLEX_REQUIRE_AUTOTUNE 1
#define FLEX_OVER_TEMP_C 275      // 527 F; any probe above: heater off, fan 100 %
#define FLEX_MAX_SETPOINT_C 250   // 482 F; higher setpoints are capped here
#define FLEX_MIN_FAN_WITH_HEAT 30 // fan forced to at least this while heating
// Cooldown fan: first-boot defaults; {"command":{"cooldownFan":{"stop":N,"trip":N}}}
// changes them at runtime and saves them in NVS.
#define FLEX_COOLDOWN_FAN_STOP 65  // fan % after "stop" (yaeger's default)
#define FLEX_COOLDOWN_FAN_TRIP 100 // fan % when any safety trip starts (over-temp: always 100)
// Comms watchdog: heater off + trip fan when no client message arrives for
// the timeout while heating (profile runs exempt). These are first-boot defaults;
// {"command":{"watchdog":{"enabled":false}}} or {...{"timeout":20}} changes them
// at runtime and saves them in NVS.
#define FLEX_WATCHDOG_ENABLED 1    // 0 = no comms cutoff at all
#define FLEX_WATCHDOG_TIMEOUT_S 10 // 2..600 s
#define FLEX_CONTROL_CORE 1        // control task core; WiFi/BLE stacks run on core 0

// ---- Derived settings (do not edit) -----------------------------------------
#define CROASTER_HAS_BLE FLEX_ENABLE_BLE // read by croaster/src/CroasterConstants.h
