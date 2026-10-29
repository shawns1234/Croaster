// Static checks of croaster-webui/web: syntax, no external resources, and every
// element id the scripts look up exists in index.html. Run: node --test croaster-webui/test/*.test.js
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const WEB = path.join(__dirname, '..', 'web');
const read = (name) => fs.readFileSync(path.join(WEB, name), 'utf8');
const files = fs.readdirSync(WEB);
const scripts = files.filter((f) => f.endsWith('.js'));
const html = read('index.html');

test('every script parses', () => {
  for (const f of scripts) assert.doesNotThrow(() => new vm.Script(read(f), { filename: f }), f);
});

test('no external resources (no CDN, no web fonts)', () => {
  for (const f of files) {
    const text = read(f).replace(/http:\/\/www\.w3\.org\/2000\/svg/g, '');
    assert.doesNotMatch(text, /https?:\/\/(?!<)/, f + ' references an external URL');
    assert.doesNotMatch(text, /@import|@font-face|url\(/, f + ' loads a stylesheet, font or image');
  }
});

test('files referenced by index.html and app.js exist', () => {
  const refs = [...html.matchAll(/(?:src|href)="(?!data:)([^"#?]+)"/g)].map((m) => m[1]);
  refs.push(...[...read('app.js').matchAll(/new Worker\('([^']+)'\)/g)].map((m) => m[1]));
  assert.ok(refs.length >= 4);
  for (const r of refs) assert.ok(files.includes(r), r + ' is missing from web/');
});

test('ids used by app.js exist in index.html', () => {
  const ids = new Set([...html.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1]));
  const used = [...read('app.js').matchAll(/\$\('#([A-Za-z0-9_-]+)/g)].map((m) => m[1]);
  assert.ok(used.length > 20);
  for (const id of used) assert.ok(ids.has(id), '#' + id + ' is not in index.html');
});

test('every form command in index.html has a payload in app.js', () => {
  const app = read('app.js');
  const sends = [...html.matchAll(/data-send="([^"]+)"/g)].map((m) => m[1]);
  for (const s of sends) assert.match(app, new RegExp("case '" + s + "':"), s);
});

test('data-cmd buttons carry valid JSON commands', () => {
  for (const m of html.matchAll(/data-cmd='([^']+)'/g)) {
    const cmd = JSON.parse(m[1]);
    assert.ok('command' in cmd, m[1]);
  }
});

test('risky commands sit behind a confirm step', () => {
  for (const name of ['restartesp', 'erase', 'dummyToggle']) {
    const re = new RegExp('data-cmd=\'\\{"command":"' + name + '"\\}\' data-confirm="');
    assert.match(html, re, name);
  }
  assert.match(html, /data-send="wifiConnect" data-confirm="/);
  assert.match(html, /data-cmd='\{"command":"stop"\}' data-confirm="[^"]+">Heater off</);
});

test('STOP starts disabled and acts only through a confirm while a roast records', () => {
  const app = read('app.js');
  assert.match(html, /<button [^>]*id="stop"[^>]* disabled[ >]/);
  assert.match(app, /\$\('#stop'\)\.addEventListener\('click', stopRoast\)/);
  const body = app.slice(app.indexOf('function stopRoast()'), app.indexOf('function renderEvents('));
  assert.match(body, /if \(!recording\(\)\) return;/);
  assert.ok(body.indexOf('confirmThen(') >= 0 && body.indexOf('confirmThen(') < body.indexOf("send({ command: 'stop' })"),
    'stop is sent from inside the confirm callback');
  assert.match(app, /stop\.disabled = !recording\(\);/);
});

test('samples are recorded only after an explicit start', () => {
  const app = read('app.js');
  const add = app.slice(app.indexOf('function addSample('), app.indexOf('function addEvent('));
  assert.match(add, /^function addSample\(d, at\) \{\s+if \(!recording\(\)\) return;/);
  assert.match(read('app.js'), /active: false/); // a new or reset session does not record
});

test('Monitoring is the first tab and the landing view; Start sits beside STOP', () => {
  const app = read('app.js');
  const tabs = [...html.matchAll(/data-tab="(\w+)"/g)].map((m) => m[1]);
  assert.deepEqual(tabs, ['monitoring', 'live', 'controls']);
  assert.match(html, /<section id="tab-monitoring" data-panel="monitoring">/);
  assert.match(html, /<section id="tab-live" data-panel="live" hidden>/);
  assert.match(app, /tab: 'monitoring',/);
  assert.match(app, /tab = 'monitoring';\n\s+S\.tab = tab;/); // unknown or missing hash
  const bar = html.slice(html.indexOf('<div class="top-actions">'), html.indexOf('</header>'));
  assert.ok(bar.indexOf('id="new-roast"') >= 0 && bar.indexOf('id="new-roast"') < bar.indexOf('id="stop"'));
  assert.match(bar, /id="end-roast" hidden disabled/);
});

test('Start and the event buttons send nothing to the board', () => {
  const app = read('app.js');
  const start = app.slice(app.indexOf('function startRoast('), app.indexOf('function endRoast('));
  const mark = app.slice(app.indexOf('function markEvent('), app.indexOf('function startRoast('));
  for (const body of [start, mark]) assert.doesNotMatch(body, /send\(|postMessage\(/);
  assert.match(mark, /^function markEvent\(code\) \{\s+if \(!R\.eventAllowed\(S\.session, code\)\) return;/);
});

test('the page sends commands only from user actions', () => {
  // Every send() call site. Loading the page or switching views reaches none of
  // them; the worker's polls (getArtisanData, getControl, getDeviceInfo) are read-only.
  // A new command needs an entry here.
  const app = read('app.js');
  const calls = app.split('\n').filter((l) => /\bsend\(/.test(l) && !/function send\(/.test(l)).map((l) => l.trim());
  assert.deepEqual(calls, [
    "send({ command: 'stop' });",
    'send({ command: { [key]: value } }); // one command per click',
    'send({ command: { profile: steps } });',
    'send(cmd);',
    'send({ command: payload });',
  ]);
  const worker = read('link.worker.js');
  const sent = [...worker.matchAll(/send\(\{ command: '(\w+)'/g)].map((m) => m[1]);
  assert.deepEqual(sent, ['getArtisanData', 'getControl', 'getDeviceInfo']);
});

test('theme.js loads in <head>, before first paint and before app.js', () => {
  const head = html.slice(0, html.indexOf('</head>'));
  assert.match(head, /<script src="theme\.js"><\/script>/);
  assert.ok(html.indexOf('theme.js') < html.indexOf('app.js'));
});

test('app.css fallbacks match the default scheme, and every colour token is used', () => {
  const T = require(path.join(WEB, 'theme.js'));
  const css = read('app.css');
  const rootBlock = css.slice(css.indexOf(':root {'), css.indexOf('}', css.indexOf(':root {')));
  const def = T.fromPreset(T.DEFAULT_PRESET);
  const vars = T.cssVars(def).vars;
  for (const k of Object.keys(vars)) {
    const m = rootBlock.match(new RegExp('\\s' + k.replace(/[-]/g, '\\-') + ':\\s*([^;]+);'));
    assert.ok(m, k + ' has no fallback in :root');
    assert.equal(m[1].trim(), vars[k], k);
  }
  const body = css.slice(css.indexOf('}', css.indexOf(':root {')));
  for (const t of T.TOKENS) assert.match(body, new RegExp('var\\(--' + t.key + '\\)'), '--' + t.key + ' is never used');
});

test('the page takes its WebSocket URL from Link.socketUrl (wss://<host>/ws over https)', () => {
  const app = read('app.js');
  assert.ok(html.indexOf('src="link.js"') > 0 && html.indexOf('src="link.js"') < html.indexOf('src="app.js"'));
  assert.match(app, /L\.socketUrl\(location, S\.info\.wsPort, query\.get\('ws'\)\)/);
  assert.doesNotMatch(app, /'wss?:\/\/'/);
});

test('the socket is paused only through Link.mayPause or on pagehide, and Clear roast sends nothing', () => {
  const app = read('app.js');
  assert.match(app, /if \(L\.mayPause\(recording\(\), S\.info\.controls, S\.data\)\) pause\(\);/);
  assert.equal((app.match(/\bpause\(\);/g) || []).length, 2);
  const clear = app.slice(app.indexOf('function clearRoast('), app.indexOf('function endRoast('));
  assert.doesNotMatch(clear, /send\(|postMessage/);
});
