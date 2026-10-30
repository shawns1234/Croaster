// Multi-point autotune (core/autotune_sequence.h, autotune_protocol.h) against
// simulated drums. Called from test_main.cpp (runAutotuneTests).
#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <string>

#include <ArduinoJson.h>

#include "core/autotune_protocol.h"
#include "core/autotune_sequence.h"
#include "core/autotune_status.h"
#include "core/pid.h"

using namespace autotune;

namespace {

// First order plus dead time with probe noise and MAX31855 quantisation.
struct FopdtDrum {
    double T = 25, amb = 25, K = 3.0 /*C per %*/, tau = 240, L = 30, dt = 0.25, noise = 0.2;
    double hist[2048] = {};
    int head = 0;
    std::mt19937 rng{7};
    std::normal_distribution<double> nrm{0, 1};
    FopdtDrum() {
        for (double& h : hist)
            h = amb;
    }
    float step(float u) {
        u = std::min(std::max(u, 0.0f), 100.0f);
        hist[head] = u;
        head = (head + 1) % 2048;
        const int nd = (int)std::lround(L / dt);
        const double ud = hist[(head + 2048 - 1 - nd) % 2048];
        T += (amb + K * ud - T) * dt / tau;
        return (float)(std::round((T + noise * nrm(rng)) * 4) / 4);
    }
    // Ultimate point of K e^{-Ls}/(tau s + 1): phase -pi at wu, Ku = 1/|G(j wu)|.
    void ultimate(double& ku, double& tu) const {
        double lo = 1e-4, hi = 3.14159 / L;
        for (int i = 0; i < 100; i++) {
            const double w = 0.5 * (lo + hi);
            (std::atan(w * tau) + w * L < 3.14159265) ? lo = w : hi = w;
        }
        const double w = 0.5 * (lo + hi);
        ku = std::sqrt(1 + w * tau * w * tau) / K;
        tu = 2 * 3.14159265 / w;
    }
};

// Behmor-like drum: element lag, heat loss rising with temperature (so gain and
// time constant fall as it gets hotter), transport delay and a probe lag.
struct Drum {
    double x1 = 0, T = 25, Tp = 25;
    double tau1 = 20, tauP = 8, dead = 10, dt = 0.25, kloss = 0.004, tau150 = 250, noise = 0.25;
    double h0, C;
    double hist[800];
    int head = 0, nd;
    std::mt19937 rng{1};
    std::normal_distribution<double> nrm{0, 1};
    Drum() {
        init();
    }
    void init() {
        h0 = 1.0 / (275 * (1 + kloss * 275)); // 100 % holds 300 C
        C = tau150 * h0 * (1 + kloss * 125);
        nd = (int)(dead / dt);
        for (double& h : hist)
            h = 25;
    }
    float step(float u) {
        u = std::min(std::max(u, 0.0f), 100.0f);
        x1 += (u / 100 - x1) * dt / tau1;
        const double hA = h0 * (1 + kloss * (T - 25));
        T += (x1 - hA * (T - 25)) / C * dt;
        hist[head] = T;
        head = (head + 1) % 800;
        const double Td = hist[(head + 800 - nd) % 800];
        Tp += (Td - Tp) * dt / tauP;
        return (float)(std::round((Tp + noise * nrm(rng)) * 4) / 4);
    }
};

template <typename Plant>
float runToEnd(Sequence& s, Plant& p, float pv, float maxS = 20000, float* peakOut = nullptr) {
    float t = 0, peak = 0;
    while (s.running() && t < maxS) {
        const float u = s.update(pv);
        TEST_ASSERT_TRUE(u >= 0 && u <= 100);
        pv = p.step(u);
        peak = std::max(peak, pv);
        t += 0.25f;
    }
    if (peakOut)
        *peakOut = peak;
    return t;
}

Config onePoint(float c) {
    Config cfg;
    cfg.pointsC[0] = c;
    cfg.pointCount = 1;
    return cfg;
}

void assertOff(Sequence& s, float pv) {
    TEST_ASSERT_FALSE(s.running());
    TEST_ASSERT_EQUAL_FLOAT(0, s.progress().output);
    for (int i = 0; i < 8; i++)
        TEST_ASSERT_EQUAL_FLOAT(0, s.update(pv)); // stays off
}

} // namespace

// The relay finds the ultimate point of a dead-time plant within the
// describing-function error, and the cycles agree.
void test_at_fopdt_ultimate_point() {
    FopdtDrum p;
    Sequence s;
    Config c = onePoint(177);
    s.start(c, 25);
    runToEnd(s, p, 25);
    TEST_ASSERT_EQUAL(Phase::Done, s.phase());
    const PointResult& r = s.point(0);
    TEST_ASSERT_EQUAL(PointStatus::Ok, r.status);
    TEST_ASSERT_TRUE(r.amplitudeCv < c.maxCv && r.periodCv < c.maxCv);
    double ku, tu;
    p.ultimate(ku, tu);
    // The describing function reads Ku low on lag-dominant plants (about 20 %
    // here), which errs on the cautious side; Tu is close.
    TEST_ASSERT_FLOAT_WITHIN(0.3 * ku, ku, r.ku);
    TEST_ASSERT_TRUE(r.ku < ku);
    TEST_ASSERT_FLOAT_WITHIN(0.15 * tu, tu, r.tuS);
    // Static gain from the bias that holds the target: exact for a linear plant
    // up to the asymmetry the bias leaves.
    TEST_ASSERT_FLOAT_WITHIN(0.15 * p.K, p.K, r.model.gainCPerPct);
    TEST_ASSERT_FLOAT_WITHIN(0.5 * p.L, p.L, r.model.deadS);
    TEST_ASSERT_FLOAT_WITHIN(0.5 * p.tau, p.tau, r.model.tauS);
    TEST_ASSERT_EQUAL_FLOAT(0, s.progress().output);
}

// Three points on a nonlinear drum: each converges, the recommendation is no
// more aggressive than any point, and it controls the drum without overshoot.
void test_at_multi_point_run() {
    Drum p;
    Sequence s;
    Config c;
    s.start(c, 25);
    TEST_ASSERT_EQUAL(3, s.progress().pointCount);
    int lastPoint = 0;
    float pv = 25, t = 0;
    bool sawPhase[8] = {};
    while (s.running() && t < 20000) {
        const float u = s.update(pv);
        pv = p.step(u);
        t += 0.25f;
        const Progress& g = s.progress();
        sawPhase[(int)g.phase] = true;
        TEST_ASSERT_TRUE(g.point >= lastPoint); // points in order
        lastPoint = g.point;
        TEST_ASSERT_TRUE(g.remainingS >= 0);
    }
    TEST_ASSERT_EQUAL(Phase::Done, s.phase());
    TEST_ASSERT_TRUE(sawPhase[(int)Phase::Heating] && sawPhase[(int)Phase::Settling] &&
                     sawPhase[(int)Phase::Oscillating]);
    TEST_ASSERT_TRUE(t < c.totalTimeoutS);
    TEST_ASSERT_EQUAL(3, s.pointsUsed());
    float tuMin = 1e9f;
    for (int i = 0; i < 3; i++) {
        const PointResult& r = s.point(i);
        TEST_ASSERT_EQUAL(PointStatus::Ok, r.status);
        TEST_ASSERT_EQUAL_FLOAT(c.pointsC[i], r.targetC);
        TEST_ASSERT_TRUE(r.gains.kp >= s.recommended().kp);
        TEST_ASSERT_TRUE(r.gains.kp / r.gains.ki <= s.recommended().kp / s.recommended().ki + 1e-3f);
        TEST_ASSERT_TRUE(r.bias > 10 && r.bias < 90);
        tuMin = std::min(tuMin, r.tuS);
    }
    // Hotter points need more heat to hold: the bias rises with the point.
    TEST_ASSERT_TRUE(s.point(0).bias < s.point(1).bias && s.point(1).bias < s.point(2).bias);
    TEST_ASSERT_TRUE(tuMin > 60);
    TEST_ASSERT_EQUAL(0, s.recommended().kd); // default rule is PI

    // Closed loop on a fresh drum: step to 177 C, ramp 6 C/min to 210 C, hold.
    Drum q;
    Pid::Config pc;
    Pid pid(pc);
    pid.setGains(s.recommended());
    pv = 25;
    float overshoot = 0, rampErr = 0, holdErr = 0;
    for (float tt = 0; tt < 2400; tt += 0.25f) {
        const float sp = tt < 900 ? 177 : (tt < 1230 ? 177 + (tt - 900) * 0.1f : 210);
        pv = q.step(pid.update(sp, pv));
        if (tt > 100 && tt < 900)
            overshoot = std::max(overshoot, pv - 177);
        if (tt > 900 && tt < 1230)
            rampErr += std::fabs(pv - sp) * 0.25f / 330;
        if (tt > 1500)
            holdErr += std::fabs(pv - 210) * 0.25f / 900;
    }
    TEST_ASSERT_TRUE(overshoot < 4);
    TEST_ASSERT_TRUE(rampErr < 5);
    TEST_ASSERT_TRUE(holdErr < 1);
}

void test_at_abort_sensor_fault() {
    Drum p;
    Sequence s;
    s.start(Config(), 25);
    float pv = 25;
    for (int i = 0; i < 400; i++)
        pv = p.step(s.update(pv));
    TEST_ASSERT_TRUE(s.progress().output > 0);
    s.update(NAN);
    TEST_ASSERT_EQUAL(Phase::Failed, s.phase());
    TEST_ASSERT_EQUAL_STRING("sensor fault", s.failure());
    assertOff(s, pv);
}

void test_at_abort_over_temperature() {
    Sequence s;
    Config c = onePoint(177);
    c.abortAboveC = 190;
    s.start(c, 25);
    s.update(150);
    TEST_ASSERT_TRUE(s.running());
    s.update(190.5f);
    TEST_ASSERT_EQUAL_STRING("over temperature", s.failure());
    assertOff(s, 150);
}

void test_at_abort_overshoot() {
    Sequence s;
    Config c = onePoint(177);
    s.start(c, 25);
    s.update(170); // into the approach band: relay
    TEST_ASSERT_EQUAL(Phase::Settling, s.phase());
    s.update(177 + c.maxOvershootC + 0.5f);
    TEST_ASSERT_EQUAL(Phase::Failed, s.phase());
    TEST_ASSERT_EQUAL_STRING("overshoot", s.failure());
    assertOff(s, 177);
}

void test_at_abort_no_heat_up() {
    Sequence s;
    Config c = onePoint(177);
    s.start(c, 25);
    float t = 0;
    while (s.running() && t < 1000) {
        const float u = s.update(25 + t * 0.01f); // 0.6 C/min: stalled
        t += 0.25f;
        if (s.running())
            TEST_ASSERT_EQUAL_FLOAT(c.outputMax, u);
    }
    TEST_ASSERT_EQUAL_STRING("no heat-up", s.failure());
    TEST_ASSERT_FLOAT_WITHIN(1, c.stallSeconds, t);
    assertOff(s, 30);
}

void test_at_abort_no_oscillation() {
    Sequence s;
    Config c = onePoint(177);
    s.start(c, 25);
    float t = 0;
    while (s.running() && t < 3000) {
        s.update(172); // never crosses the band: the heater cannot reach the point
        t += 0.25f;
    }
    TEST_ASSERT_EQUAL_STRING("no oscillation", s.failure());
    TEST_ASSERT_FLOAT_WITHIN(1, c.cycleTimeoutS, t);
    assertOff(s, 172);
}

void test_at_abort_time_limit() {
    Drum p;
    Sequence s;
    Config c;
    c.totalTimeoutS = 600;
    s.start(c, 25);
    const float t = runToEnd(s, p, 25);
    TEST_ASSERT_EQUAL_STRING("time limit", s.failure());
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 600, t);
    assertOff(s, 150);
}

void test_at_abort_command() {
    Drum p;
    Sequence s;
    s.start(Config(), 25);
    float pv = 25;
    for (int i = 0; i < 2000; i++)
        pv = p.step(s.update(pv));
    s.abort("stop");
    TEST_ASSERT_EQUAL(Phase::Aborted, s.phase());
    TEST_ASSERT_EQUAL_STRING("stop", s.failure());
    assertOff(s, pv);
    s.abort("again"); // no effect once ended
    TEST_ASSERT_EQUAL_STRING("stop", s.failure());
}

void test_at_refuses_hot_start_and_empty_plan() {
    Sequence s;
    Config c = onePoint(150);
    s.start(c, 150 + c.maxOvershootC + 1);
    TEST_ASSERT_EQUAL(Phase::Failed, s.phase());
    TEST_ASSERT_EQUAL_STRING("too hot to start", s.failure());
    TEST_ASSERT_EQUAL_FLOAT(0, s.update(150));
    Config none;
    none.pointCount = 0;
    s.start(none, 25);
    TEST_ASSERT_EQUAL_STRING("no test points", s.failure());
    TEST_ASSERT_EQUAL_FLOAT(0, s.update(25));
}

// Cycles that never agree: every point is unsettled and the run fails without
// a recommendation.
void test_at_inconsistent_cycles_fail() {
    Drum p;
    Sequence s;
    Config c;
    c.maxCv = 0.0001f;
    s.start(c, 25);
    runToEnd(s, p, 25);
    TEST_ASSERT_EQUAL(Phase::Failed, s.phase());
    TEST_ASSERT_EQUAL_STRING("no consistent oscillation", s.failure());
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(PointStatus::Unsettled, s.point(i).status);
        TEST_ASSERT_EQUAL(c.maxCycles, s.point(i).cycles);
    }
}

void test_at_make_plan() {
    Config c;
    c.abortAboveC = 270;
    c.maxOvershootC = 30;
    const float req[] = {204, 0, 149, 300, 151};
    TEST_ASSERT_EQUAL(3, makePlan(req, 5, 100, 250, c));
    TEST_ASSERT_EQUAL_FLOAT(149, c.pointsC[0]); // sorted; 151 dropped (within 5 C)
    TEST_ASSERT_EQUAL_FLOAT(204, c.pointsC[1]);
    TEST_ASSERT_EQUAL_FLOAT(240, c.pointsC[2]); // 300 clamped under 270 - 30
    TEST_ASSERT_EQUAL_FLOAT(0, c.pointsC[3]);
    const float low[] = {50};
    TEST_ASSERT_EQUAL(1, makePlan(low, 1, 100, 250, c));
    TEST_ASSERT_EQUAL_FLOAT(100, c.pointsC[0]);
    const float none[] = {0, NAN};
    TEST_ASSERT_EQUAL(0, makePlan(none, 2, 100, 250, c));
}

void test_at_rules() {
    const Model none{NAN, NAN, NAN};
    PidGains g = Sequence::gainsFor(Rule::ZieglerNicholsPi, 4, 120, none);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 1.8f, g.kp);
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 1.8f / 100, g.ki);
    TEST_ASSERT_EQUAL_FLOAT(0, g.kd);
    g = Sequence::gainsFor(Rule::TyreusLuybenPid, 4.4f, 120, none);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 2.0f, g.kp);
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 2.0f / 264, g.ki);
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 2.0f * 120 / 6.3f, g.kd);
    g = Sequence::gainsFor(Rule::TyreusLuybenPi, 3.22f, 100, none);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 1.0f, g.kp);
    const Model m{3, 300, 30};
    g = Sequence::gainsFor(Rule::SimcPi, 4, 120, m);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 300.0f / (2 * 3 * 30), g.kp); // tau / (2 K L)
    TEST_ASSERT_FLOAT_WITHIN(1e-5, g.kp / 240, g.ki);           // min(tau, 8 L)
    // A model the ultimate point cannot support is reported as unknown.
    const Model bad = Sequence::fitModel(0.1f, 100, 177, 25, 50);
    TEST_ASSERT_TRUE(std::isnan(bad.tauS));
}

void test_at_estimate() {
    Config c;
    const float cold = Sequence::estimateSeconds(c, 25);
    const float warm = Sequence::estimateSeconds(c, 140);
    TEST_ASSERT_TRUE(cold > warm && warm > 0);
    TEST_ASSERT_TRUE(cold <= c.totalTimeoutS);
    // Simulated drum: the estimate from the measured rate and period ends near
    // the real finish once the first point is measured.
    Drum p;
    Sequence s;
    s.start(c, 25);
    float pv = 25, t = 0, predicted = -1;
    while (s.running()) {
        pv = p.step(s.update(pv));
        t += 0.25f;
        if (predicted < 0 && s.progress().point == 1)
            predicted = t + s.progress().remainingS;
    }
    TEST_ASSERT_FLOAT_WITHIN(0.3f * t, t, predicted);
}

void test_at_status_text() {
    Snapshot s;
    s.mode = Mode::Autotune;
    s.autotune = AutotuneStatus::Running;
    s.autotuneProgress.phase = Phase::Oscillating;
    s.autotuneProgress.point = 1;
    s.autotuneProgress.pointCount = 3;
    s.autotuneProgress.cycle = 3;
    s.autotuneProgress.cycleOf = 6;
    s.autotuneProgress.targetC = 176.67f;
    char b[48];
    autotunetext::full(s, 'F', b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("running: cycle 3/6 at 350 F (2/3)", b);
    autotunetext::brief(s, 'F', b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("AT2/3 cyc3/6 350F", b);
    s.autotuneProgress.phase = Phase::Heating;
    autotunetext::full(s, 'F', b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("running: heating to 350 F (2/3)", b);
    autotunetext::brief(s, 'C', b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("AT2/3 heat 177C", b);
    s.autotuneProgress.phase = Phase::Settling;
    s.autotuneProgress.cycle = 1;
    s.autotuneProgress.cycleOf = 2;
    autotunetext::full(s, 'F', b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("running: settling 1/2 at 350 F (2/3)", b);
    s.autotune = AutotuneStatus::Pending;
    autotunetext::full(s, 'F', b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("results ready", b);
    autotunetext::brief(s, 'F', b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("AT results ready", b);
    s.autotune = AutotuneStatus::Discarded;
    autotunetext::full(s, 'F', b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("discarded", b);
    s.autotune = AutotuneStatus::Failed;
    s.autotuneReason = "no oscillation";
    autotunetext::brief(s, 'F', b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("AT fail: no cycle", b);
    // Worst-case lengths fit the header.
    s.autotune = AutotuneStatus::Running;
    s.autotuneProgress.phase = Phase::Oscillating;
    s.autotuneProgress.cycle = 12;
    s.autotuneProgress.cycleOf = 12;
    s.autotuneProgress.targetC = 240;
    autotunetext::brief(s, 'F', b, sizeof b);
    TEST_ASSERT_TRUE(std::strlen(b) <= 20);
    TEST_ASSERT_EQUAL_STRING("AT2/3 cyc12/12 464F", b);
}

void test_at_protocol() {
    Command c;
    JsonDocument in;
    in["autotune"] = "apply";
    TEST_ASSERT_TRUE(autotuneproto::parseAction(in["autotune"], c));
    TEST_ASSERT_EQUAL(Command::Kind::ApplyAutotune, c.kind);
    in["autotune"] = "discard";
    TEST_ASSERT_TRUE(autotuneproto::parseAction(in["autotune"], c));
    TEST_ASSERT_EQUAL(Command::Kind::DiscardAutotune, c.kind);
    in["autotune"] = "start";
    TEST_ASSERT_FALSE(autotuneproto::parseAction(in["autotune"], c));
    in["autotune"] = 1;
    TEST_ASSERT_FALSE(autotuneproto::parseAction(in["autotune"], c));

    Drum p;
    Sequence seq;
    Config cfg;
    seq.start(cfg, 25);
    runToEnd(seq, p, 25);
    TEST_ASSERT_EQUAL(Phase::Done, seq.phase());
    Report r;
    seq.fillReport(r);
    Snapshot s;
    s.autotune = AutotuneStatus::Pending;
    s.autotunePending = true;
    s.gains = {1, 0.1f, 0.01f};
    JsonDocument doc;
    autotuneproto::writeReport(s, r, cfg, 1800, 'F', doc.to<JsonObject>());
    TEST_ASSERT_EQUAL_STRING("results ready", doc["status"]);
    TEST_ASSERT_TRUE(doc["pending"].as<bool>());
    TEST_ASSERT_FALSE(doc["running"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("done", doc["phase"]);
    TEST_ASSERT_EQUAL(300, doc["plan"]["points"][0].as<int>());
    TEST_ASSERT_EQUAL(400, doc["plan"]["points"][2].as<int>());
    TEST_ASSERT_EQUAL(1800, doc["plan"]["estimate"].as<int>());
    TEST_ASSERT_EQUAL_STRING("ziegler-nichols-pi", doc["plan"]["rule"]);
    TEST_ASSERT_TRUE(doc["progress"].isNull());
    TEST_ASSERT_EQUAL(3, doc["results"].size());
    TEST_ASSERT_EQUAL(300, doc["results"][0]["target"].as<int>());
    TEST_ASSERT_EQUAL_STRING("ok", doc["results"][0]["status"]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, r.points[0].amplitudeC * 1.8f, doc["results"][0]["amplitude"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f * r.recommended.kp, r.recommended.kp, doc["recommended"]["kp"].as<float>());
    TEST_ASSERT_EQUAL(3, doc["recommended"]["used"].as<int>());
    TEST_ASSERT_EQUAL_FLOAT(1, doc["current"]["kp"].as<float>());
    const JsonArrayConst log = doc["log"].as<JsonArrayConst>();
    TEST_ASSERT_TRUE(log.size() > 6);
    TEST_ASSERT_TRUE(log.size() <= (size_t)kLogSize);
    const std::string last = log[log.size() - 1]["text"].as<const char*>();
    TEST_ASSERT_EQUAL_STRING("done: Kp", last.substr(0, 8).c_str());
    // While running: progress in the display unit.
    Sequence run;
    run.start(cfg, 25);
    Drum q;
    float pv = 25;
    for (int i = 0; i < 400; i++)
        pv = q.step(run.update(pv));
    run.fillReport(r);
    s.mode = Mode::Autotune;
    s.autotune = AutotuneStatus::Running;
    s.autotunePending = false;
    s.autotuneProgress = run.progress();
    doc.clear();
    autotuneproto::writeReport(s, r, cfg, 1800, 'F', doc.to<JsonObject>());
    TEST_ASSERT_TRUE(doc["running"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("heating", doc["phase"]);
    TEST_ASSERT_EQUAL(300, doc["progress"]["target"].as<int>());
    TEST_ASSERT_EQUAL(0, doc["results"].size());
    TEST_ASSERT_TRUE(doc["recommended"].isNull());
    TEST_ASSERT_EQUAL_STRING("point 1: heating to 300 F", doc["log"][0]["text"]);
}

void runAutotuneTests() {
    RUN_TEST(test_at_fopdt_ultimate_point);
    RUN_TEST(test_at_multi_point_run);
    RUN_TEST(test_at_abort_sensor_fault);
    RUN_TEST(test_at_abort_over_temperature);
    RUN_TEST(test_at_abort_overshoot);
    RUN_TEST(test_at_abort_no_heat_up);
    RUN_TEST(test_at_abort_no_oscillation);
    RUN_TEST(test_at_abort_time_limit);
    RUN_TEST(test_at_abort_command);
    RUN_TEST(test_at_refuses_hot_start_and_empty_plan);
    RUN_TEST(test_at_inconsistent_cycles_fail);
    RUN_TEST(test_at_make_plan);
    RUN_TEST(test_at_rules);
    RUN_TEST(test_at_estimate);
    RUN_TEST(test_at_status_text);
    RUN_TEST(test_at_protocol);
}
