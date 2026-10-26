#pragma once
// Compile-time validation of config.h. Each message names the setting to fix.
#include <cstdint>

#if !defined(ROASTER_THERMOCOUPLE) || \
    (ROASTER_THERMOCOUPLE != ROASTER_TC_MAX31855 && ROASTER_THERMOCOUPLE != ROASTER_TC_MAX6675)
#    error "config.h: ROASTER_THERMOCOUPLE must be ROASTER_TC_MAX31855 or ROASTER_TC_MAX6675"
#endif
#if ROASTER_HEATER != ROASTER_HEATER_NONE && ROASTER_HEATER != ROASTER_HEATER_SSR
#    error "config.h: ROASTER_HEATER must be ROASTER_HEATER_NONE or ROASTER_HEATER_SSR"
#endif
#if ROASTER_FAN != ROASTER_FAN_NONE && ROASTER_FAN != ROASTER_FAN_AC_DIMMER && ROASTER_FAN != ROASTER_FAN_PWM
#    error "config.h: ROASTER_FAN must be ROASTER_FAN_NONE, ROASTER_FAN_AC_DIMMER or ROASTER_FAN_PWM"
#endif
#if ROASTER_DISPLAY != ROASTER_DISPLAY_NONE && ROASTER_DISPLAY != ROASTER_DISPLAY_SSD1306
#    error "config.h: ROASTER_DISPLAY must be ROASTER_DISPLAY_NONE or ROASTER_DISPLAY_SSD1306"
#endif
#if ROASTER_HEATER != ROASTER_HEATER_NONE && ROASTER_FAN == ROASTER_FAN_NONE
#    error "config.h: a heater needs a fan the firmware controls (airflow interlock). Set ROASTER_FAN."
#endif
#if ROASTER_HEATER == ROASTER_HEATER_NONE && ROASTER_FAN != ROASTER_FAN_NONE
#    error "config.h: fan without heater is not supported; set ROASTER_FAN_NONE for a monitor build"
#endif
#if ROASTER_REQUIRE_MAINS && ROASTER_FAN != ROASTER_FAN_AC_DIMMER && ROASTER_HEATER != ROASTER_HEATER_NONE
#    error "config.h: ROASTER_REQUIRE_MAINS needs ROASTER_FAN_AC_DIMMER (only it can see mains); set it to 0"
#endif
#if ROASTER_FAN == ROASTER_FAN_AC_DIMMER && ROASTER_MAINS_HZ != 50 && ROASTER_MAINS_HZ != 60
#    error "config.h: ROASTER_MAINS_HZ must be 50 or 60"
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
    ROASTER_PIN_TC_SCK,     ROASTER_PIN_TC_MISO, ROASTER_PIN_TC_CS_ET, ROASTER_PIN_TC_CS_BT,
#if ROASTER_HEATER != ROASTER_HEATER_NONE
    ROASTER_PIN_HEATER,
#endif
#if ROASTER_FAN != ROASTER_FAN_NONE
    ROASTER_PIN_FAN,
#endif
#if ROASTER_FAN == ROASTER_FAN_AC_DIMMER
    ROASTER_PIN_ZERO_CROSS,
#endif
#if ROASTER_DISPLAY != ROASTER_DISPLAY_NONE
    ROASTER_PIN_I2C_SDA,    ROASTER_PIN_I2C_SCL,
#endif
};
constexpr int kPinCount = sizeof(kPins) / sizeof(kPins[0]);

constexpr bool allValid(int i = 0) {
    return i >= kPinCount || (validGpio(kPins[i]) && allValid(i + 1));
}
constexpr bool unique(int i = 0, int j = 1) {
    return i >= kPinCount ? true : (j >= kPinCount ? unique(i + 1, i + 2) : (kPins[i] != kPins[j] && unique(i, j + 1)));
}
static_assert(allValid(), "config.h: a ROASTER_PIN_* is reserved or missing (use 1, 2, 4-18, 21, 38-42, 47)");
static_assert(unique(), "config.h: two ROASTER_PIN_* settings use the same GPIO");

#if ROASTER_FAN == ROASTER_FAN_AC_DIMMER
static_assert(ROASTER_PIN_FAN < 32, "config.h: AC dimmer gate pin must be < 32 (fast GPIO register path)");
#endif
static_assert(ROASTER_HEATER_MAX_PERCENT > 0 && ROASTER_HEATER_MAX_PERCENT <= 100,
              "config.h: ROASTER_HEATER_MAX_PERCENT must be 1..100");
static_assert(ROASTER_HEATER_WINDOW_MS >= 100 && ROASTER_HEATER_WINDOW_MS <= 10000,
              "config.h: ROASTER_HEATER_WINDOW_MS must be 100..10000");
static_assert(ROASTER_MAX_SETPOINT_C < ROASTER_OVER_TEMP_C,
              "config.h: ROASTER_MAX_SETPOINT_C must be below ROASTER_OVER_TEMP_C");
static_assert(ROASTER_MIN_FAN_WITH_HEAT >= 0 && ROASTER_MIN_FAN_WITH_HEAT <= 100, "ROASTER_MIN_FAN_WITH_HEAT: 0..100");
static_assert(ROASTER_COOLDOWN_FAN >= 0 && ROASTER_COOLDOWN_FAN <= 100, "ROASTER_COOLDOWN_FAN: 0..100");
static_assert(ROASTER_CONTROL_CORE == 0 || ROASTER_CONTROL_CORE == 1, "ROASTER_CONTROL_CORE: 0 or 1");
} // namespace configcheck
