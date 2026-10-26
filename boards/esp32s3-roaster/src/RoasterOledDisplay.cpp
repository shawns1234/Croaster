#include "RoasterOledDisplay.h"

#include <CroasterDeviceIdentity.h>
#include <U8g2lib.h>
#include <Wire.h>

namespace {
#if ROASTER_DISPLAY == ROASTER_DISPLAY_ROASTER_SH1106
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
#endif

String fmtTemp(double t) {
    return isnan(t) ? String("---") : String(t, 1);
}
} // namespace

void RoasterOledDisplay::begin() {
    Wire.begin(ROASTER_PIN_I2C_SDA, ROASTER_PIN_I2C_SCL, 400000);
    for (uint8_t addr : {0x3C, 0x3D}) { // a missing screen must not stall the firmware
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            oled.setI2CAddress(addr << 1);
            hasDisplay = oled.begin();
            break;
        }
    }
}

void RoasterOledDisplay::loop() {
    if (!hasDisplay || isUpdatingFirmware || !isDisplayOn)
        return;
    unsigned long now = millis();
    if (now - lastUpdate < 500)
        return;
    lastUpdate = now;
    draw();
}

void RoasterOledDisplay::draw() {
    const String unit = _core->temperatureUnit();
    oled.clearBuffer();

    oled.setFont(u8g2_font_6x10_tf);
    String ip = CroasterDeviceIdentity::ipAddress();
    // Top 16 rows are yellow on two-colour panels: header only.
    oled.drawStr(0, 9, ip.isEmpty() ? "no network" : ip.c_str());
    if (indicator)
        oled.drawDisc(124, 4, 3);

    struct Row {
        const char* label;
        double temp, ror;
        int y;
    } rows[] = {{"BT", _core->tempBt, _core->rorBt, 34}, {"ET", _core->tempEt, _core->rorEt, 53}};
    for (const Row& r : rows) {
        oled.setFont(u8g2_font_6x10_tf);
        oled.drawStr(0, r.y - 7, r.label);
        oled.setFont(u8g2_font_logisoso16_tf);
        oled.drawStr(16, r.y, (fmtTemp(r.temp) + unit).c_str());
        oled.setFont(u8g2_font_6x10_tf);
        if (!isnan(r.temp))
            oled.drawStr(98, r.y, String((int)round(r.ror)).c_str()); // RoR, deg/min
    }

    oled.setFont(u8g2_font_6x10_tf);
    char line[32];
    if (roaster) {
        const Snapshot s = roaster->snapshot();
        if (s.trip != SafetyTrip::None)
            snprintf(line, sizeof line, "TRIP %s  F%3.0f", tripName(s.trip), s.fanPct);
        else
            snprintf(line, sizeof line, "H%3.0f F%3.0f %s", s.heaterPct, s.fanPct, modeName(s.mode));
    } else {
        snprintf(line, sizeof line, "monitor");
    }
    oled.drawStr(0, 63, line);
    oled.sendBuffer();
}

void RoasterOledDisplay::rotateScreen() {
    if (!hasDisplay)
        return;
    rotated = !rotated;
    oled.setDisplayRotation(rotated ? U8G2_R2 : U8G2_R0);
    draw();
}

void RoasterOledDisplay::blinkIndicator(bool state) {
    indicator = state;
}

void RoasterOledDisplay::displayToggle() {
    if (!hasDisplay)
        return;
    isDisplayOn = !isDisplayOn;
    oled.setPowerSave(isDisplayOn ? 0 : 1);
}

void RoasterOledDisplay::updateFirmwareUpdateProgress(int progress) {
    if (!hasDisplay)
        return;
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(13, 20, "Updating Firmware");
    oled.drawStr(52, 36, (String(progress) + " %").c_str());
    oled.drawBox(0, 48, progress * 128 / 100, 4);
    oled.sendBuffer();
}

void RoasterOledDisplay::updatingStatusToggle(bool isUpdating) {
    isUpdatingFirmware = isUpdating;
    if (isUpdating && hasDisplay && !isDisplayOn)
        displayToggle();
}
