#pragma once
// Compile-time validation of config.h. Each message names the setting to fix.
#include <cstdint>

#if !defined(FLEX_THERMOCOUPLE) || (FLEX_THERMOCOUPLE != FLEX_TC_MAX31855 && FLEX_THERMOCOUPLE != FLEX_TC_MAX6675)
#    error "config.h: FLEX_THERMOCOUPLE must be FLEX_TC_MAX31855 or FLEX_TC_MAX6675"
#endif
#if FLEX_HEATER != FLEX_HEATER_NONE && FLEX_HEATER != FLEX_HEATER_SSR
#    error "config.h: FLEX_HEATER must be FLEX_HEATER_NONE or FLEX_HEATER_SSR"
#endif
#if FLEX_FAN != FLEX_FAN_NONE && FLEX_FAN != FLEX_FAN_AC_DIMMER && FLEX_FAN != FLEX_FAN_PWM
#    error "config.h: FLEX_FAN must be FLEX_FAN_NONE, FLEX_FAN_AC_DIMMER or FLEX_FAN_PWM"
#endif
#if FLEX_DISPLAY != FLEX_DISPLAY_NONE && FLEX_DISPLAY != FLEX_DISPLAY_SSD1306
#    error "config.h: FLEX_DISPLAY must be FLEX_DISPLAY_NONE or FLEX_DISPLAY_SSD1306"
#endif
#if FLEX_HEATER != FLEX_HEATER_NONE && FLEX_FAN == FLEX_FAN_NONE
#    error "config.h: a heater needs a fan the firmware controls (airflow interlock). Set FLEX_FAN."
#endif
#if FLEX_HEATER == FLEX_HEATER_NONE && FLEX_FAN != FLEX_FAN_NONE
#    error "config.h: fan without heater is not supported; set FLEX_FAN_NONE for a monitor build"
#endif
#if FLEX_REQUIRE_MAINS && FLEX_FAN != FLEX_FAN_AC_DIMMER && FLEX_HEATER != FLEX_HEATER_NONE
#    error "config.h: FLEX_REQUIRE_MAINS needs FLEX_FAN_AC_DIMMER (only it can see mains); set it to 0"
#endif
#if FLEX_FAN == FLEX_FAN_AC_DIMMER && FLEX_MAINS_HZ != 50 && FLEX_MAINS_HZ != 60
#    error "config.h: FLEX_MAINS_HZ must be 50 or 60"
#endif

namespace configcheck {
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
    FLEX_PIN_TC_SCK,     FLEX_PIN_TC_MISO, FLEX_PIN_TC_CS_ET, FLEX_PIN_TC_CS_BT,
#if FLEX_HEATER != FLEX_HEATER_NONE
    FLEX_PIN_HEATER,
#endif
#if FLEX_FAN != FLEX_FAN_NONE
    FLEX_PIN_FAN,
#endif
#if FLEX_FAN == FLEX_FAN_AC_DIMMER
    FLEX_PIN_ZERO_CROSS,
#endif
#if FLEX_DISPLAY != FLEX_DISPLAY_NONE
    FLEX_PIN_I2C_SDA,    FLEX_PIN_I2C_SCL,
#endif
};
constexpr int kPinCount = sizeof(kPins) / sizeof(kPins[0]);

constexpr bool allValid(int i = 0) {
    return i >= kPinCount || (validGpio(kPins[i]) && allValid(i + 1));
}
constexpr bool unique(int i = 0, int j = 1) {
    return i >= kPinCount ? true : (j >= kPinCount ? unique(i + 1, i + 2) : (kPins[i] != kPins[j] && unique(i, j + 1)));
}
static_assert(allValid(), "config.h: a FLEX_PIN_* is reserved or missing (use 1, 2, 4-18, 21, 38-42, 47)");
static_assert(unique(), "config.h: two FLEX_PIN_* settings use the same GPIO");

#if FLEX_FAN == FLEX_FAN_AC_DIMMER
static_assert(FLEX_PIN_FAN < 32, "config.h: AC dimmer gate pin must be < 32 (fast GPIO register path)");
#endif
static_assert(FLEX_HEATER_MAX_PERCENT > 0 && FLEX_HEATER_MAX_PERCENT <= 100,
              "config.h: FLEX_HEATER_MAX_PERCENT must be 1..100");
static_assert(FLEX_HEATER_WINDOW_MS >= 100 && FLEX_HEATER_WINDOW_MS <= 10000,
              "config.h: FLEX_HEATER_WINDOW_MS must be 100..10000");
static_assert(FLEX_MAX_SETPOINT_C < FLEX_OVER_TEMP_C, "config.h: FLEX_MAX_SETPOINT_C must be below FLEX_OVER_TEMP_C");
static_assert(FLEX_MIN_FAN_WITH_HEAT >= 0 && FLEX_MIN_FAN_WITH_HEAT <= 100, "FLEX_MIN_FAN_WITH_HEAT: 0..100");
static_assert(FLEX_COOLDOWN_FAN >= 0 && FLEX_COOLDOWN_FAN <= 100, "FLEX_COOLDOWN_FAN: 0..100");
static_assert(FLEX_CONTROL_CORE == 0 || FLEX_CONTROL_CORE == 1, "FLEX_CONTROL_CORE: 0 or 1");
} // namespace configcheck
