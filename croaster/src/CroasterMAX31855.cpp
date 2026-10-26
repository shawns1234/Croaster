#include "CroasterMAX31855.h"

CroasterMAX31855::CroasterMAX31855(int8_t sckPin, int8_t csPin, int8_t soPin)
    : sck(sckPin)
    , cs(csPin)
    , so(soPin) {
    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
    pinMode(sck, OUTPUT);
    digitalWrite(sck, LOW);
    pinMode(so, INPUT);
}

uint32_t CroasterMAX31855::readRaw() {
    uint32_t v = 0;
    digitalWrite(cs, LOW);
    delayMicroseconds(1);
    for (int i = 0; i < 32; i++) {
        digitalWrite(sck, HIGH);
        delayMicroseconds(1);
        v = (v << 1) | (digitalRead(so) ? 1 : 0);
        digitalWrite(sck, LOW);
        delayMicroseconds(1);
    }
    digitalWrite(cs, HIGH);
    return v;
}

double CroasterMAX31855::readCelsius() {
    const uint32_t v = readRaw();

    int16_t internal = (v >> 4) & 0x0FFF; // 12-bit signed, 0.0625 C
    if (internal & 0x0800)
        internal |= 0xF000;
    lastInternal = internal * 0.0625;

    fault = v & 0x07;
    if (v == 0 || v == 0xFFFFFFFF || (v & 0x00010000) || fault)
        return NAN; // no chip / bus stuck, or fault flag set

    int16_t tc = (v >> 18) & 0x3FFF; // 14-bit signed, 0.25 C
    if (tc & 0x2000)
        tc |= 0xC000;
    return tc * 0.25;
}

double CroasterMAX31855::readKelvin() {
    return readCelsius() + 273.15;
}

double CroasterMAX31855::readFahrenheit() {
    return readCelsius() * 9.0 / 5.0 + 32.0;
}
