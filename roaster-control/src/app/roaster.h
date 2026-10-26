#pragma once
// The control core. Owns sensors, PID/autotune/profile and both actuators, and runs
// them in one fixed-rate FreeRTOS task (default: core 1). Everything else talks to
// it through two calls:
//   submit(Command)  - thread-safe, non-blocking (queue)
//   snapshot()       - thread-safe copy of the latest state
// Safety interlocks live here, so no network path can bypass them:
// over-temperature, dead PID probe, no mains (AC dimmer), OTA in progress,
// minimum fan while heating, heater ceiling (HeaterOutput maxPercent).
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "core/pid.h"
#include "core/relay_autotune.h"
#include "core/roast_profile.h"
#include "core/roaster_types.h"
#include "hw/fan_output.h"
#include "hw/heater_output.h"
#include "hw/thermocouples.h"

struct RoasterLimits {
    float overTempC = 275;     // any probe -> heater off, fan 100
    float maxSetpointC = 250;  // legacy clamp in Control::setSetpoint
    float minFanWithHeat = 30; // legacy: heater on with fan <= 10 forced fan to 30
    bool requireMains = true;  // refuse heat when the dimmer sees no zero-cross
};

class Roaster {
public:
    Roaster(Thermocouples& tc, HeaterOutput& heater, FanOutput& fan, RoasterLimits lim)
        : tc_(tc)
        , heater_(heater)
        , fan_(fan)
        , lim_(lim)
        , pid_(pidCfg()) {}

    void begin(const PidGains& gains, PidInput input, int core, UBaseType_t priority);
    bool submit(const Command& c);
    Snapshot snapshot() const;
    // Copies a parsed profile in; takes effect on StartProfile. Network task only.
    bool loadProfile(const RoastProfile& p);

    static constexpr uint32_t kTickMs = 10;       // SSR timing resolution
    static constexpr uint32_t kControlEvery = 25; // => 250 ms sensor/PID period

private:
    static Pid::Config pidCfg() {
        Pid::Config c;
        c.dtSeconds = kTickMs * kControlEvery / 1000.0f;
        return c;
    }
    static void taskEntry(void* self);
    void run();
    void apply(const Command& c);
    void controlStep(uint32_t nowMs);
    float pidInput(const ThermoSample& s) const;
    void setMode(Mode m, float pv);

    Thermocouples& tc_;
    HeaterOutput& heater_;
    FanOutput& fan_;
    RoasterLimits lim_;
    Pid pid_;
    RelayAutotune autotune_;
    RoastProfile profile_, pendingProfile_;
    volatile bool profilePending_ = false;
    QueueHandle_t queue_ = nullptr;
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

    // control-task-owned state
    Snapshot st_;
    ThermoSample last_{};
    float manualHeater_ = 0;
    uint32_t profileStartMs_ = 0, sensorBadSinceMs_ = 0;
    Snapshot published_;
};
