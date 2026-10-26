#pragma once
#include <Arduino.h>
#include <Thermocouple.h>

/**
 * @class CroasterMAX31855
 * @brief MAX31855 K-type thermocouple converter behind the same `Thermocouple`
 *        interface as MAX6675_Thermocouple, so CroasterCore can use either chip.
 *
 * Bit-banged like MAX6675_Thermocouple (any pins, ESP8266 + ESP32, shared
 * SCK/SO with one CS per chip). Returns NAN on a fault (open probe, short to
 * GND/VCC) instead of a bogus number.
 */
class CroasterMAX31855 : public Thermocouple {
public:
    CroasterMAX31855(int8_t sckPin, int8_t csPin, int8_t soPin);

    double readCelsius() override;
    double readKelvin() override;
    double readFahrenheit() override;

    /** @brief Cold-junction (board) temperature from the last read, in Celsius. */
    double internalCelsius() const {
        return lastInternal;
    }

    /** @brief Fault bits of the last read (bit0 open, bit1 short GND, bit2 short VCC; 0 = OK). */
    uint8_t lastFault() const {
        return fault;
    }

private:
    int8_t sck, cs, so;
    double lastInternal = NAN;
    uint8_t fault = 0;

    uint32_t readRaw();
};
