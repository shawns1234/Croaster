#include "fan_dimmer.h"

#include <Arduino.h>
#include <driver/gptimer.h>
#include <soc/gpio_reg.h>

namespace {
gptimer_handle_t s_timer = nullptr;
volatile uint32_t s_gateMask = 0;
volatile uint32_t s_delayUs = 0; // 0 = off
volatile uint64_t s_lastZcUs = 0;
volatile uint32_t s_zcCount = 0;
volatile uint8_t s_phase = 0; // 0 idle, 1 waiting to fire, 2 waiting to release gate
uint32_t s_halfUs = 8333;
constexpr uint32_t kGatePulseUs = 100;

inline void IRAM_ATTR gateHigh() {
    REG_WRITE(GPIO_OUT_W1TS_REG, s_gateMask);
}
inline void IRAM_ATTR gateLow() {
    REG_WRITE(GPIO_OUT_W1TC_REG, s_gateMask);
}

void IRAM_ATTR armAt(uint64_t count) {
    gptimer_alarm_config_t a = {};
    a.alarm_count = count;
    a.flags.auto_reload_on_alarm = false;
    gptimer_set_alarm_action(s_timer, &a);
}

bool IRAM_ATTR onAlarm(gptimer_handle_t, const gptimer_alarm_event_data_t* ev, void*) {
    if (s_phase == 1) {
        gateHigh();
        s_phase = 2;
        armAt(ev->alarm_value + kGatePulseUs);
    } else {
        gateLow();
        s_phase = 0;
    }
    return false;
}

void IRAM_ATTR onZeroCross() {
    uint64_t now = 0;
    gptimer_get_raw_count(s_timer, &now);
    if (now - s_lastZcUs < s_halfUs / 2)
        return; // debounce the Z-C pulse edges / noise
    s_lastZcUs = now;
    s_zcCount = s_zcCount + 1;
    gateLow();
    const uint32_t d = s_delayUs;
    if (d == 0) {
        s_phase = 0;
        return;
    }
    s_phase = 1;
    armAt(now + d);
}
} // namespace

bool FanDimmer::begin() {
    const int zcPin = zc_, gatePin = gate_;
    const uint8_t mainsHz = hz_;
    if (gatePin >= 32)
        return false; // fast path uses the low GPIO bank registers
    s_halfUs = 1000000u / (2u * mainsHz);
    s_gateMask = 1u << gatePin;
    digitalWrite(gatePin, LOW);
    pinMode(gatePin, OUTPUT);
    pinMode(zcPin, INPUT); // RobotDyn module has its own pull-up on Z-C

    gptimer_config_t cfg = {};
    cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    cfg.direction = GPTIMER_COUNT_UP;
    cfg.resolution_hz = 1000000; // 1 tick = 1 us
    if (gptimer_new_timer(&cfg, &s_timer) != ESP_OK)
        return false;
    gptimer_event_callbacks_t cbs = {};
    cbs.on_alarm = onAlarm;
    if (gptimer_register_event_callbacks(s_timer, &cbs, nullptr) != ESP_OK)
        return false;
    if (gptimer_enable(s_timer) != ESP_OK || gptimer_start(s_timer) != ESP_OK)
        return false;

    attachInterrupt(digitalPinToInterrupt(zcPin), onZeroCross, RISING);
    ok_ = true;
    return true;
}

void FanDimmer::set(float pct) {
    if (!(pct > 0))
        pct = 0;
    if (pct > 100)
        pct = 100;
    pct_ = pct;
    if (pct == 0) {
        s_delayUs = 0;
        return;
    }
    // Linear in phase angle (simple; RMS-linear mapping can come later).
    uint32_t d = static_cast<uint32_t>(s_halfUs * (1.0f - pct / 100.0f));
    const uint32_t minD = 150, maxD = s_halfUs - 600; // keep clear of the next zero crossing
    if (d < minD)
        d = minD;
    if (d > maxD)
        d = maxD;
    s_delayUs = d;
}

bool FanDimmer::mainsDetected() const {
    if (!ok_)
        return false;
    uint64_t now = 0;
    gptimer_get_raw_count(s_timer, &now);
    return s_zcCount > 0 && (now - s_lastZcUs) < 100000; // a crossing within 100 ms
}

uint32_t FanDimmer::zeroCrossCount() const {
    return s_zcCount;
}
