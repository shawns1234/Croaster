#pragma once
// GPIO rules for ESP32-S3 modules with octal PSRAM (N8R8, N16R8, ...), constexpr for static_assert.
// Chip level: GPIO 22-25 do not exist, 19/20 are native USB, 0/3/45/46 are strapping pins.
// Module level (octal PSRAM): GPIO 26-32 are the SPI flash bus, 33-37 the octal PSRAM lines.
// Dev-board wiring (UART0 bridge, RGB LED) is not covered: each board's ConfigCheck.h adds it.

namespace esp32s3_opi {

constexpr bool isGpio(int p) {
    return p >= 0 && p <= 48 && !(p >= 22 && p <= 25);
}

constexpr bool isFlashOrPsramPin(int p) {
    return p >= 26 && p <= 37;
}

// Native USB (Serial over USB-CDC).
constexpr bool isUsbPin(int p) {
    return p == 19 || p == 20;
}

constexpr bool isStrappingPin(int p) {
    return p == 0 || p == 3 || p == 45 || p == 46;
}

// Free for board use on the module, before any dev-board wiring.
constexpr bool isUsableGpio(int p) {
    return isGpio(p) && !isFlashOrPsramPin(p) && !isUsbPin(p) && !isStrappingPin(p);
}

} // namespace esp32s3_opi
