#include "recipe.h"

#include <cstring>

namespace recipe {

namespace {
const char* const kEventNames[] = {"CHARGE", "TP", "DRY_END", "FC_START", "FC_END", "SC_START", "SC_END", "DROP"};
static_assert(sizeof(kEventNames) / sizeof(kEventNames[0]) == static_cast<size_t>(Event::Count), "event names");

bool eq(const char* a, const char* b) {
    return a && b && std::strcmp(a, b) == 0;
}
size_t utf8Length(const char* s) {
    size_t n = 0;
    for (; *s; s++)
        if ((static_cast<unsigned char>(*s) & 0xC0) != 0x80)
            n++;
    return n;
}
// A temperature difference in `unit` to C.
float deltaToC(float d, char unit) {
    return unit == 'F' ? d * 5.0f / 9.0f : d;
}
bool number(JsonVariantConst v, float& out) {
    if (!v.is<float>())
        return false;
    out = v.as<float>();
    return std::isfinite(out);
}
bool percent(JsonVariantConst v, float& out) {
    return number(v, out) && out >= 0 && out <= 100;
}

const char* parseTrigger(JsonVariantConst j, char unit, Trigger& t) {
    if (!j.is<JsonObjectConst>())
        return "trigger missing";
    const char* type = j["type"];
    const char* op = j["op"] | "gte";
    if (eq(op, "gte"))
        t.op = Op::Gte;
    else if (eq(op, "lte"))
        t.op = Op::Lte;
    else if (eq(op, "near"))
        t.op = Op::Near;
    else
        return "trigger op must be gte, lte or near";

    if (eq(type, "event")) {
        t.type = TriggerType::Event;
        if (!parseEvent(j["value"].as<const char*>(), t.event))
            return "unknown event";
        return "";
    }
    float v = 0;
    if (!number(j["value"], v))
        return "trigger value missing";
    float defTol = 0, maxTol = 0;
    if (eq(type, "time")) {
        t.type = TriggerType::Time;
        if (!(v >= 0 && v <= kMaxTimeS))
            return "time trigger must be 0..3600 s";
        t.value = v;
        defTol = 5;
        maxTol = 60;
        if (!j["tol"].isNull()) {
            if (!number(j["tol"], t.tol) || !(t.tol > 0 && t.tol <= maxTol))
                return "tolerance out of range";
        } else {
            t.tol = defTol;
        }
        return "";
    }
    if (eq(type, "bt") || eq(type, "et")) {
        t.type = eq(type, "bt") ? TriggerType::Bt : TriggerType::Et;
        t.value = toCelsius(v, unit);
        if (!(t.value >= kMinTempC && t.value <= kMaxTempC))
            return "temperature trigger must be 0..350 C";
        defTol = 2;
        maxTol = 20;
    } else if (eq(type, "ror")) {
        t.type = TriggerType::Ror;
        t.value = deltaToC(v, unit);
        if (!(t.value >= kMinRorC && t.value <= kMaxRorC))
            return "RoR trigger must be -50..100 C/min";
        defTol = 1;
        maxTol = 20;
    } else {
        return "trigger type must be time, bt, et, ror or event";
    }
    if (!j["tol"].isNull()) {
        float tol = 0;
        if (!number(j["tol"], tol))
            return "tolerance out of range";
        t.tol = deltaToC(tol, unit);
        if (!(t.tol > 0 && t.tol <= maxTol))
            return "tolerance out of range";
    } else {
        t.tol = defTol;
    }
    return "";
}

const char* parseAction(JsonVariantConst j, char unit, Action& a) {
    if (!j.is<JsonObjectConst>())
        return "action must be an object";
    const char* type = j["type"];
    if (eq(type, "power") || eq(type, "fan")) {
        a.type = eq(type, "power") ? ActionType::Power : ActionType::Fan;
        return percent(j["value"], a.value) ? "" : "power and fan must be 0..100 %";
    }
    if (eq(type, "setpoint")) {
        a.type = ActionType::Setpoint;
        float v = 0;
        if (!number(j["value"], v))
            return "setpoint value missing";
        a.value = toCelsius(v, unit);
        if (!(a.value >= kMinTempC && a.value <= kMaxTempC))
            return "setpoint must be 0..350 C";
        if (!j["ramp"].isNull() && (!number(j["ramp"], a.rampS) || !(a.rampS >= 0 && a.rampS <= kMaxRampS)))
            return "ramp must be 0..3600 s";
        return "";
    }
    if (eq(type, "hold")) {
        a.type = ActionType::Hold;
        return "";
    }
    if (eq(type, "cool")) {
        a.type = ActionType::Cool;
        return "";
    }
    if (eq(type, "end")) {
        a.type = ActionType::End;
        return "";
    }
    if (eq(type, "note")) {
        a.type = ActionType::Note;
        if (!j["text"].is<const char*>() || utf8Length(j["text"].as<const char*>()) > kMaxNoteChars)
            return "note text must be up to 80 characters";
        return "";
    }
    return "action type must be power, fan, setpoint, hold, cool, end or note";
}
} // namespace

bool parseEvent(const char* s, Event& out) {
    for (size_t i = 0; i < static_cast<size_t>(Event::Count); i++)
        if (eq(s, kEventNames[i])) {
            out = static_cast<Event>(i);
            return true;
        }
    return false;
}

const char* eventName(Event e) {
    return static_cast<size_t>(e) < static_cast<size_t>(Event::Count) ? kEventNames[static_cast<size_t>(e)] : "";
}

const char* parse(JsonObjectConst j, char unit, Recipe& out) {
    if (j.isNull())
        return "recipe must be an object";
    const char* mode = j["mode"] | "auto";
    if (!eq(mode, "auto") && !eq(mode, "guided"))
        return "mode must be auto or guided";
    out.guided = eq(mode, "guided");
    JsonArrayConst steps = j["steps"].as<JsonArrayConst>();
    if (steps.isNull() || steps.size() == 0 || steps.size() > kMaxSteps)
        return "a recipe has 1 to 32 steps";
    out.count = 0;
    for (JsonVariantConst s : steps) {
        if (!s.is<JsonObjectConst>())
            return "step must be an object";
        Step& st = out.steps[out.count];
        st = Step();
        const char* e = parseTrigger(s["trigger"], unit, st.trigger);
        if (*e)
            return e;
        JsonVariantConst any = s["anyOrder"];
        if (!any.isNull() && !any.is<bool>())
            return "anyOrder must be true or false";
        st.anyOrder = any | false;
        JsonVariantConst label = s["label"];
        if (!label.isNull() && (!label.is<const char*>() || utf8Length(label.as<const char*>()) > kMaxLabelChars))
            return "label must be up to 40 characters";
        JsonArrayConst actions = s["actions"].as<JsonArrayConst>();
        if (actions.isNull() || actions.size() == 0 || actions.size() > kMaxActions)
            return "a step has 1 to 4 actions";
        for (JsonVariantConst a : actions) {
            e = parseAction(a, unit, st.actions[st.actionCount]);
            if (*e)
                return e;
            st.actionCount++;
        }
        out.count++;
    }
    return "";
}

void RoastClock::mark(Event e, uint32_t nowMs) {
    if (e == Event::Charge) {
        mask_ = 0;
        chargeMs_ = nowMs;
    }
    mask_ |= 1u << static_cast<int>(e);
    atS_[static_cast<int>(e)] = sinceCharge(nowMs);
}

void RoastClock::undo(Event e) {
    if (e == Event::Charge)
        mask_ = 0; // without CHARGE there is no roast clock
    else
        mask_ &= ~(1u << static_cast<int>(e));
}

float RoastClock::sinceCharge(uint32_t nowMs) const {
    return charged() ? (nowMs - chargeMs_) / 1000.0f : NAN;
}

float RoastClock::at(Event e) const {
    return seen(e) ? atS_[static_cast<int>(e)] : NAN;
}

const char* stateName(State s) {
    switch (s) {
    case State::Ready:
        return "ready";
    case State::Running:
        return "running";
    case State::Done:
        return "done";
    case State::Aborted:
        return "aborted";
    default:
        return "empty";
    }
}

const char* outcomeName(Outcome o) {
    switch (o) {
    case Outcome::Prompted:
        return "prompted";
    case Outcome::Confirmed:
        return "confirmed";
    case Outcome::Skipped:
        return "skipped";
    case Outcome::Missed:
        return "missed";
    default:
        return "applied";
    }
}

bool Runner::load(const Recipe& r) {
    if (state_ == State::Running || r.count == 0 || r.count > kMaxSteps)
        return false;
    recipe_ = r;
    state_ = State::Ready;
    reason_ = "";
    firedCount_ = 0;
    prompt_ = -1;
    rampActive_ = false;
    seq_++;
    return true;
}

void Runner::clear() {
    if (state_ == State::Running)
        finish(State::Aborted, "cleared");
    recipe_.count = 0;
    state_ = State::Empty;
    seq_++;
}

bool Runner::start() {
    if (state_ == State::Empty || state_ == State::Running)
        return false;
    for (size_t i = 0; i < kMaxSteps; i++)
        done_[i] = seenFalse_[i] = false;
    firedCount_ = 0;
    prompt_ = -1;
    rampActive_ = false;
    pendingEnd_ = false;
    followSetpoint_ = false;
    reason_ = "";
    state_ = State::Running;
    seq_++;
    return true;
}

void Runner::finish(State s, const char* reason) {
    state_ = s;
    reason_ = reason;
    rampActive_ = false;
    if (prompt_ >= 0) { // a prompt left open is not applied
        for (size_t i = firedCount_; i-- > 0;)
            if (log_[i].step == prompt_ && log_[i].outcome == Outcome::Prompted) {
                log_[i].outcome = Outcome::Missed;
                break;
            }
        prompt_ = -1;
    }
    seq_++;
}

void Runner::abort(const char* reason) {
    if (state_ == State::Running)
        finish(State::Aborted, reason);
}

int Runner::next() const {
    for (size_t i = 0; i < recipe_.count; i++)
        if (!done_[i] && !recipe_.steps[i].anyOrder)
            return static_cast<int>(i);
    return -1;
}

Runner::Cond Runner::evaluate(const Trigger& t, const Inputs& in, const RoastClock& clock) const {
    if (t.type == TriggerType::Event)
        return clock.seen(t.event) ? Cond::True : Cond::False;
    float x = NAN;
    switch (t.type) {
    case TriggerType::Time:
        if (!clock.charged())
            return Cond::False; // the roast clock has not started
        x = clock.sinceCharge(in.nowMs);
        break;
    case TriggerType::Bt:
        x = in.btC;
        break;
    case TriggerType::Et:
        x = in.etC;
        break;
    case TriggerType::Ror:
    default:
        x = in.rorC;
        break;
    }
    if (!std::isfinite(x))
        return Cond::Unknown; // no reading: neither arms nor fires
    bool c = false;
    switch (t.op) {
    case Op::Gte:
        c = x >= t.value;
        break;
    case Op::Lte:
        c = x <= t.value;
        break;
    case Op::Near:
    default:
        c = std::fabs(x - t.value) <= t.tol;
        break;
    }
    return c ? Cond::True : Cond::False;
}

void Runner::record(uint8_t i, float atS, Outcome o) {
    if (firedCount_ < kMaxSteps)
        log_[firedCount_++] = Fired{i, atS, o};
    seq_++;
}

void Runner::apply(const Step& s, const Inputs& in, Output& out) {
    for (uint8_t k = 0; k < s.actionCount; k++) {
        const Action& a = s.actions[k];
        switch (a.type) {
        case ActionType::Power:
            rampActive_ = false;
            followSetpoint_ = false;
            out.add(Command::Kind::SetMode, static_cast<float>(Mode::Manual));
            out.add(Command::Kind::SetHeater, fmaxf(0, fminf(100, a.value)));
            break;
        case ActionType::Fan:
            out.add(Command::Kind::SetFan, fmaxf(0, fminf(100, a.value)));
            break;
        case ActionType::Setpoint:
            out.add(Command::Kind::SetMode, static_cast<float>(Mode::Pid));
            followSetpoint_ = true;
            rampToC_ = a.value;
            if (a.rampS > 0) {
                rampActive_ = true;
                rampFromC_ = in.setpointC;
                rampToC_ = a.value;
                rampS_ = a.rampS;
                rampStartMs_ = rampSentMs_ = in.nowMs;
                out.add(Command::Kind::SetSetpoint, rampFromC_);
            } else {
                rampActive_ = false;
                out.add(Command::Kind::SetSetpoint, a.value);
            }
            break;
        case ActionType::Hold:
            rampActive_ = false;
            break;
        case ActionType::Cool:
            out.stop = true;
            pendingEnd_ = true;
            break;
        case ActionType::End:
            pendingEnd_ = true;
            break;
        case ActionType::Note:
        default:
            break;
        }
    }
}

void Runner::fire(uint8_t i, const Inputs& in, const RoastClock& clock, Output& out) {
    done_[i] = true;
    const float at = clock.sinceCharge(in.nowMs);
    if (recipe_.guided) {
        if (prompt_ >= 0) // an unanswered prompt is replaced by the newer one
            for (size_t k = firedCount_; k-- > 0;)
                if (log_[k].step == prompt_ && log_[k].outcome == Outcome::Prompted) {
                    log_[k].outcome = Outcome::Missed;
                    break;
                }
        prompt_ = i;
        record(i, at, Outcome::Prompted);
        return;
    }
    record(i, at, Outcome::Applied);
    apply(recipe_.steps[i], in, out);
}

void Runner::tick(const Inputs& in, const RoastClock& clock, Output& out) {
    if (state_ != State::Running)
        return;
    if (in.trip != SafetyTrip::None) {
        finish(State::Aborted, tripName(in.trip));
        return;
    }
    Cond cond[kMaxSteps];
    for (size_t i = 0; i < recipe_.count; i++) {
        cond[i] = done_[i] ? Cond::Unknown : evaluate(recipe_.steps[i].trigger, in, clock);
        if (cond[i] == Cond::False)
            seenFalse_[i] = true;
    }
    // In order: the next step, and again after it fires (the one after may already be true).
    for (int n = next(); n >= 0 && !pendingEnd_; n = next()) {
        if (!(seenFalse_[n] && cond[n] == Cond::True))
            break;
        fire(static_cast<uint8_t>(n), in, clock, out);
    }
    for (size_t i = 0; i < recipe_.count && !pendingEnd_; i++)
        if (recipe_.steps[i].anyOrder && !done_[i] && seenFalse_[i] && cond[i] == Cond::True)
            fire(static_cast<uint8_t>(i), in, clock, out);

    if (rampActive_ && in.nowMs - rampSentMs_ >= kRampEveryMs) {
        const float p = fminf(1, (in.nowMs - rampStartMs_) / 1000.0f / rampS_);
        out.add(Command::Kind::SetSetpoint, rampFromC_ + (rampToC_ - rampFromC_) * p);
        rampSentMs_ = in.nowMs;
        if (p >= 1)
            rampActive_ = false;
    }
    if (pendingEnd_) {
        finish(State::Done, out.stop ? "cool" : "end");
        return;
    }
    bool all = true;
    for (size_t i = 0; i < recipe_.count; i++)
        all = all && done_[i];
    if (all && !rampActive_ && prompt_ < 0)
        finish(State::Done, "complete");
}

bool Runner::confirm(uint8_t step, const Inputs& in, Output& out) {
    if (state_ != State::Running || prompt_ != step)
        return false;
    prompt_ = -1;
    for (size_t k = firedCount_; k-- > 0;)
        if (log_[k].step == step && log_[k].outcome == Outcome::Prompted) {
            log_[k].outcome = Outcome::Confirmed;
            break;
        }
    seq_++;
    apply(recipe_.steps[step], in, out);
    if (pendingEnd_)
        finish(State::Done, out.stop ? "cool" : "end");
    return true;
}

bool Runner::dismiss(uint8_t step) {
    if (state_ != State::Running || prompt_ != step)
        return false;
    prompt_ = -1;
    for (size_t k = firedCount_; k-- > 0;)
        if (log_[k].step == step && log_[k].outcome == Outcome::Prompted) {
            log_[k].outcome = Outcome::Skipped;
            break;
        }
    seq_++;
    return true;
}

bool Runner::skipNext(const RoastClock& clock, uint32_t nowMs) {
    const int n = next();
    if (state_ != State::Running || n < 0)
        return false;
    done_[n] = true;
    record(static_cast<uint8_t>(n), clock.sinceCharge(nowMs), Outcome::Skipped);
    return true;
}

} // namespace recipe
