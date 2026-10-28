#pragma once
// Compile-time validation of config.h. Each message names the setting to fix.
#include <cstdint>

#if !defined(CMON_THERMOCOUPLE) || (CMON_THERMOCOUPLE != CMON_TC_MAX31855 && CMON_THERMOCOUPLE != CMON_TC_MAX6675)
#    error "config.h: CMON_THERMOCOUPLE must be CMON_TC_MAX31855 or CMON_TC_MAX6675"
#endif
#if CPID_HEATER != CPID_HEATER_NONE && CPID_HEATER != CPID_HEATER_SSR
#    error "config.h: CPID_HEATER must be CPID_HEATER_NONE or CPID_HEATER_SSR"
#endif
#if CPID_FAN != CPID_FAN_NONE && CPID_FAN != CPID_FAN_AC_DIMMER && CPID_FAN != CPID_FAN_PWM
#    error "config.h: CPID_FAN must be CPID_FAN_NONE, CPID_FAN_AC_DIMMER or CPID_FAN_PWM"
#endif
#if CMON_DISPLAY != CMON_DISPLAY_NONE && CMON_DISPLAY != CMON_DISPLAY_SSD1306
#    error "config.h: CMON_DISPLAY must be CMON_DISPLAY_NONE or CMON_DISPLAY_SSD1306"
#endif
#if CPID_HEATER != CPID_HEATER_NONE && CPID_FAN == CPID_FAN_NONE
#    error "config.h: a heater needs a fan the firmware controls (airflow interlock). Set CPID_FAN."
#endif
#if CPID_HEATER == CPID_HEATER_NONE && CPID_FAN != CPID_FAN_NONE
#    error "config.h: fan without heater is not supported; set CPID_FAN_NONE for a monitor build"
#endif
#if CPID_REQUIRE_MAINS && CPID_FAN != CPID_FAN_AC_DIMMER && CPID_HEATER != CPID_HEATER_NONE
#    error "config.h: CPID_REQUIRE_MAINS needs CPID_FAN_AC_DIMMER (only it can see mains); set it to 0"
#endif
#if CPID_FAN == CPID_FAN_AC_DIMMER && CPID_MAINS_HZ != 50 && CPID_MAINS_HZ != 60
#    error "config.h: CPID_MAINS_HZ must be 50 or 60"
#endif

namespace configcheck {

constexpr bool validTempUnit(const char* u) {
    return (u[0] == 'C' || u[0] == 'F' || u[0] == 'K') && u[1] == '\0';
}
static_assert(validTempUnit(CMON_TEMP_UNIT), "config.h: CMON_TEMP_UNIT must be \"C\", \"F\" or \"K\"");
// N16R8: GPIO26-32 quad SPI flash, GPIO33-37 octal PSRAM (35-37 are on the header).
constexpr bool isFlashOrPsramPin(int p) {
    return p >= 26 && p <= 37;
}

// Native USB (Serial over USB-CDC).
constexpr bool isUsbPin(int p) {
    return p == 19 || p == 20;
}

// Strapping pins (0, 3, 45, 46), UART0 to the USB-UART bridge (43, 44), RGB LED (48).
constexpr bool isReserved(int p) {
    return p == 0 || p == 3 || p == 45 || p == 46 || p == 43 || p == 44 || p == 48;
}

constexpr bool validGpio(int p) {
    return p >= 1 && p <= 47 && !(p >= 22 && p <= 25) && !isFlashOrPsramPin(p) && !isUsbPin(p) && !isReserved(p);
}

constexpr int kPins[] = {
    CMON_PIN_TC_SCK,     CMON_PIN_TC_MISO, CMON_PIN_TC_CS_ET, CMON_PIN_TC_CS_BT,
#if CPID_HEATER != CPID_HEATER_NONE
    CPID_PIN_HEATER,
#endif
#if CPID_FAN != CPID_FAN_NONE
    CPID_PIN_FAN,
#endif
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
              "CPID_MAINS_GRACE_MS: 0..10000 (heat continues this long without mains)");
static_assert(CPID_WATCHDOG_TIMEOUT_S >= 2 && CPID_WATCHDOG_TIMEOUT_S <= 600,
              "CPID_WATCHDOG_TIMEOUT_S: 2..600 (WatchdogSettings limits)");
} // namespace configcheck
