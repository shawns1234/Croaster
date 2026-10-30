// Autotune panel logic: turns the board's getAutotune reply into the texts, step
// list and result rows the page shows. Shared by the page and the host tests
// (node croaster-webui/test). Pure functions only: no DOM, no network.
// Reply format: croaster-control/src/core/autotune_protocol.h.
(function (root, factory) {
  'use strict';
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.Autotune = api;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const isNum = (v) => typeof v === 'number' && isFinite(v);
  const deg = (unit) => (unit === 'K' ? ' K' : ' °' + (unit || 'C'));

  // "250, 300 and 350 °F"
  function pointList(points, unit) {
    const p = (points || []).map((v) => String(Math.round(v)));
    if (!p.length) return '';
    const head = p.length > 1 ? p.slice(0, -1).join(', ') + ' and ' + p[p.length - 1] : p[0];
    return head + deg(unit);
  }

  // Whole minutes, rounded up to 5 above 10 min so the figure does not look exact.
  function minutes(seconds) {
    if (!isNum(seconds) || seconds <= 0) return null;
    const m = seconds / 60;
    return m <= 10 ? Math.max(1, Math.ceil(m)) : Math.ceil(m / 5) * 5;
  }

  function confirmText(rep) {
    const plan = (rep && rep.plan) || {};
    const n = minutes(plan.estimate);
    const pts = pointList(plan.points, rep && rep.unit);
    return 'Autotune heats the roaster to a few test temperatures' + (pts ? ' (' + pts + ')' : '') +
      ' and measures how it responds, then calculates PID settings for your machine. It takes about ' +
      (n ? n + ' minutes' : 'an hour') + '; you can stop it any time, and nothing is changed until you apply the results.\n\n' +
      'Run it with the drum empty, turning, and the fan at your usual roast speed.';
  }

  // Step list: heat, settle and measure for each test point, then calculate.
  // State per step: 'done', 'active', 'pending' or 'failed'.
  function steps(rep) {
    if (!rep || !rep.plan) return [];
    const pts = rep.plan.points || [];
    const u = deg(rep.unit);
    const g = rep.progress;
    const results = rep.results || [];
    const out = [];
    const order = { heating: 0, settling: 1, oscillating: 2, computing: 3 };
    const ended = rep.phase === 'done' || rep.phase === 'failed' || rep.phase === 'aborted';
    pts.forEach((t, i) => {
      const T = Math.round(t) + u;
      ['Heat to ' + T, 'Settle at ' + T, 'Measure at ' + T].forEach((label, k) => {
        let state = 'pending';
        if (rep.running && g) {
          const at = Math.min(order[rep.phase] !== undefined ? order[rep.phase] : 0, 2);
          if (i < g.point || (i === g.point && k < at)) state = 'done';
          else if (i === g.point && k === at) state = rep.phase === 'computing' ? 'done' : 'active';
        } else if (ended && (rep.phase === 'done' || i < results.length)) {
          state = 'done';
        }
        let detail = '';
        if (state === 'active' && k > 0 && g.cycleOf) detail = 'cycle ' + g.cycle + (k === 2 ? ' of up to ' : ' of ') + g.cycleOf;
        if (k === 2 && results[i]) detail = results[i].status;
        out.push({ label, state, detail });
      });
    });
    const calc = { label: 'Calculate gains', state: 'pending', detail: '' };
    if (rep.phase === 'done') calc.state = 'done';
    else if (rep.running && rep.phase === 'computing' && g && g.point === pts.length - 1) calc.state = 'active';
    out.push(calc);
    if (ended && rep.phase !== 'done') {
      const at = out.findIndex((s) => s.state === 'pending'); // the step the run stopped in
      if (at >= 0) out[at].state = 'failed';
    }
    return out;
  }

  // 0..1 from the board's elapsed and remaining estimate.
  function fraction(rep) {
    if (!rep) return 0;
    if (rep.phase === 'done') return 1;
    const g = rep.progress;
    if (!g || !isNum(g.elapsed) || !isNum(g.remaining)) return 0;
    const total = g.elapsed + g.remaining;
    return total > 0 ? Math.min(1, g.elapsed / total) : 0;
  }

  function clock(s) {
    if (!isNum(s)) return '';
    s = Math.max(0, Math.round(s));
    return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
  }

  // One line under the progress bar.
  function progressText(rep) {
    if (!rep) return '';
    const g = rep.progress;
    if (rep.running && g) {
      const left = minutes(g.remaining);
      return 'Point ' + (g.point + 1) + ' of ' + g.points + ' · ' + clock(g.elapsed) + ' elapsed' +
        (left ? ' · about ' + left + ' min left' : '');
    }
    if (rep.phase === 'done') return rep.pending ? 'Finished. Review the results and save or discard them.' : 'Finished.';
    if (rep.phase === 'failed') return 'Stopped: ' + failureText(rep.failure, rep) + '.';
    if (rep.phase === 'aborted') return 'Stopped' + (rep.failure ? ': ' + failureText(rep.failure, rep) : '') + '.';
    return '';
  }

  const FAILURES = {
    'sensor fault': 'the probe stopped reading',
    'over temperature': 'the temperature passed the safety limit',
    overshoot: 'the temperature overshot the test point too far',
    'time limit': 'the run took longer than its time limit',
    'no heat-up': 'the temperature did not rise; check the heater and mains',
    'no oscillation': 'the temperature did not cycle around the test point',
    'no consistent oscillation': 'no test point gave steady cycles; nothing to calculate from',
    'too hot to start': 'the roaster is too hot to start; let it cool below the first test point',
    'no test points': 'no test points are configured',
    stopped: 'stopped by the user',
    'setpoint changed': 'the setpoint was changed',
  };
  function failureText(code, rep) {
    let t = FAILURES[code] || code || 'unknown reason';
    if (code === 'too hot to start' && rep && rep.plan && rep.plan.points && rep.plan.points.length)
      t = 'the roaster is too hot to start; let it cool toward ' + Math.round(rep.plan.points[0]) + deg(rep.unit) + ' first';
    return t;
  }

  // Replies refusing a start because the other one is running.
  const REFUSALS = {
    recipeRunning: 'A recipe is running. Abort it before starting autotune.',
    autotuneRunning: 'Autotune is running. Stop it before running a recipe.',
  };
  function refusal(code) {
    return REFUSALS[code] || code;
  }

  function fmtGain(v) {
    if (!isNum(v)) return '—';
    if (v === 0) return '0';
    return String(Number(v.toPrecision(4)));
  }

  // Per-point table rows, in the reply's unit.
  function resultRows(rep) {
    const u = deg(rep && rep.unit).trim();
    return ((rep && rep.results) || []).map((r) => ({
      target: Math.round(r.target) + ' ' + u,
      status: r.status,
      cycles: String(r.cycles),
      swing: isNum(r.amplitude) ? r.amplitude.toFixed(1) + ' ' + u : '—',
      period: isNum(r.period) ? Math.round(r.period) + ' s' : '—',
      ku: fmtGain(r.ku), tu: isNum(r.tu) ? Math.round(r.tu) + ' s' : '—',
      kp: fmtGain(r.kp), ki: fmtGain(r.ki), kd: fmtGain(r.kd),
    }));
  }

  // Current vs recommended gains for the results dialog; null until a result is pending.
  function gainRows(rep) {
    if (!rep || !rep.pending || !rep.recommended) return null;
    const cur = rep.current || {};
    const rec = rep.recommended;
    return ['kp', 'ki', 'kd'].map((k) => ({ name: 'K' + k.slice(1), current: fmtGain(cur[k]), next: fmtGain(rec[k]) }));
  }

  const RULES = {
    'ziegler-nichols-pi': 'Ziegler–Nichols PI', 'tyreus-luyben-pi': 'Tyreus–Luyben PI',
    'tyreus-luyben-pid': 'Tyreus–Luyben PID', 'simc-pi': 'SIMC PI', 'ziegler-nichols': 'Ziegler–Nichols PID',
  };
  function ruleName(r) { return RULES[r] || r || ''; }

  // How often to ask for the report: fast while running or waiting for a choice.
  function pollMs(rep) {
    if (rep && rep.running) return 3000;
    if (rep && rep.pending) return 10000;
    return 0; // idle: fetch only when the telemetry status changes
  }

  return { pointList, minutes, confirmText, steps, fraction, clock, progressText, failureText, refusal,
    fmtGain, resultRows, gainRows, ruleName, pollMs };
});
