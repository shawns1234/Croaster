#include "autotune_sequence.h"

#include <algorithm>
#include <cmath>

namespace autotune {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kRateWindowS = 10;   // heat-up rate sample window
constexpr float kMinSwingRatio = 2;  // amplitude must exceed this many hysteresis bands
constexpr int kExtraSettle = 2;      // settling may run this many cycles past settleCycles

float meanOf(const float* v, int n) {
    float s = 0;
    for (int i = 0; i < n; i++)
        s += v[i];
    return s / n;
}
float cvOf(const float* v, int n) {
    const float m = meanOf(v, n);
    if (!(m > 0))
        return INFINITY;
    float s = 0;
    for (int i = 0; i < n; i++)
        s += (v[i] - m) * (v[i] - m);
    return std::sqrt(s / n) / m;
}
void sortUp(float* v, int n) { // n <= kMaxPoints
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && v[j - 1] > v[j]; j--)
            std::swap(v[j - 1], v[j]);
}
float medianOf(float* v, int n) {
    sortUp(v, n);
    return n % 2 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}
} // namespace

const char* phaseName(Phase p) {
    switch (p) {
    case Phase::Heating: return "heating";
    case Phase::Settling: return "settling";
    case Phase::Oscillating: return "oscillating";
    case Phase::Computing: return "computing";
    case Phase::Done: return "done";
    case Phase::Failed: return "failed";
    case Phase::Aborted: return "aborted";
    case Phase::Idle:
    default: return "idle";
    }
}

const char* ruleName(Rule r) {
    switch (r) {
    case Rule::TyreusLuybenPid: return "tyreus-luyben-pid";
    case Rule::SimcPi: return "simc-pi";
    case Rule::ZieglerNichols: return "ziegler-nichols";
    case Rule::TyreusLuybenPi: return "tyreus-luyben-pi";
    case Rule::ZieglerNicholsPi:
    default: return "ziegler-nichols-pi";
    }
}

const char* pointStatusName(PointStatus s) {
    switch (s) {
    case PointStatus::Ok: return "ok";
    case PointStatus::Unsettled: return "unsettled";
    case PointStatus::SmallSwing: return "small swing";
    case PointStatus::Pending:
    default: return "pending";
    }
}

int makePlan(const float* requestedC, int n, float minC, float maxSetpointC, Config& cfg) {
    const float hi = std::min(maxSetpointC, cfg.abortAboveC - cfg.maxOvershootC);
    float v[kMaxPoints];
    int k = 0;
    for (int i = 0; i < n && k < kMaxPoints; i++) {
        const float p = requestedC[i];
        if (!std::isfinite(p) || p <= 0)
            continue;
        v[k++] = std::min(std::max(p, minC), hi);
    }
    sortUp(v, k);
    int out = 0;
    for (int i = 0; i < k; i++)
        if (hi >= minC && (out == 0 || v[i] - cfg.pointsC[out - 1] >= 5))
            cfg.pointsC[out++] = v[i];
    for (int i = out; i < kMaxPoints; i++)
        cfg.pointsC[i] = 0;
    cfg.pointCount = out;
    return out;
}

PidGains Sequence::gainsFor(Rule r, float ku, float tu, const Model& m) {
    float kp, ti, td = 0;
    switch (r) {
    case Rule::ZieglerNichols:
        kp = 0.6f * ku, ti = tu / 2, td = tu / 8;
        break;
    case Rule::TyreusLuybenPid:
        kp = ku / 2.2f, ti = 2.2f * tu, td = tu / 6.3f;
        break;
    case Rule::SimcPi:
        // SIMC with tau_c = L: Kc = tau / (2 K L), tau_I = min(tau, 8 L).
        if (std::isfinite(m.gainCPerPct) && std::isfinite(m.tauS) && m.deadS > 0) {
            kp = m.tauS / (2 * m.gainCPerPct * m.deadS);
            ti = std::min(m.tauS, 8 * m.deadS);
            break;
        }
        // no model: fall back to Tyreus-Luyben PI
        kp = ku / 3.22f, ti = 2.2f * tu;
        break;
    case Rule::TyreusLuybenPi:
        kp = ku / 3.22f, ti = 2.2f * tu;
        break;
    case Rule::ZieglerNicholsPi:
    default:
        kp = 0.45f * ku, ti = tu / 1.2f;
        break;
    }
    PidGains g;
    g.kp = kp;
    g.ki = ti > 0 ? kp / ti : 0;
    g.kd = kp * td;
    return g;
}

Model Sequence::fitModel(float ku, float tu, float targetC, float ambientC, float bias) {
    Model m{NAN, NAN, NAN};
    if (!(bias > 0) || !(tu > 0) || !(targetC > ambientC))
        return m;
    m.gainCPerPct = (targetC - ambientC) / bias;
    const float kku = m.gainCPerPct * ku;
    if (!(kku > 1.0f))
        return m; // a gain this low cannot reach -180 degrees with a single lag
    const float w = 2 * kPi / tu;
    m.tauS = std::sqrt(kku * kku - 1) / w;
    m.deadS = (kPi - std::atan(w * m.tauS)) / w;
    return m;
}

float Sequence::estimateSeconds(const Config& c, float pv) {
    Sequence s;
    s.cfg_ = c;
    s.p_.pointCount = (uint8_t)c.pointCount;
    s.p_.phase = Phase::Heating;
    return s.remaining(std::isfinite(pv) ? pv : c.ambientC);
}

float Sequence::remaining(float pv) const {
    const float rate = rate_ > 0.02f ? rate_ : cfg_.expectedRateCps;
    const float per = lastPeriod_ > 0 ? lastPeriod_ : cfg_.expectedPeriodS;
    const float perPoint = (cfg_.settleCycles + 1 + cfg_.minCycles) * per;
    float s = 0, from = pv;
    for (int i = p_.point; i < cfg_.pointCount; i++) {
        const float to = cfg_.pointsC[i];
        const bool active = i == p_.point;
        if (!active || p_.phase == Phase::Heating) {
            s += std::max(0.0f, to - cfg_.approachC - from) / rate;
            s += perPoint;
        } else if (p_.phase == Phase::Settling) {
            s += (cfg_.settleCycles - settled_ + cfg_.minCycles + 0.5f) * per;
        } else if (p_.phase == Phase::Oscillating) {
            s += std::max(cfg_.minCycles - measured_, 1) * per;
        }
        from = to;
    }
    return std::min(s, std::max(0.0f, cfg_.totalTimeoutS - t_));
}

void Sequence::start(const Config& c, float pv) {
    const uint32_t changes = changes_;
    *this = Sequence();
    changes_ = changes + 1; // a new run always reads as a change
    cfg_ = c;
    cfg_.pointCount = std::max(0, std::min(cfg_.pointCount, kMaxPoints));
    cfg_.minCycles = std::max(2, std::min(cfg_.minCycles, kMaxCycles));
    cfg_.maxCycles = std::max(cfg_.minCycles, std::min(cfg_.maxCycles, kMaxCycles));
    p_.pointCount = (uint8_t)cfg_.pointCount;
    for (int i = 0; i < cfg_.pointCount; i++)
        pts_[i].targetC = cfg_.pointsC[i];
    bias_ = cfg_.initialBias;
    if (cfg_.pointCount == 0) {
        p_.phase = Phase::Heating; // so fail() records it
        fail("no test points");
        return;
    }
    p_.targetC = cfg_.pointsC[0];
    enter(Phase::Heating);
    if (std::isfinite(pv) && pv > p_.targetC + cfg_.maxOvershootC)
        fail("too hot to start");
    p_.remainingS = remaining(std::isfinite(pv) ? pv : cfg_.ambientC);
}

void Sequence::enter(Phase ph) {
    p_.phase = ph;
    phaseT_ = t_;
    p_.cycle = 0;
    p_.cycleOf = ph == Phase::Settling ? (uint8_t)cfg_.settleCycles
                 : ph == Phase::Oscillating ? (uint8_t)cfg_.maxCycles
                                            : 0;
    if (ph == Phase::Heating) {
        stallT_ = rateT_ = t_;
        stallPv_ = NAN; // taken from the next sample
        p_.amplitudeC = p_.periodS = 0;
    }
    if (ph == Phase::Settling || ph == Phase::Oscillating) {
        lastSwitchT_ = t_;
        if (ph == Phase::Settling) {
            lastLowT_ = lastHighT_ = -1;
            settled_ = 0;
        }
        measured_ = 0;
    }
    addLog(p_.targetC, 0);
}

void Sequence::addLog(float a, float b) {
    LogEntry& e = log_[logHead_];
    e = LogEntry{t_, p_.phase, p_.point, p_.cycle, a, b};
    logHead_ = (logHead_ + 1) % kLogSize;
    logCount_ = std::min(logCount_ + 1, kLogSize);
    changes_++;
}

void Sequence::fillReport(Report& r) const {
    r.seq = changes_;
    r.phase = p_.phase;
    r.failure = failure_;
    r.rule = cfg_.rule;
    r.pointCount = cfg_.pointCount;
    for (int i = 0; i < kMaxPoints; i++)
        r.points[i] = pts_[i];
    r.recommended = rec_;
    r.used = used_;
    r.logCount = logCount_;
    for (int i = 0; i < logCount_; i++)
        r.log[i] = logAt(i);
}

void Sequence::fail(const char* why) {
    if (!running())
        return;
    failure_ = why;
    p_.output = 0;
    p_.remainingS = 0;
    p_.phase = Phase::Failed;
    addLog(0, 0);
}

void Sequence::abort(const char* reason) {
    if (!running())
        return;
    failure_ = reason;
    p_.output = 0;
    p_.remainingS = 0;
    p_.phase = Phase::Aborted;
    addLog(0, 0);
}

void Sequence::setStep() {
    bias_ = std::min(std::max(bias_, cfg_.minBias), cfg_.outputMax - cfg_.minBias);
    d_ = std::min(cfg_.relayStep, std::min(bias_, cfg_.outputMax - bias_));
}

float Sequence::update(float pv) {
    if (!running())
        return 0;
    t_ += cfg_.dtSeconds;
    p_.elapsedS = t_;
    if (!std::isfinite(pv)) {
        fail("sensor fault");
        return 0;
    }
    if (pv > cfg_.abortAboveC) {
        fail("over temperature");
        return 0;
    }
    if (pv > p_.targetC + cfg_.maxOvershootC) {
        fail("overshoot");
        return 0;
    }
    if (t_ > cfg_.totalTimeoutS) {
        fail("time limit");
        return 0;
    }

    switch (p_.phase) {
    case Phase::Heating:
        if (std::isnan(stallPv_))
            stallPv_ = ratePv_ = pv;
        if (t_ - rateT_ >= kRateWindowS) {
            const float r = (pv - ratePv_) / (t_ - rateT_);
            rate_ = rate_ > 0 ? 0.7f * rate_ + 0.3f * r : r;
            rateT_ = t_, ratePv_ = pv;
        }
        if (pv >= p_.targetC - cfg_.approachC) {
            if (p_.point > 0 && pts_[p_.point - 1].bias > 0) {
                // Heat loss grows about linearly with the rise over ambient.
                const PointResult& prev = pts_[p_.point - 1];
                bias_ = prev.bias * (p_.targetC - cfg_.ambientC) / (prev.targetC - cfg_.ambientC);
            }
            setStep();
            high_ = pv < p_.targetC;
            enter(Phase::Settling);
            break;
        }
        if (t_ - stallT_ >= cfg_.stallSeconds) {
            if (pv - stallPv_ < cfg_.minRiseC) {
                fail("no heat-up");
                return 0;
            }
            stallT_ = t_, stallPv_ = pv;
        }
        p_.output = cfg_.outputMax;
        break;
    case Phase::Settling:
    case Phase::Oscillating:
        relayStep(pv);
        if (!running())
            return 0;
        break;
    case Phase::Computing:
        finishPoint();
        break;
    default:
        break;
    }
    if (running())
        p_.remainingS = remaining(pv);
    return running() ? p_.output : 0;
}

void Sequence::relayStep(float pv) {
    cycMax_ = std::max(cycMax_, pv);
    cycMin_ = std::min(cycMin_, pv);
    if (t_ - lastSwitchT_ > cfg_.cycleTimeoutS) {
        fail("no oscillation");
        return;
    }
    const float h = cfg_.hysteresisC;
    if (!high_ && pv < p_.targetC - h) {
        high_ = true;
        lastSwitchT_ = t_;
        lastHighT_ = t_;
    } else if (high_ && pv > p_.targetC + h) {
        high_ = false;
        lastSwitchT_ = t_;
        // A cycle is switch-low to switch-low; its swing spans both peaks.
        if (lastLowT_ >= 0 && lastHighT_ > lastLowT_) {
            const float period = t_ - lastLowT_;
            const float tLow = lastHighT_ - lastLowT_, tHigh = t_ - lastHighT_;
            const float amp = (cycMax_ - cycMin_) / 2;
            p_.amplitudeC = amp, p_.periodS = period;
            lastPeriod_ = period;
            if (p_.phase == Phase::Settling) {
                // Centre the duty: more time high than low means the bias is short.
                bias_ += d_ * (tHigh - tLow) / (tHigh + tLow);
                setStep();
                settled_++;
                p_.cycle = (uint8_t)std::min(settled_, 255);
                const float asym = std::fabs(tHigh - tLow) / (tHigh + tLow);
                addLog(amp, period);
                if (settled_ >= cfg_.settleCycles && (asym < 0.2f || settled_ >= cfg_.settleCycles + kExtraSettle))
                    enter(Phase::Oscillating);
            } else {
                amps_[measured_] = amp, periods_[measured_] = period;
                measured_++;
                p_.cycle = (uint8_t)measured_;
                addLog(amp, period);
                bool converged = false;
                if (measured_ >= cfg_.minCycles) {
                    const int n = cfg_.minCycles, from = measured_ - n;
                    converged = cvOf(amps_ + from, n) < cfg_.maxCv && cvOf(periods_ + from, n) < cfg_.maxCv;
                }
                if (converged || measured_ >= cfg_.maxCycles) {
                    pts_[p_.point].status = converged ? PointStatus::Ok : PointStatus::Unsettled;
                    p_.phase = Phase::Computing;
                    p_.output = 0;
                    phaseT_ = t_;
                    addLog(0, 0);
                    cycMax_ = -1e9f, cycMin_ = 1e9f;
                    lastLowT_ = t_;
                    return;
                }
            }
        }
        lastLowT_ = t_;
        cycMax_ = -1e9f, cycMin_ = 1e9f;
    }
    p_.output = relayOut();
}

void Sequence::finishPoint() {
    PointResult& r = pts_[p_.point];
    const int n = std::min(cfg_.minCycles, measured_), from = measured_ - n;
    r.cycles = (uint8_t)measured_;
    r.amplitudeC = meanOf(amps_ + from, n);
    r.periodS = meanOf(periods_ + from, n);
    r.amplitudeCv = cvOf(amps_ + from, n);
    r.periodCv = cvOf(periods_ + from, n);
    r.bias = bias_, r.step = d_;
    const float h = cfg_.hysteresisC, a = r.amplitudeC;
    if (a < kMinSwingRatio * h)
        r.status = PointStatus::SmallSwing;
    // Describing function of a relay with hysteresis: N(a) = 4d / (pi sqrt(a^2 - h^2)).
    r.ku = a > h ? 4 * d_ / (kPi * std::sqrt(a * a - h * h)) : 0;
    r.tuS = r.periodS;
    r.model = fitModel(r.ku, r.tuS, r.targetC, cfg_.ambientC, r.bias);
    r.gains = gainsFor(cfg_.rule, r.ku, r.tuS, r.model);
    addLog(r.ku, r.tuS);

    if (p_.point + 1 < cfg_.pointCount) {
        p_.point++;
        p_.targetC = cfg_.pointsC[p_.point];
        enter(Phase::Heating);
        p_.output = cfg_.outputMax;
        return;
    }
    finishRun();
}

void Sequence::finishRun() {
    // Conservative envelope over the consistent points: the lowest Kp, the
    // slowest integral (largest Ti) and the median Td. Each point's own rule
    // set is at least this cautious in P and I.
    float kps[kMaxPoints], tis[kMaxPoints], tds[kMaxPoints];
    int n = 0;
    for (int i = 0; i < cfg_.pointCount; i++) {
        const PointResult& r = pts_[i];
        if (r.status != PointStatus::Ok || !(r.gains.kp > 0) || !(r.gains.ki > 0))
            continue;
        kps[n] = r.gains.kp;
        tis[n] = r.gains.kp / r.gains.ki;
        tds[n] = r.gains.kd / r.gains.kp;
        n++;
    }
    used_ = n;
    if (n == 0) {
        p_.output = 0;
        p_.phase = Phase::Computing; // still running for fail()
        fail("no consistent oscillation");
        return;
    }
    const float kp = *std::min_element(kps, kps + n);
    const float ti = *std::max_element(tis, tis + n);
    const float td = medianOf(tds, n);
    rec_.kp = kp;
    rec_.ki = kp / ti;
    rec_.kd = kp * td;
    p_.output = 0;
    p_.remainingS = 0;
    p_.phase = Phase::Done;
    addLog(rec_.kp, rec_.ki);
}

} // namespace autotune
