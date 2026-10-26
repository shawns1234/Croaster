#pragma once
#include <Arduino.h>

/**
 * @enum CroasterThermocoupleType
 * @brief Thermocouple converter chip on the (shared) SPI bus.
 */
enum class CroasterThermocoupleType : uint8_t {
    MAX6675,  ///< 12-bit, 0..1024 C, 0.25 C steps (Croaster default)
    MAX31855, ///< 14-bit, -270..1800 C, 0.25 C steps, fault bits (Yaeger hardware)
};

/**
 * @struct CroasterPinConfig
 * @brief Pin assignments for the thermocouple (MAX6675 / MAX31855) sensors.
 *
 * The firmware core is decoupled from any specific wiring: every implementation
 * provides its own CroasterPinConfig (in its config.h) and passes it to the
 * CroasterCore constructor. There is no library default — pins are always
 * explicit.
 */
struct CroasterPinConfig {
    int8_t sckPin;  ///< MAX6675 SPI clock (SCK)
    int8_t soPin;   ///< MAX6675 SPI MISO (SO)
    int8_t csPinBt; ///< MAX6675 chip-select for the bean (BT) thermocouple
    int8_t csPinEt; ///< MAX6675 chip-select for the environment (ET) thermocouple
};
