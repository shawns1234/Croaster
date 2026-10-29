#pragma once
// Compile-time validation of config.h. Each message names the setting to fix.
#include <cstdint>

#include <hw/esp32s3_opi_pins.h>

#if !defined(CMON_THERMOCOUPLE) || (CMON_THERMOCOUPLE != CMON_TC_MAX31855 && CMON_THERMOCOUPLE != CMON_TC_MAX6675)
#    error "config.h: CMON_THERMOCOUPLE must be CMON_TC_MAX31855 or CMON_TC_MAX6675"
#endif
#if CPID_HEATER != CPID_HEATER_SSR
#    error "config.h: CPID_HEATER must be CPID_HEATER_SSR"
#endif
#if CPID_FAN != CPID_FAN_AC_DIMMER && CPID_FAN != CPID_FAN_PWM
#    error "config.h: CPID_FAN must be CPID_FAN_AC_DIMMER or CPID_FAN_PWM"
#endif
#if CMON_DISPLAY != CMON_DISPLAY_NONE && CMON_DISPLAY != CMON_DISPLAY_SSD1306
#    error "config.h: CMON_DISPLAY must be CMON_DISPLAY_NONE or CMON_DISPLAY_SSD1306"
#endif
#if CPID_REQUIRE_MAINS && CPID_FAN != CPID_FAN_AC_DIMMER
#    error "config.h: CPID_REQUIRE_MAINS needs CPID_FAN_AC_DIMMER (only it can see mains); set it to 0"
#endif
#if CPID_FAN == CPID_FAN_AC_DIMMER && CPID_MAINS_HZ != 50 && CPID_MAINS_HZ != 60
#    error "config.h: CPID_MAINS_HZ must be 50 or 60"
#endif

#if !defined(CMON_WIFI_SLEEP) || (CMON_WIFI_SLEEP != 0 && CMON_WIFI_SLEEP != 1)
#    error "config.h: CMON_WIFI_SLEEP must be 0 or 1"
#endif

namespace configcheck {

constexpr bool validTempUnit(const char* u) {
    return (u[0] == 'C' || u[0] == 'F' || u[0] == 'K') && u[1] == '\0';
}
static_assert(validTempUnit(CMON_TEMP_UNIT), "config.h: CMON_TEMP_UNIT must be \"C\", \"F\" or \"K\"");
#if CMON_ENABLE_WEBUI
static_assert(CMON_WEBUI_PORT > 0 && CMON_WEBUI_PORT < 32768 && CMON_WEBUI_PORT != 81,
              "config.h: CMON_WEBUI_PORT must be 1..32767 and not 81 (Croaster's WebSocket)");
#endif

// Dev-board wiring: UART0 to the USB-UART bridge (43, 44), RGB LED (48; some DevKitC revisions use 38).
constexpr bool isBoardReserved(int p) {
    return p == 43 || p == 44 || p == 48;
}

constexpr bool validGpio(int p) {
    return esp32s3_opi::isUsableGpio(p) && !isBoardReserved(p);
}

constexpr int kPins[] = {
    CMON_PIN_TC_SCK,     CMON_PIN_TC_MISO, CMON_PIN_TC_CS_ET, CMON_PIN_TC_CS_BT, CPID_PIN_HEATER, CPID_PIN_FAN,
#if CPID_FAN == CPID_FAN_AC_DIMMER
    CPID_PIN_ZERO_CROSS,
#endif
#if CMON_DISPLAY != CMON_DISPLAY_NONE
    CMON_PIN_I2C_SDA,    CMON_PIN_I2C_SCL,
#endif
};
constexpr int kPinCount = sizeof(kPins) / sizeof(kPins[0]);

constexpr bool allValid(int i = 0) {
    return i >= kPinCount || (validGpio(kPins[i]) && allValid(i + 1));
}
constexpr bool unique(int i = 0, int j = 1) {
    return i >= kPinCount ? true : (j >= kPinCount ? unique(i + 1, i + 2) : (kPins[i] != kPins[j] && unique(i, j + 1)));
}
static_assert(allValid(),
              "config.h: a CMON_PIN_* or CPID_PIN_* is reserved or missing (use 1, 2, 4-18, 21, 38-42, 47)");
static_assert(unique(), "config.h: two CMON_PIN_*/CPID_PIN_* settings use the same GPIO");

#if CPID_FAN == CPID_FAN_AC_DIMMER
static_assert(CPID_PIN_FAN < 32, "config.h: AC dimmer gate pin must be < 32 (fast GPIO register path)");
#endif
static_assert(CPID_HEATER_MAX_PERCENT > 0 && CPID_HEATER_MAX_PERCENT <= 100,
              "config.h: CPID_HEATER_MAX_PERCENT must be 1..100");
static_assert(CPID_HEATER_WINDOW_MS >= 100 && CPID_HEATER_WINDOW_MS <= 10000,
              "config.h: CPID_HEATER_WINDOW_MS must be 100..10000");
static_assert(CPID_MAX_SETPOINT_C < CPID_OVER_TEMP_C, "config.h: CPID_MAX_SETPOINT_C must be below CPID_OVER_TEMP_C");
static_assert(CPID_MIN_FAN_WITH_HEAT >= 0 && CPID_MIN_FAN_WITH_HEAT <= 100, "CPID_MIN_FAN_WITH_HEAT: 0..100");
static_assert(CPID_PID_INPUT == CPID_PID_INPUT_ET || CPID_PID_INPUT == CPID_PID_INPUT_BT ||
                  CPID_PID_INPUT == CPID_PID_INPUT_MAX,
              "CPID_PID_INPUT: CPID_PID_INPUT_ET, _BT or _MAX");
static_assert(CPID_COOLDOWN_FAN_STOP >= 0 && CPID_COOLDOWN_FAN_STOP <= 100, "CPID_COOLDOWN_FAN_STOP: 0..100");
static_assert(CPID_COOLDOWN_FAN_TRIP >= 0 && CPID_COOLDOWN_FAN_TRIP <= 100, "CPID_COOLDOWN_FAN_TRIP: 0..100");
static_assert(CPID_CONTROL_CORE == 0 || CPID_CONTROL_CORE == 1, "CPID_CONTROL_CORE: 0 or 1");
static_assert(CPID_WATCHDOG_ENABLED == 0 || CPID_WATCHDOG_ENABLED == 1, "CPID_WATCHDOG_ENABLED: 0 or 1");
static_assert(CPID_REQUIRE_AUTOTUNE == 0 || CPID_REQUIRE_AUTOTUNE == 1, "CPID_REQUIRE_AUTOTUNE: 0 or 1");
static_assert(CPID_MAINS_GRACE_MS >= 0 && CPID_MAINS_GRACE_MS <= 10000,
              "CPID_MAINS_GRACE_MS: 0..10000 (heat continues this long after the zero-cross is lost)");
static_assert(CPID_WATCHDOG_TIMEOUT_S >= 2 && CPID_WATCHDOG_TIMEOUT_S <= 600,
              "CPID_WATCHDOG_TIMEOUT_S: 2..600 (WatchdogSettings limits)");
} // namespace configcheck
