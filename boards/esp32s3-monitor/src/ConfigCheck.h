#pragma once
// Compile-time validation of config.h. Each message names the setting to fix.
// Pin rules: the same N16R8 module as boards/esp32s3-pid/src/ConfigCheck.h.

#if !defined(CMON_THERMOCOUPLE) || (CMON_THERMOCOUPLE != CMON_TC_MAX31855 && CMON_THERMOCOUPLE != CMON_TC_MAX6675)
#    error "config.h: CMON_THERMOCOUPLE must be CMON_TC_MAX31855 or CMON_TC_MAX6675"
#endif
#if CMON_DISPLAY != CMON_DISPLAY_NONE && CMON_DISPLAY != CMON_DISPLAY_SSD1306
#    error "config.h: CMON_DISPLAY must be CMON_DISPLAY_NONE or CMON_DISPLAY_SSD1306"
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
    CMON_PIN_TC_SCK,  CMON_PIN_TC_MISO, CMON_PIN_TC_CS_ET, CMON_PIN_TC_CS_BT,
#if CMON_DISPLAY != CMON_DISPLAY_NONE
    CMON_PIN_I2C_SDA, CMON_PIN_I2C_SCL,
#endif
};
constexpr int kPinCount = sizeof(kPins) / sizeof(kPins[0]);

constexpr bool allValid(int i = 0) {
    return i >= kPinCount || (validGpio(kPins[i]) && allValid(i + 1));
}
constexpr bool unique(int i = 0, int j = 1) {
    return i >= kPinCount ? true : (j >= kPinCount ? unique(i + 1, i + 2) : (kPins[i] != kPins[j] && unique(i, j + 1)));
}
static_assert(allValid(), "config.h: a CMON_PIN_* is reserved or missing (use 1, 2, 4-18, 21, 38-42, 47)");
static_assert(unique(), "config.h: two CMON_PIN_* settings use the same GPIO");
} // namespace configcheck
