#pragma once
#include <CroasterDisplay.h>

#include <app/roaster.h>

/**
 * @class RoasterOledDisplay
 * @brief 128x64 I2C OLED (SSD1306 or SH1106, U8g2) showing BT/ET with RoR plus
 *        the roaster state (heater %, fan %, mode, setpoint, trip).
 *
 * Implements Croaster's CroasterDisplay, like boards/common/CroasterDisplaySSD1306,
 * so CroasterApp drives it (begin/loop, rotate, toggle, OTA progress). Temperatures
 * and RoR come from CroasterCore; roaster state from the control task's snapshot.
 * The controller (SSD1306 / SH1106) is chosen by ROASTER_DISPLAY in config.h.
 */
class RoasterOledDisplay : public CroasterDisplay {
public:
    RoasterOledDisplay(CroasterCore& core, Roaster* roaster)
        : CroasterDisplay(core)
        , roaster(roaster) {}

    void begin() override;
    void loop() override;
    void rotateScreen() override;
    void blinkIndicator(bool state) override;
    void displayToggle() override;
    void updateFirmwareUpdateProgress(int progress) override;
    void updatingStatusToggle(bool isUpdating) override;
    bool isFirmwareUpdating() const override {
        return isUpdatingFirmware;
    }

private:
    void draw();

    Roaster* roaster; ///< nullptr in a monitor-only build
    bool hasDisplay = false;
    bool isDisplayOn = true;
    bool isUpdatingFirmware = false;
    bool rotated = false;
    bool indicator = false;
    unsigned long lastUpdate = 0;
};
