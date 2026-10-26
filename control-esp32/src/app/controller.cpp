#include "controller.h"

#include <Arduino.h>
#include <Update.h>

#include <cmath>

void Controller::begin(const PidGains& gains, PidInput input, int core, UBaseType_t prio) {
    pid_.setGains(gains);
    st_.gains = gains;
    st_.pidInput = input;
    queue_ = xQueueCreate(16, sizeof(Command));
    xTaskCreatePinnedToCore(taskEntry, "control", 6144, this, prio, nullptr, core);
}

bool Controller::submit(const Command& c) {
    return queue_ && xQueueSend(queue_, &c, 0) == pdTRUE;
}

Snapshot Controller::snapshot() const {
    portENTER_CRITICAL(&mux_);
    Snapshot s = published_;
    portEXIT_CRITICAL(&mux_);
    return s;
}

bool Controller::loadProfile(const RoastProfile& p) {
    // Called from the network task; the control task takes it on StartProfile.
    // A later load before StartProfile replaces the earlier one.
    if (p.size() == 0)
        return false;
    portENTER_CRITICAL(&mux_);
    pendingProfile_ = p;
    profilePending_ = true;
    portEXIT_CRITICAL(&mux_);
    return true;
}

void Controller::taskEntry(void* self) {
    static_cast<Controller*>(self)->run();
}

void Controller::run() {
    TickType_t wake = xTaskGetTickCount();
    uint32_t n = 0;
    for (;;) {
        Command c;
        while (xQueueReceive(queue_, &c, 0) == pdTRUE)
            apply(c);
        const uint32_t now = millis();
        if (n++ % kControlEvery == 0)
            controlStep(now);
        heater_.tick(now);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kTickMs));
    }
}

float Controller::pidInput(const ThermoSample& s) const {
    switch (st_.pidInput) {
    case PidInput::BT:
        return s.bt;
    case PidInput::MAX:
        return std::isfinite(s.et) && std::isfinite(s.bt) ? fmaxf(s.et, s.bt) : NAN;
    default:
        return s.et;
    }
}

void Controller::setMode(Mode m, float pv) {
    const bool leavingAutotune = st_.mode == Mode::Autotune && m != Mode::Autotune;
    const float carry = startOutputOnModeChange(st_.mode, m, heater_.requested());
    if (m == Mode::Pid || m == Mode::Profile) {
        if (st_.mode == Mode::Manual || st_.mode == Mode::Autotune)
            pid_.reset(st_.setpoint, pv, carry);
    }
    if (m == Mode::Manual)
        manualHeater_ = carry;
    if (leavingAutotune && autotune_.state() == RelayAutotune::State::Running) {
        autotune_.abort();
        st_.autotuneMsg = autotune_.failure();
    }
    st_.mode = m;
}

void Controller::apply(const Command& c) {
    const float pv = pidInput(last_);
    // A fresh operator action re-arms after a trip; controlStep() re-trips at once if
    // the cause (over-temp, dead probe, no mains) is still present.
    if (c.kind == Command::Kind::SetHeater || c.kind == Command::Kind::SetMode ||
        c.kind == Command::Kind::StartProfile || c.kind == Command::Kind::StartAutotune)
        st_.trip = SafetyTrip::None;
    switch (c.kind) {
    case Command::Kind::SetHeater:
        manualHeater_ = c.a;
        break; // used in Manual mode
    case Command::Kind::SetFan:
        fan_.set(c.a);
        break;
    case Command::Kind::SetSetpoint: {
        const float prev = st_.setpoint;
        st_.setpoint = fmaxf(0, fminf(c.a, lim_.maxSetpointC));
        // Autotune keeps the setpoint it started with, so a new one would leave SV
        // showing a target the relay ignores. Stop tuning (heater off) instead.
        if (st_.mode == Mode::Autotune && st_.setpoint != prev) {
            setMode(Mode::Manual, pv);
            st_.autotuneMsg = "aborted: setpoint changed";
        }
        // legacy Control::setSetpoint: going from 0 to a real setpoint clears the
        // PID history so it starts from zero output, not from stale state.
        if (prev <= 0 && st_.setpoint > 0 && st_.mode == Mode::Pid)
            pid_.reset(st_.setpoint, pv, 0);
        break;
    }
    case Command::Kind::SetMode: {
        const Mode m = static_cast<Mode>(static_cast<int>(c.a));
        if (m == Mode::Manual || m == Mode::Pid)
            setMode(m, pv);
        break;
    }
    case Command::Kind::SetPidInput:
        st_.pidInput = static_cast<PidInput>(static_cast<int>(c.a));
        break;
    case Command::Kind::SetGains:
        st_.gains = {c.a, c.b, c.c};
        pid_.setGains(st_.gains);
        break;
    case Command::Kind::StartAutotune: {
        if (!(st_.setpoint >= 100) || !std::isfinite(pv)) {
            st_.autotuneMsg = "need setpoint >= 100 C (212 F) and a valid probe";
            break;
        }
        if (fan_.get() < 30)
            fan_.set(60); // legacy CommandLoop behaviour
        RelayAutotune::Config cfg;
        cfg.setpoint = st_.setpoint;
        cfg.dtSeconds = pidCfg().dtSeconds;
        cfg.abortAboveC = lim_.overTempC;
        autotune_.start(cfg);
        st_.autotuneMsg = "running";
        st_.mode = Mode::Autotune;
        break;
    }
    case Command::Kind::StartProfile:
        portENTER_CRITICAL(&mux_);
        if (profilePending_) {
            profile_ = pendingProfile_;
            profilePending_ = false;
        }
        portEXIT_CRITICAL(&mux_);
        if (profile_.size()) {
            profileStartMs_ = millis();
            setMode(Mode::Profile, pv);
        }
        break;
    case Command::Kind::Stop:
        manualHeater_ = 0;
        setMode(Mode::Manual, pv);
        manualHeater_ = 0;
        break;
    case Command::Kind::EmergencyOff:
        manualHeater_ = 0;
        setMode(Mode::Manual, pv);
        manualHeater_ = 0;
        fan_.set(100);
        break;
    case Command::Kind::ClearTrip:
        st_.trip = SafetyTrip::None;
        break;
    case Command::Kind::Trip:
        st_.trip = static_cast<SafetyTrip>(static_cast<int>(c.a));
        manualHeater_ = 0;
        setMode(Mode::Manual, pv);
        break;
    }
}

void Controller::controlStep(uint32_t now) {
    last_ = tc_.read();
    const float pv = pidInput(last_);
    float out = 0;

    switch (st_.mode) {
    case Mode::Manual:
        out = manualHeater_;
        break;
    case Mode::Pid:
        out = pid_.update(st_.setpoint, pv);
        break;
    case Mode::Profile: {
        st_.profileElapsedS = (now - profileStartMs_) / 1000.0f;
        const ProfileTarget t = profile_.at(st_.profileElapsedS);
        st_.setpoint = fminf(t.setpointC, lim_.maxSetpointC);
        if (std::isfinite(t.fanPct))
            fan_.set(t.fanPct);
        out = pid_.update(st_.setpoint, pv);
        break;
    }
    case Mode::Autotune:
        out = autotune_.update(pv);
        if (autotune_.state() == RelayAutotune::State::Done) {
            st_.gains = autotune_.result();
            pid_.setGains(st_.gains);
            st_.autotuneSeq++;
            st_.autotuneMsg = "done";
            manualHeater_ = 0; // legacy: after tuning heater off (setpoint 0) and fan 30
            st_.mode = Mode::Manual;
            fan_.set(30);
            out = 0;
        } else if (autotune_.state() == RelayAutotune::State::Failed) {
            st_.autotuneMsg = autotune_.failure();
            manualHeater_ = 0;
            st_.mode = Mode::Manual;
            out = 0;
        }
        break;
    }

    // ---- safety interlocks (order matters: last one wins) ----
    const bool anyProbeHot = (std::isfinite(last_.etRaw) && last_.etRaw > lim_.overTempC) ||
                             (std::isfinite(last_.btRaw) && last_.btRaw > lim_.overTempC);
    if (anyProbeHot) {
        st_.trip = SafetyTrip::OverTemp;
        fan_.set(100);
    }

    const bool inputBad = !std::isfinite(pv) || (!std::isfinite(last_.et) && !std::isfinite(last_.bt));
    if (inputBad && st_.mode != Mode::Manual) {
        if (!sensorBadSinceMs_)
            sensorBadSinceMs_ = now;
        if (now - sensorBadSinceMs_ > 2000)
            st_.trip = SafetyTrip::SensorFault;
    } else {
        sensorBadSinceMs_ = 0;
    }

    // Flash writes stall this task for tens of ms at a time: never heat during OTA.
    if (Update.isRunning())
        st_.trip = SafetyTrip::Firmware;

    st_.mainsOk = fan_.mainsDetected();
    if (out > 0 && lim_.requireMains && !st_.mainsOk)
        st_.trip = SafetyTrip::NoMainsSync;

    if (st_.trip != SafetyTrip::None) {
        out = 0;
        if (st_.mode != Mode::Manual)
            setMode(Mode::Manual, pv);
        manualHeater_ = 0;
    }
    if (out > 0 && fan_.get() < lim_.minFanWithHeat)
        fan_.set(lim_.minFanWithHeat);
    heater_.set(out);

    // ---- publish ----
    st_.et = last_.et;
    st_.bt = last_.bt;
    st_.etRaw = last_.etRaw;
    st_.btRaw = last_.btRaw;
    st_.ambient = last_.ambient;
    st_.etFault = last_.etFault;
    st_.btFault = last_.btFault;
    st_.heaterPct = heater_.requested();
    st_.fanPct = fan_.get();
    st_.uptimeMs = now;
    portENTER_CRITICAL(&mux_);
    published_ = st_;
    portEXIT_CRITICAL(&mux_);
}
