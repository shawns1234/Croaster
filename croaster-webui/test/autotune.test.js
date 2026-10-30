// Host tests for web/autotune.js (autotune panel texts and step list).
// Run: node --test croaster-webui/test/*.test.js
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const A = require('../web/autotune.js');

const plan = { points: [300, 350, 400], settleCycles: 2, minCycles: 3, maxCycles: 6, maxCv: 10,
  rule: 'ziegler-nichols-pi', estimate: 3300, timeLimit: 5400 };
const running = (phase, point, cycle) => ({ status: 'running', unit: 'F', running: true, pending: false, phase, failure: '', plan,
  progress: { point, points: 3, cycle, cycleOf: phase === 'settling' ? 2 : 6, target: plan.points[point], output: 40,
    amplitude: 6, period: 130, elapsed: 600, remaining: 1800 },
  current: { kp: 1, ki: 0.1, kd: 0.01 }, results: [], log: [] });
const result = (target, status) => ({ target, status, cycles: 4, amplitude: 6.1, period: 131, amplitudeCv: 0.03,
  periodCv: 0.02, bias: 30, step: 30, ku: 4.3, tu: 132, model: { gain: 3.4, tau: 300, dead: 35 }, kp: 1.9, ki: 0.017, kd: 0 });
const done = () => Object.assign(running('done', 2, 0), { running: false, pending: true, progress: undefined,
  results: [result(300, 'ok'), result(350, 'ok'), result(400, 'unsettled')],
  recommended: { kp: 1.82, ki: 0.0161, kd: 0, used: 2, rule: 'ziegler-nichols-pi' } });

test('confirm text names the points, the time and the drum load', () => {
  const t = A.confirmText({ unit: 'F', plan });
  assert.match(t, /^Autotune heats the roaster to a few test temperatures \(300, 350 and 400 °F\) and measures how it responds/);
  assert.match(t, /It takes about 55 minutes; you can stop it any time, and nothing is changed until you apply the results\./);
  assert.match(t, /drum empty, turning, and the fan at your usual roast speed/);
  assert.match(A.confirmText(null), /about an hour/);
});

test('minutes rounds up, to 5 above 10 min', () => {
  assert.equal(A.minutes(3300), 55);
  assert.equal(A.minutes(3301), 60);
  assert.equal(A.minutes(290), 5);
  assert.equal(A.minutes(0), null);
});

test('step list follows the running phase', () => {
  const s = A.steps(running('oscillating', 1, 3));
  assert.equal(s.length, 10);
  assert.deepEqual(s.map((x) => x.state), ['done', 'done', 'done', 'done', 'done', 'active', 'pending', 'pending', 'pending', 'pending']);
  assert.equal(s[5].label, 'Measure at 350 °F');
  assert.equal(s[5].detail, 'cycle 3 of up to 6');
  const h = A.steps(running('heating', 0, 0));
  assert.equal(h[0].state, 'active');
  assert.equal(h[0].label, 'Heat to 300 °F');
  const st = A.steps(running('settling', 2, 1));
  assert.equal(st[7].state, 'active');
  assert.equal(st[7].detail, 'cycle 1 of 2');
});

test('step list after a finished and a failed run', () => {
  assert.ok(A.steps(done()).every((x) => x.state === 'done'));
  const failed = Object.assign(done(), { phase: 'failed', failure: 'no heat-up', pending: false, recommended: undefined,
    results: [result(300, 'ok')] });
  const s = A.steps(failed);
  assert.deepEqual(s.map((x) => x.state).slice(0, 5), ['done', 'done', 'done', 'failed', 'pending']);
  assert.equal(A.progressText(failed), 'Stopped: the temperature did not rise; check the heater and mains.');
});

test('progress fraction and text', () => {
  assert.equal(A.fraction(running('heating', 0, 0)), 0.25);
  assert.equal(A.fraction(done()), 1);
  assert.equal(A.progressText(running('heating', 0, 0)), 'Point 1 of 3 · 10:00 elapsed · about 30 min left');
  assert.match(A.progressText(done()), /save or discard/);
  const hot = { phase: 'failed', failure: 'too hot to start', unit: 'F', plan };
  assert.match(A.progressText(hot), /cool toward 300 °F/);
});

test('results and current vs new gains', () => {
  const rows = A.resultRows(done());
  assert.equal(rows.length, 3);
  assert.deepEqual(rows[0], { target: '300 °F', status: 'ok', cycles: '4', swing: '6.1 °F', period: '131 s',
    ku: '4.3', tu: '132 s', kp: '1.9', ki: '0.017', kd: '0' });
  assert.deepEqual(A.gainRows(done()), [{ name: 'Kp', current: '1', next: '1.82' },
    { name: 'Ki', current: '0.1', next: '0.0161' }, { name: 'Kd', current: '0.01', next: '0' }]);
  assert.equal(A.gainRows(running('heating', 0, 0)), null);
  assert.equal(A.gainRows(Object.assign(done(), { pending: false })), null);
});

test('refusals read plainly; polling is fast only while running or pending', () => {
  assert.match(A.refusal('recipeRunning'), /recipe is running/);
  assert.match(A.refusal('autotuneRunning'), /Autotune is running/);
  assert.equal(A.refusal('other'), 'other');
  assert.equal(A.pollMs(running('heating', 0, 0)), 3000);
  assert.equal(A.pollMs(done()), 10000);
  assert.equal(A.pollMs(null), 0);
  assert.equal(A.fmtGain(0.0161234), '0.01612');
  assert.equal(A.ruleName('tyreus-luyben-pi'), 'Tyreus–Luyben PI');
});
