// Croaster web UI: page logic. The WebSocket and polling live in link.worker.js;
// this file keeps the roast session, draws the chart and wires the controls.
(function () {
  'use strict';
  const R = window.Roast;
  const L = window.Link;
  const TH = window.Theme;
  const $ = (sel, el) => (el || document).querySelector(sel);
  const $$ = (sel, el) => Array.from((el || document).querySelectorAll(sel));
  const SVGNS = 'http://www.w3.org/2000/svg';

  const KEY_SESSION = 'croaster.session';
  const KEY_PROFILE = 'croaster.profile';
  const KEY_THEME = 'croaster.theme';
  const KEY_ROR = 'croaster.rorSmooth';
  const KEY_TP = 'croaster.autoTp';
  const MAX_SAMPLES = 4 * 3600; // 4 h at 1 Hz
  const PENDING_STALE_MS = 4000;
  const PENDING_DROP_MS = 10000;
  const STOP_CONFIRM_MS = 5000; // the board should report Manual and heater 0 within this after "stop"
  const RESUME_GAP_MS = 10 * 60 * 1000; // a saved roast older than this is not resumed on load
  const INTERPOLATIONS = ['linear', 'ease-in', 'ease-out', 'ease-in-out'];
  const EVENT_SHORT = { CHARGE: 'CH', TP: 'TP', DRY_END: 'DE', FC_START: 'FCs', FC_END: 'FCe',
    SC_START: 'SCs', SC_END: 'SCe', DROP: 'DR' };

  // Labels for the values the board sends. Keys not listed are shown under their own name.
  const LABELS = {
    name: 'Device name', BT: 'Bean temp (BT)', ET: 'Environment temp (ET)', DBT: 'BT rate of rise',
    DET: 'ET rate of rise', cBT: 'BT correction', cET: 'ET correction', temp: 'Unit', interval: 'Broadcast interval (s)',
    timer: 'Uptime (s)', BurnerVal: 'Heater (%)', FanVal: 'Fan (%)', SV: 'Setpoint', Mode: 'Mode', trip: 'Trip',
    Amb: 'Cold junction', Target: 'PID input', autotune: 'Autotune', pidKp: 'PID Kp', pidKi: 'PID Ki',
    pidKd: 'PID Kd', wifiStrength: 'WiFi RSSI (dBm)',
    mode: 'Mode', pidInput: 'PID input', setpoint: 'Setpoint', burner: 'Heater (%)', fan: 'Fan (%)',
    'cooldownFan.stop': 'Cooldown fan after stop (%)', 'cooldownFan.trip': 'Cooldown fan after trip (%)',
    'watchdog.enabled': 'Comms watchdog', 'watchdog.timeout': 'Watchdog timeout (s)', 'pid.kp': 'PID Kp',
    'pid.ki': 'PID Ki', 'pid.kd': 'PID Kd', tuned: 'Gains tuned', mainsOk: 'Mains detected',
    profileElapsed: 'Profile elapsed (s)', ambient: 'Cold junction',
    ver: 'Firmware version', ip: 'IP address', ssid: 'WiFi network', board: 'Board', darkMode: 'Dark screen',
    brightness: 'Brightness (%)',
  };
  const ALWAYS_LISTED = ['BT', 'ET', 'DBT', 'DET']; // left out of the telemetry while a probe has no reading

  const S = {
    info: { controls: false, wsPort: 81, board: '' },
    link: 'connecting',
    name: '',
    data: null, dataAt: 0,
    control: null, controlAt: 0,
    device: null, deviceAt: 0,
    stats: null, // connection figures from the worker (Monitoring > Connection)
    paused: false, // socket closed while the page is hidden
    uptime: null, restarts: 0, // board uptime (telemetry `timer`, s since boot) and restarts seen from it
    session: null,
    rorSmooth: localStorage.getItem(KEY_ROR) === '1',
    autoTp: localStorage.getItem(KEY_TP) !== '0',
    pending: {},
    tab: 'monitoring',
    cursorT: null,
    dirty: true,
    lastSaveMs: 0,
    lastAutotune: null,
    lastTrip: null,
    stopCheck: null, // {at, warned} while waiting for the board to report the stop
    profile: loadJson(KEY_PROFILE) || [{ duration: 60, setpoint: '', interpolation: 'linear', fanValue: '' }],
  };
  let worker = null;

  // ---------- storage ----------
  function loadJson(key) {
    try { return JSON.parse(localStorage.getItem(key)); } catch (_) { return null; }
  }
  function saveJson(key, value) {
    try { localStorage.setItem(key, JSON.stringify(value)); return true; } catch (_) { return false; }
  }
  function newSession() {
    return { startedAt: Date.now(), unit: null, samples: [], events: [], active: false, endedAt: null };
  }
  function saveSession(force) {
    const now = Date.now();
    if (!force && now - S.lastSaveMs < 10000) return;
    S.lastSaveMs = now;
    if (!saveJson(KEY_SESSION, S.session)) $('#session-info').textContent = 'Browser storage full: download the CSV.';
  }

  // ---------- helpers ----------
  const isNum = (v) => typeof v === 'number' && isFinite(v);
  const unit = () => (S.data && S.data.temp) || S.session.unit || '';
  const unitLabel = () => (unit() === 'K' ? 'K' : unit() ? '°' + unit() : '');
  function fmt(v, digits) {
    return isNum(v) ? v.toFixed(digits === undefined ? 1 : digits) : '—';
  }
  function eventOf(code) {
    return S.session.events.find((e) => e.code === code);
  }
  function chargeT() {
    const e = eventOf('CHARGE');
    return e ? e.t : null;
  }
  function nowT() {
    return (Date.now() - S.session.startedAt) / 1000;
  }
  function endT() {
    return S.session.endedAt ? (S.session.endedAt - S.session.startedAt) / 1000 : nowT();
  }
  const recording = () => R.isRecording(S.session);
  function lastSample() {
    const ss = S.session.samples;
    return ss.length ? ss[ss.length - 1] : null;
  }
  function rorAt(i) {
    const s = S.session.samples[i];
    if (!s) return null;
    if (!S.rorSmooth) return isNum(s.dbt) ? s.dbt : null;
    if (s.sr === undefined) s.sr = R.smoothedRor(S.session.samples, i, 'bt', 30);
    return s.sr;
  }
  function el(tag, attrs, text) {
    const e = document.createElement(tag);
    for (const k in attrs || {}) e.setAttribute(k, attrs[k]);
    if (text !== undefined) e.textContent = text;
    return e;
  }
  function svgEl(tag, attrs) {
    const e = document.createElementNS(SVGNS, tag);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    return e;
  }

  // ---------- link ----------
  function send(payload) {
    if (!worker) return;
    worker.postMessage({ type: 'send', payload });
    logReply('out', '→ ' + JSON.stringify(payload));
  }

  function onWorker(e) {
    const m = e.data;
    if (m.type === 'status') {
      S.link = m.state;
      S.dirty = true;
    } else if (m.type === 'stats') {
      S.stats = m;
      if (S.tab === 'monitoring') S.dirty = true;
    } else if (m.type === 'sent' && !m.ok) {
      logReply('err', 'not sent: not connected');
    } else if (m.type === 'message') {
      onMessage(m.msg, m.polled, m.at);
    }
  }

  function onMessage(msg, polled, at) {
    if (!msg || typeof msg !== 'object') return;
    if (msg.text !== undefined) { logReply('in', '← ' + msg.text); return; }
    if (msg.data && msg.data.type === 'preferences') { logReply('in', '← ' + JSON.stringify(msg)); return; }
    if (msg.data && typeof msg.data === 'object') {
      if (msg.name) S.name = msg.name;
      S.data = msg.data;
      S.dataAt = at;
      watchUptime(msg.data.timer);
      watchStatus(msg.data);
      checkStop(msg.data);
      if (polled) addSample(msg.data, at);
      S.dirty = true;
      return;
    }
    if (msg.command === 'getControl') { S.control = msg.response || {}; S.controlAt = at; S.dirty = true; return; }
    if (msg.command === 'getDeviceInfo') { S.device = msg.response || {}; S.deviceAt = at; S.dirty = true; return; }
    if (msg.command === 'profile') {
      const r = msg.response || {};
      $('#profile-status').textContent = r.loaded ? 'Loaded ' + r.steps + ' steps.' :
        'Not loaded: the board rejected the profile (' + (r.steps || 0) + ' steps read; 1 to 32 steps, each duration above 0).';
    }
    logReply('in', '← ' + JSON.stringify(msg));
  }

  // `timer` is the board's uptime; a drop means it restarted (power, reset line, crash or watchdog).
  function watchUptime(t) {
    if (!isNum(t)) return;
    if (S.uptime !== null && t + 2 < S.uptime) {
      S.restarts++;
      logReply('err', 'board restarted: uptime went from ' + R.formatClock(S.uptime) + ' to ' + R.formatClock(t));
    }
    S.uptime = t;
  }

  // Autotune results and trips arrive as telemetry fields; surface their changes in the reply log.
  function watchStatus(d) {
    if (d.autotune !== undefined && d.autotune !== S.lastAutotune) {
      if (S.lastAutotune !== null) {
        const bad = /^(need|failed|aborted|autotune required)/.test(d.autotune);
        logReply(bad ? 'err' : 'in', 'autotune: ' + d.autotune);
      }
      S.lastAutotune = d.autotune;
    }
    if (d.trip !== undefined && d.trip !== S.lastTrip) {
      if (d.trip) logReply('err', 'trip: ' + d.trip);
      S.lastTrip = d.trip;
    }
  }

  // "stop" has no reply of its own: report what the next readings show.
  function expectStop() {
    S.stopCheck = { at: Date.now(), warned: false };
  }
  function checkStop(d) {
    const c = S.stopCheck;
    if (!c) return;
    if (R.stopConfirmed(d)) {
      logReply('in', 'stop: board reports heater 0 %, Manual' + (isNum(d.FanVal) ? ', fan ' + d.FanVal + ' %' : ''));
      S.stopCheck = null;
    } else if (!c.warned && Date.now() - c.at > STOP_CONFIRM_MS) {
      c.warned = true;
      logReply('err', 'stop: not confirmed, board reports heater ' + fmt(d.BurnerVal, 0) + ' %, mode ' +
        (d.Mode || '?') + '. Use Heater off in Controls, or cut power.');
      S.dirty = true;
    }
  }

  function logReply(kind, text) {
    const log = $('#reply-log');
    const li = el('li', { class: kind }, new Date().toLocaleTimeString() + ' ' + text);
    log.prepend(li);
    while (log.children.length > 40) log.lastChild.remove();
  }

  // ---------- session ----------
  function addSample(d, at) {
    if (!recording()) return; // readings still update the tiles; only a started roast is recorded
    const ses = S.session;
    if (d.temp && ses.unit && d.temp !== ses.unit) {
      R.convertSession(ses, d.temp);
      ses.samples.forEach((s) => { delete s.sr; });
    }
    if (d.temp) ses.unit = d.temp;
    const s = {
      t: Math.round((at - ses.startedAt) / 100) / 10,
      bt: isNum(d.BT) ? d.BT : null, et: isNum(d.ET) ? d.ET : null,
      dbt: isNum(d.DBT) ? d.DBT : null, det: isNum(d.DET) ? d.DET : null,
      h: isNum(d.BurnerVal) ? d.BurnerVal : null, f: isNum(d.FanVal) ? d.FanVal : null,
      sv: isNum(d.SV) ? d.SV : null,
      mode: (d.Mode || '') + (d.trip ? ' trip:' + d.trip : ''),
    };
    ses.samples.push(s);
    if (ses.samples.length > MAX_SAMPLES) ses.samples.shift();

    const charge = chargeT();
    if (S.autoTp && charge !== null && !eventOf('TP') && !eventOf('DROP')) {
      const tp = R.detectTurningPoint(ses.samples, charge, { riseBy: ses.unit === 'F' ? 2 : 1 });
      if (tp) addEvent({ code: 'TP', t: tp.t, bt: tp.bt, auto: true });
    }
    saveSession(false);
  }

  function addEvent(ev) {
    S.session.events.push(ev);
    S.dirty = true;
    renderEvents();
    saveSession(true);
  }

  function markEvent(code) {
    if (!R.eventAllowed(S.session, code)) return;
    const last = lastSample();
    const live = S.data && isNum(S.data.BT) ? S.data.BT : null;
    addEvent({ code, t: last ? last.t : nowT(), bt: last ? last.bt : live });
  }

  // Start: begins a recording in this page only (samples from now, so the preheat
  // shows; the roast clock starts at Charge). Nothing is sent to the board. Asks
  // first when it would clear an earlier roast.
  function startRoast() {
    const go = () => {
      S.session = newSession();
      S.session.active = true;
      saveSession(true);
      render();
    };
    if (recording()) return;
    if (S.session.samples.length || S.session.events.length) {
      confirmThen('Start a new roast? The previous samples and events are cleared (download the CSV first to keep them).', go);
    } else {
      go();
    }
  }

  // Clear roast: drops the kept samples and events of an ended roast from this browser.
  // Nothing is sent to the board.
  function clearRoast() {
    if (recording() || (!S.session.samples.length && !S.session.events.length)) return;
    confirmThen('Clear the last roast from this browser? Its samples and events are deleted (download the CSV first to keep them). The board is not changed.', () => {
      S.session = newSession();
      saveSession(true);
      render();
    });
  }

  function endRoast() {
    if (R.endSession(S.session, Date.now())) saveSession(true);
    render();
  }

  function stopRoast() {
    if (!recording()) return;
    const c = S.control && S.control.cooldownFan;
    const fan = c && isNum(c.stop) ? ' (' + c.stop + ' %)' : '';
    confirmThen('Stop the roast? The heater turns off, the board switches to Manual (ending PID, profile or autotune), ' +
      'the fan runs at the cooldown speed' + fan + ', and recording ends.', () => {
      if (S.link !== 'open') {
        logReply('err', 'STOP not sent: not connected. The board keeps its current heater and mode.');
        return;
      }
      send({ command: 'stop' });
      expectStop();
      endRoast();
    });
  }

  function renderEvents() {
    const box = $('#event-buttons');
    if (!box.children.length) {
      for (const ev of R.EVENTS) {
        const b = el('button', { type: 'button', 'data-code': ev.code, class: ev.code.toLowerCase() }, ev.label);
        b.addEventListener('click', () => markEvent(ev.code));
        box.append(b);
      }
    }
    for (const b of $$('button', box)) {
      const code = b.dataset.code;
      b.classList.toggle('marked', !!eventOf(code));
      b.disabled = !R.eventAllowed(S.session, code);
    }
    $('#undo-event').disabled = !recording() || !S.session.events.length;
    $('#end-after-drop').hidden = !recording() || !eventOf('DROP');

    const list = $('#event-list');
    list.textContent = '';
    const evs = S.session.events.slice().sort((a, b) => a.t - b.t);
    const t0 = chargeT();
    if (!evs.length) list.append(el('li', { class: 'muted' }, 'No events yet'));
    for (const e of evs) {
      const label = (R.EVENTS.find((x) => x.code === e.code) || { label: e.code }).label;
      const at = t0 === null ? '' : R.formatClock(e.t - t0);
      const temp = isNum(e.bt) ? ' · ' + e.bt.toFixed(1) + ' ' + unitLabel() : '';
      list.append(el('li', {}, label + ' ' + at + temp + (e.auto ? ' (auto)' : '')));
    }
  }

  // ---------- rendering ----------
  function render() {
    S.dirty = false;
    const d = S.data || {};
    $('#device-name').textContent = S.name || (S.link === 'open' ? 'connected' : 'not connected');
    const link = $('#link');
    link.dataset.state = S.link;
    $('#link-text').textContent = S.link;
    for (const u of $$('[data-unit]')) u.textContent = unitLabel();

    // Tiles. Heater, fan, setpoint and mode only show when the board reports them.
    $('#t-bt').textContent = fmt(d.BT);
    $('#t-et').textContent = fmt(d.ET);
    const ss = S.session.samples;
    const ror = S.rorSmooth ? rorAt(ss.length - 1) : d.DBT;
    $('#t-ror').textContent = fmt(ror);
    $('#ror-src').textContent = S.rorSmooth ? 'smoothed' : 'device';
    $('#t-ror-et').textContent = isNum(d.DET) ? 'ET ' + d.DET.toFixed(1) + ' ' + unitLabel() + '/min' : unitLabel() + '/min';
    $('#t-heat').textContent = fmt(d.BurnerVal, 0);
    $('#t-fan').textContent = fmt(d.FanVal, 0);
    $('#t-sv').textContent = fmt(d.SV);
    $('#t-mode').textContent = d.Mode || '—';
    const stop = $('#stop');
    stop.disabled = !recording();
    stop.title = recording() ? 'End the roast: heater off, Manual, cooldown fan' : 'Available while a roast is recording';
    const start = $('#new-roast');
    start.disabled = recording();
    start.title = recording() ? 'A roast is recording: STOP or End roast ends it' : 'Start recording a roast';
    const end = $('#end-roast');
    end.hidden = S.info.controls;
    end.disabled = !recording();
    end.title = recording() ? 'Stop recording; the board is not changed' : 'Available while a roast is recording';
    $('#t-trip').textContent = d.trip ? 'TRIP ' + d.trip : (d.Target ? 'input ' + d.Target : '');
    $('.tile.mode').classList.toggle('tripped', !!d.trip);
    for (const e of $$('[data-field]')) {
      if (e.classList.contains('stepper')) continue;
      e.hidden = S.data ? d[e.dataset.field] === undefined : !S.info.controls;
    }

    const banner = $('#trip-banner');
    banner.hidden = !d.trip;
    if (d.trip) banner.textContent = 'TRIP: ' + d.trip + '. Heater is off. Fix the cause, then use Clear trip in Controls.';

    renderClock();
    renderEvents();
    if (S.tab === 'live') { renderChart(); }
    if (S.tab === 'monitoring') renderMonitoring();
    if (S.tab === 'controls') renderControls();
    let info;
    if (ss.length) {
      const charge = chargeT();
      const drop = eventOf('DROP');
      const last = !recording() && charge !== null ? ', roast ' + R.formatClock((drop ? drop.t : endT()) - charge) : '';
      info = ss.length + ' samples since ' + new Date(S.session.startedAt).toLocaleTimeString() +
        (S.session.unit ? ' (' + unitLabel() + ')' : '') + (recording() ? '' : ', recording ended' + last);
    } else {
      info = recording() ? 'Recording, waiting for the first sample' : 'Not recording. Start begins a recording.';
    }
    $('#session-info').textContent = info;
    $('#clear-roast').disabled = recording() || (!ss.length && !S.session.events.length);
  }

  function renderClock() {
    const st = R.sessionState(S.session);
    const charge = chargeT();
    const drop = eventOf('DROP');
    let label = 'Idle', value = '--:--';
    if (st === 'roasting') {
      label = 'Roast';
      value = R.formatClock(nowT() - charge);
    } else if (st === 'cooling') {
      label = 'Cooling · roast ' + R.formatClock(drop.t - charge);
      value = R.formatClock(nowT() - drop.t);
    } else if (st === 'preheat') {
      label = 'Preheat';
      value = R.formatClock(nowT());
    }
    $('#phase').textContent = label;
    $('#roast-clock').textContent = value;
  }

  function kvRows(table, obj, listed) {
    table.textContent = '';
    if (!obj) {
      const tr = el('tr');
      tr.append(el('td', {}, 'Waiting for the board…'));
      table.append(tr);
      return;
    }
    const flat = [];
    const walk = (o, prefix) => {
      for (const k of Object.keys(o)) {
        const v = o[k];
        if (v && typeof v === 'object' && !Array.isArray(v)) walk(v, prefix + k + '.');
        else flat.push([prefix + k, v]);
      }
    };
    walk(obj, '');
    for (const k of listed || []) if (!(k in obj)) flat.push([k, undefined]);
    for (const [k, v] of flat) {
      const tr = el('tr');
      tr.append(el('td', {}, LABELS[k] || k));
      let text;
      if (v === undefined) text = 'no reading';
      else if (v === null) text = 'none';
      else if (typeof v === 'boolean') text = v ? 'yes' : 'no';
      else if (v === '') text = '(none)';
      else text = String(v);
      tr.append(el('td', {}, text));
      tr.append(el('td', { class: 'key' }, k));
      table.append(tr);
    }
  }

  function age(at) {
    return at ? 'updated ' + ((Date.now() - at) / 1000).toFixed(0) + ' s ago' : '';
  }

  function renderMonitoring() {
    const top = S.data ? Object.assign({ name: S.name }, S.data) : null;
    kvRows($('#mon-data'), top, S.data ? ALWAYS_LISTED : null);
    kvRows($('#mon-control'), S.control);
    kvRows($('#mon-info'), S.device);
    $('#age-data').textContent = age(S.dataAt);
    $('#age-control').textContent = age(S.controlAt);
    $('#age-info').textContent = age(S.deviceAt);
    renderConnection();
  }

  // Connection card: what the worker sees of the WebSocket. The board does not report its client count.
  function renderConnection() {
    const t = $('#mon-link');
    const st = S.stats;
    t.textContent = '';
    const secs = (at) => (at ? ((Date.now() - at) / 1000).toFixed(0) + ' s ago' : '—');
    const rows = [
      ['Page', location.origin],
      ['WebSocket URL', st ? st.url : '—'],
      ['State', S.link + (st && st.detail && st.detail !== st.url ? ' (' + st.detail + ')' : '')],
      ['Last message', st ? secs(st.lastRxAt) : '—'],
      ['Round trip (getArtisanData)', st && isNum(st.rttMs) ? st.rttMs + ' ms' : '—'],
      ['Connected since', st && S.link === 'open' ? secs(st.openedAt) : '—'],
      ['Connections opened', st ? String(st.opens) : '—'],
      ['Connection attempts', st ? String(st.attempts) : '—'],
      ['Last close', st && st.closedAt ? [st.closeCode ? L.closeText(st.closeCode) : '', st.closeReason, secs(st.closedAt)]
        .filter(Boolean).join(', ') : '—'],
      ['Board WiFi signal', S.data && isNum(S.data.wifiStrength) ? S.data.wifiStrength + ' dBm' : 'not reported'],
      ['Board uptime', S.uptime !== null ? R.formatClock(S.uptime) : '—'],
      ['Board restarts seen', String(S.restarts)],
      ['Board clients', 'not reported by the board'],
    ];
    if (location.protocol === 'https:' && !new URLSearchParams(location.search).get('ws')) {
      rows.push(['Note', 'Page loaded over https: the proxy must forward wss://' + location.host + '/ws to port ' +
        S.info.wsPort + ' on the board.']);
    }
    for (const [k, v] of rows) {
      const tr = el('tr');
      tr.append(el('td', {}, k));
      tr.append(el('td', {}, v));
      t.append(tr);
    }
  }

  // ---------- chart ----------
  function renderChart() {
    const svg = $('#chart');
    const W = svg.clientWidth, H = svg.clientHeight;
    if (!W || !H) return;
    const ss = S.session.samples;
    const charge = chargeT();
    const M = { l: 46, r: 46, t: 20, b: 22 };
    const t0 = charge !== null ? charge : (ss.length ? ss[0].t : 0);
    let xMin = ss.length ? ss[0].t - t0 : 0;
    if (charge !== null) xMin = Math.max(xMin, -180); // keep at most 3 min of preheat in view
    const xLast = ss.length ? ss[ss.length - 1].t - t0 : 0;
    const xMax = Math.max(xLast, xMin + (charge !== null ? 720 : 300));
    const xTicks = R.niceTicks(xMin / 60, xMax / 60, Math.max(3, Math.floor(W / 90)));
    const x = (t) => M.l + ((t - t0) - xMin) / (xMax - xMin) * (W - M.l - M.r);

    const u = unit();
    const base = u === 'C' ? [40, 230] : u === 'K' ? [313, 503] : [100, 450];
    let tLo = base[0], tHi = base[1], rLo = 0, rHi = u === 'F' ? 30 : 18;
    const vis = ss.filter((s) => s.t - t0 >= xMin);
    for (const s of vis) {
      for (const v of [s.bt, s.et, s.sv]) if (isNum(v)) { tLo = Math.min(tLo, v); tHi = Math.max(tHi, v); }
    }
    const first = ss.length - vis.length;
    for (let i = first; i < ss.length; i++) {
      const r = rorAt(i);
      if (isNum(r)) { rLo = Math.min(rLo, Math.max(r, -20)); rHi = Math.max(rHi, Math.min(r, 120)); }
    }
    const yT = R.niceTicks(tLo, tHi, Math.max(3, Math.floor(H / 60)));
    const yR = R.niceTicks(rLo, rHi, yT.ticks.length - 1);
    const yt = (v) => H - M.b - (v - yT.lo) / (yT.hi - yT.lo) * (H - M.t - M.b);
    const yr = (v) => H - M.b - (Math.max(yR.lo, Math.min(yR.hi, v)) - yR.lo) / (yR.hi - yR.lo) * (H - M.t - M.b);

    svg.textContent = '';
    svg.setAttribute('viewBox', '0 0 ' + W + ' ' + H);
    for (const v of yT.ticks) {
      svg.append(svgEl('line', { class: 'grid', x1: M.l, x2: W - M.r, y1: yt(v), y2: yt(v) }));
      const tx = svgEl('text', { x: M.l - 6, y: yt(v) + 4, 'text-anchor': 'end' });
      tx.textContent = v;
      svg.append(tx);
    }
    const rStep = (yR.hi - yR.lo) / Math.max(1, yT.ticks.length - 1);
    yT.ticks.forEach((_, i) => {
      const v = Math.round((yR.lo + i * rStep) * 10) / 10;
      const tx = svgEl('text', { x: W - M.r + 6, y: yr(v) + 4 });
      tx.textContent = v;
      svg.append(tx);
    });
    for (const m of xTicks.ticks) {
      const px = x(t0 + m * 60);
      if (px < M.l - 1 || px > W - M.r + 1) continue;
      svg.append(svgEl('line', { class: 'grid', x1: px, x2: px, y1: M.t, y2: H - M.b }));
      const tx = svgEl('text', { x: px, y: H - 6, 'text-anchor': 'middle' });
      tx.textContent = R.formatClock(m * 60);
      svg.append(tx);
    }

    const stepN = Math.max(1, Math.ceil(vis.length / (W * 1.5)));
    const line = (cls, get, ymap) => {
      let d = '', pen = false;
      for (let i = first; i < ss.length; i += stepN) {
        const v = get(i);
        if (!isNum(v)) { pen = false; continue; }
        d += (pen ? 'L' : 'M') + x(ss[i].t).toFixed(1) + ' ' + ymap(v).toFixed(1);
        pen = true;
      }
      if (d) svg.append(svgEl('path', { class: cls, d }));
    };
    line('sv', (i) => ss[i].sv, yt);
    line('et', (i) => ss[i].et, yt);
    line('bt', (i) => ss[i].bt, yt);
    line('ror', rorAt, yr);
    if (!S.rorSmooth) line('ror-et', (i) => ss[i].det, yr);
    $('#key-ror-et').hidden = S.rorSmooth;

    // Labels of events close together go on the next row down instead of overlapping.
    const rowEnd = [];
    for (const e of S.session.events.slice().sort((a, b) => a.t - b.t)) {
      const px = x(e.t);
      if (px < M.l) continue;
      const g = svgEl('g', { class: 'event' });
      g.append(svgEl('line', { x1: px, x2: px, y1: M.t, y2: H - M.b }));
      const label = EVENT_SHORT[e.code] + (isNum(e.bt) ? ' ' + Math.round(e.bt) : '');
      let row = rowEnd.findIndex((end) => end < px);
      if (row < 0) row = rowEnd.length;
      rowEnd[row] = px + 3 + label.length * 7;
      const tx = svgEl('text', { x: px + 3, y: M.t - 6 + row * 13 });
      tx.textContent = label;
      g.append(tx);
      svg.append(g);
    }

    if (S.cursorT !== null && ss.length) {
      const px = x(S.cursorT);
      svg.append(svgEl('line', { class: 'cursor', x1: px, x2: px, y1: M.t, y2: H - M.b }));
    }
    renderStrips(x, W, M, first);
    S.chartX = { x, t0, W, M, xMin, xMax };
  }

  function renderStrips(x, W, M, first) {
    const svg = $('#strips');
    if (svg.hidden) return;
    const H = svg.clientHeight;
    if (!H) return;
    const ss = S.session.samples;
    svg.textContent = '';
    svg.setAttribute('viewBox', '0 0 ' + W + ' ' + H);
    const y = (v) => H - 4 - Math.max(0, Math.min(100, v)) / 100 * (H - 8);
    for (const v of [0, 50, 100]) {
      svg.append(svgEl('line', { class: 'grid', x1: M.l, x2: W - M.r, y1: y(v), y2: y(v) }));
      const tx = svgEl('text', { x: M.l - 6, y: y(v) + 4, 'text-anchor': 'end' });
      tx.textContent = v + '%';
      svg.append(tx);
    }
    const step = (cls, key) => {
      let d = '', last = null;
      for (let i = first; i < ss.length; i++) {
        const v = ss[i][key];
        if (!isNum(v)) { last = null; continue; }
        const px = x(ss[i].t).toFixed(1), py = y(v).toFixed(1);
        d += last === null ? 'M' + px + ' ' + py : 'H' + px + (v !== last ? 'V' + py : '');
        last = v;
      }
      if (d) svg.append(svgEl('path', { class: cls, d }));
    };
    step('fan', 'f');
    step('heat', 'h');
  }

  function onChartPointer(ev) {
    const c = S.chartX;
    const ss = S.session.samples;
    if (!c || !ss.length) return;
    const rect = $('#chart').getBoundingClientRect();
    const px = ev.clientX - rect.left;
    const t = c.t0 + c.xMin + (px - c.M.l) / (c.W - c.M.l - c.M.r) * (c.xMax - c.xMin);
    let best = 0;
    for (let i = 0; i < ss.length; i++) if (Math.abs(ss[i].t - t) < Math.abs(ss[best].t - t)) best = i;
    const s = ss[best];
    S.cursorT = s.t;
    const parts = [R.formatClock(s.t - c.t0), 'BT ' + fmt(s.bt), 'ET ' + fmt(s.et), 'RoR ' + fmt(rorAt(best))];
    if (isNum(s.h)) parts.push('heater ' + s.h + '%');
    if (isNum(s.f)) parts.push('fan ' + s.f + '%');
    if (isNum(s.sv)) parts.push('SV ' + fmt(s.sv));
    $('#readout').textContent = parts.join('  ');
    renderChart();
  }

  // ---------- controls ----------
  function renderControls() {
    const d = S.data || {};
    for (const st of $$('.stepper')) {
      const key = st.dataset.key;
      const p = S.pending[key];
      const now = Date.now();
      const reported = d[st.dataset.field];
      if (p && (reported === p.value || now - p.at > PENDING_DROP_MS)) delete S.pending[key];
      const q = S.pending[key];
      st.classList.toggle('pending', !!q && now - q.at <= PENDING_STALE_MS);
      st.classList.toggle('stale', !!q && now - q.at > PENDING_STALE_MS);
      st.title = q && now - q.at > PENDING_STALE_MS ? 'The board reports ' + reported + ': it may have limited the value' : '';
      $('output', st).textContent = q ? q.value : fmt(reported, 0);
    }
    $('#autotune-status').textContent = d.autotune || '—';

    // Fill the forms from the board's values, except fields the user is editing.
    const c = S.control || {};
    fill('setpoint', { value: d.SV });
    fill('pidInput', { value: d.Target });
    fill('pid', { kp: d.pidKp, ki: d.pidKi, kd: d.pidKd });
    if (c.cooldownFan) fill('cooldownFan', c.cooldownFan);
    if (c.watchdog) fill('watchdog', c.watchdog);
    fill('tempUnit', { value: d.temp });
    fill('interval', { value: d.interval });
    fill('correctionBt', { value: d.cBT });
    fill('correctionEt', { value: d.cET });
  }

  function fill(name, values) {
    const form = $('form[data-send="' + name + '"]');
    if (!form || form.dataset.touched) return;
    for (const k in values) {
      const input = form.elements[k];
      const v = values[k];
      if (!input || v === undefined || v === null || document.activeElement === input) continue;
      if (input.type === 'checkbox') input.checked = !!v;
      else input.value = typeof v === 'number' ? Math.round(v * 1000) / 1000 : v;
    }
  }

  function formPayload(form) {
    const f = form.elements;
    const n = (k) => Number(f[k].value);
    switch (form.dataset.send) {
      case 'setpoint': return { setpoint: n('value') };
      case 'pidInput': return { pidInput: f.value.value };
      case 'pid': return { pid: { kp: n('kp'), ki: n('ki'), kd: n('kd') } };
      case 'cooldownFan': return { cooldownFan: { stop: n('stop'), trip: n('trip') } };
      case 'watchdog': return { watchdog: { enabled: f.enabled.checked, timeout: Math.round(n('timeout')) } };
      case 'tempUnit': return { tempUnit: f.value.value };
      case 'interval': return { interval: Math.round(n('value')) };
      case 'correctionBt': return { correctionBt: n('value') };
      case 'correctionEt': return { correctionEt: n('value') };
      case 'wifiConnect': return { wifiConnect: { ssid: f.ssid.value, pass: f.pass.value } };
    }
    return null;
  }

  function confirmThen(text, action) {
    if (!text) { action(); return; }
    const dlg = $('#confirm');
    $('#confirm-text').textContent = text;
    dlg.returnValue = '';
    dlg.onclose = () => { if (dlg.returnValue === 'ok') action(); };
    dlg.showModal();
  }

  function stepperClick(st, step) {
    const key = st.dataset.key;
    const reported = (S.data || {})[st.dataset.field];
    const base = S.pending[key] ? S.pending[key].value : (isNum(reported) ? reported : 0);
    const value = Math.max(Number(st.dataset.min), Math.min(Number(st.dataset.max), Math.round(base + step)));
    S.pending[key] = { value, at: Date.now() };
    send({ command: { [key]: value } }); // one command per click
    renderControls();
  }

  // ---------- profile editor ----------
  function renderProfile() {
    const body = $('#profile tbody');
    body.textContent = '';
    S.profile.forEach((step, i) => {
      const tr = el('tr');
      tr.append(el('td', { class: 'num' }, String(i + 1)));
      const cell = (input) => { const td = el('td'); td.append(input); tr.append(td); return input; };
      const num = (key, attrs) => {
        const inp = el('input', Object.assign({ type: 'number', step: 'any' }, attrs));
        inp.value = step[key];
        inp.addEventListener('input', () => { step[key] = inp.value; saveJson(KEY_PROFILE, S.profile); });
        return cell(inp);
      };
      num('duration', { min: '1' });
      num('setpoint', {});
      const sel = el('select');
      for (const o of INTERPOLATIONS) sel.append(el('option', { value: o }, o));
      sel.value = step.interpolation || 'linear';
      sel.addEventListener('change', () => { step.interpolation = sel.value; saveJson(KEY_PROFILE, S.profile); });
      cell(sel);
      num('fanValue', { min: '0', max: '100', placeholder: 'keep' });
      const td = el('td', { class: 'row-buttons' });
      const btn = (label, title, fn) => {
        const b = el('button', { type: 'button', class: 'ghost', title }, label);
        b.addEventListener('click', () => { fn(); saveJson(KEY_PROFILE, S.profile); renderProfile(); });
        td.append(b);
      };
      btn('↑', 'Move up', () => { if (i > 0) S.profile.splice(i - 1, 0, S.profile.splice(i, 1)[0]); });
      btn('↓', 'Move down', () => { if (i < S.profile.length - 1) S.profile.splice(i + 1, 0, S.profile.splice(i, 1)[0]); });
      btn('✕', 'Remove', () => { S.profile.splice(i, 1); });
      tr.append(td);
      body.append(tr);
    });
    $('#profile-add').disabled = S.profile.length >= 32;
  }

  function loadProfile() {
    const steps = [];
    for (const [i, s] of S.profile.entries()) {
      const duration = Number(s.duration), setpoint = Number(s.setpoint);
      if (!(duration > 0) || s.setpoint === '' || !isFinite(setpoint)) {
        $('#profile-status').textContent = 'Step ' + (i + 1) + ': needs a duration above 0 and a setpoint.';
        return;
      }
      const step = { duration, setpoint, interpolation: s.interpolation || 'linear' };
      if (s.fanValue !== '' && s.fanValue !== undefined && isFinite(Number(s.fanValue))) step.fanValue = Number(s.fanValue);
      steps.push(step);
    }
    if (!steps.length || steps.length > 32) {
      $('#profile-status').textContent = 'A profile has 1 to 32 steps.';
      return;
    }
    $('#profile-status').textContent = 'Sent ' + steps.length + ' steps, waiting for the board…';
    send({ command: { profile: steps } });
  }

  // ---------- wiring ----------
  // Monitoring is the landing view; a #live or #controls in the URL opens that view.
  // Opening a view only changes what is drawn: it sends nothing and records nothing.
  function setTab(tab) {
    if (tab === 'controls' && !S.info.controls) tab = 'monitoring';
    if (!['live', 'monitoring', 'controls'].includes(tab)) tab = 'monitoring';
    S.tab = tab;
    for (const p of $$('[data-panel]')) p.hidden = p.dataset.panel !== tab;
    for (const a of $$('[data-tab]')) a.classList.toggle('active', a.dataset.tab === tab);
    render();
  }

  // ---------- appearance ----------
  // The scheme lives in croaster.theme as JSON (theme.js documents the format).
  // Every change is applied to <html> at once and saved, so the chart and the
  // rest of the page restyle without a reload.
  const AP = { theme: null, inputs: {} };

  function saveTheme() {
    try { localStorage.setItem(KEY_THEME, TH.serialize(AP.theme)); } catch (_) { /* storage full or blocked */ }
  }

  function setTheme(theme) {
    AP.theme = theme;
    TH.apply(theme, document.documentElement);
    saveTheme();
    syncAppearance();
  }

  function apStatus(text, isError) {
    const p = $('#ap-status');
    p.textContent = text;
    p.classList.toggle('err', !!isError);
  }

  function syncAppearance() {
    const t = AP.theme;
    for (const k in AP.inputs) {
      const i = AP.inputs[k];
      if (i.hex) {
        i.color.value = t.colors[k];
        if (document.activeElement !== i.hex) { i.hex.value = t.colors[k]; i.hex.removeAttribute('aria-invalid'); }
      } else if (i.type === 'bool') i.input.checked = t.style[k];
      else {
        i.input.value = t.style[k];
        if (i.out) i.out.textContent = t.style[k] + (i.unit || '');
      }
    }
    const current = TH.serialize(t);
    for (const b of $$('#ap-presets button')) {
      b.setAttribute('aria-pressed', String(TH.serialize(TH.fromPreset(b.dataset.preset)) === current));
    }
    const warn = TH.contrastWarnings(t);
    const ul = $('#ap-warnings');
    ul.hidden = !warn.length;
    ul.textContent = '';
    for (const w of warn) {
      ul.append(el('li', {}, w.label + ': contrast ' + w.ratio.toFixed(2) + ':1, below the ' + TH.AA_MIN +
        ':1 WCAG AA minimum for text.'));
    }
  }

  function buildAppearance() {
    const presets = $('#ap-presets');
    for (const name of Object.keys(TH.PRESETS)) {
      const b = el('button', { type: 'button', 'data-preset': name }, TH.PRESETS[name].label);
      b.addEventListener('click', () => { setTheme(TH.fromPreset(name)); apStatus(''); });
      presets.append(b);
    }

    const colors = $('#ap-colors');
    let group = null;
    for (const tok of TH.TOKENS) {
      if (tok.group !== group) {
        group = tok.group;
        colors.append(el('h4', { class: 'small muted' }, group));
      }
      const id = 'ap-c-' + tok.key;
      const row = el('div', { class: 'swatch-row' });
      const color = el('input', { type: 'color', id, 'aria-label': tok.label + ' colour picker' });
      const hex = el('input', { type: 'text', maxlength: '7', spellcheck: 'false', autocomplete: 'off',
        'aria-label': tok.label + ' hex value' });
      row.append(el('label', { for: id }, tok.label), color, hex);
      colors.append(row);
      color.addEventListener('input', () => {
        AP.theme.colors[tok.key] = color.value.toLowerCase();
        setTheme(AP.theme);
      });
      hex.addEventListener('input', () => {
        const v = TH.normalizeHex(hex.value);
        hex.setAttribute('aria-invalid', String(!v));
        if (v) { AP.theme.colors[tok.key] = v; setTheme(AP.theme); }
      });
      hex.addEventListener('change', () => { hex.value = AP.theme.colors[tok.key]; hex.removeAttribute('aria-invalid'); });
      AP.inputs[tok.key] = { color, hex };
    }

    const style = $('#ap-style');
    for (const key of Object.keys(TH.STYLE_FIELDS)) {
      const f = TH.STYLE_FIELDS[key];
      const id = 'ap-s-' + key;
      const row = el('div', { class: 'style-row' });
      const label = el('label', { for: id }, f.label);
      let input, out = null;
      if (f.type === 'number') {
        input = el('input', { type: 'range', id, min: f.min, max: f.max, step: f.step });
        out = el('output', { for: id });
        const wrap = el('span');
        wrap.append(input, ' ', out);
        row.append(label, wrap);
      } else if (f.type === 'choice') {
        input = el('select', { id });
        for (const o of f.options) input.append(el('option', { value: o }, o[0].toUpperCase() + o.slice(1)));
        row.append(label, input);
      } else {
        input = el('input', { type: 'checkbox', id });
        row.append(label, input);
      }
      style.append(row);
      input.addEventListener(f.type === 'number' ? 'input' : 'change', () => {
        const raw = f.type === 'bool' ? input.checked : f.type === 'number' ? Number(input.value) : input.value;
        const r = TH.normalize(Object.assign({}, AP.theme, { style: Object.assign({}, AP.theme.style, { [key]: raw }) }));
        setTheme(r.theme);
      });
      AP.inputs[key] = { input, out, type: f.type, unit: f.unit === '%' ? '%' : f.unit ? ' ' + f.unit : '' };
    }

    const dlg = $('#appearance');
    $('#appearance-open').addEventListener('click', () => {
      $('#ap-json').value = TH.serialize(AP.theme, true);
      apStatus('');
      syncAppearance();
      dlg.showModal();
    });
    $('#ap-close').addEventListener('click', () => dlg.close());
    $('#ap-reset').addEventListener('click', () =>
      confirmThen('Reset all colours and style options to the default scheme?', () => {
        setTheme(TH.fromPreset(TH.DEFAULT_PRESET));
        $('#ap-json').value = TH.serialize(AP.theme, true);
        apStatus('Reset to the default scheme.');
      }));
    $('#ap-export').addEventListener('click', () => {
      const json = TH.serialize(AP.theme, true);
      $('#ap-json').value = json;
      const a = el('a', { download: 'croaster-theme.json' });
      a.href = URL.createObjectURL(new Blob([json + '\n'], { type: 'application/json' }));
      document.body.append(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(a.href), 1000);
    });
    $('#ap-copy').addEventListener('click', () => {
      const ta = $('#ap-json');
      ta.value = TH.serialize(AP.theme, true);
      const done = () => apStatus('Copied.');
      if (navigator.clipboard && window.isSecureContext) {
        navigator.clipboard.writeText(ta.value).then(done, () => apStatus('Copy failed. Select the text and copy it.', true));
      } else {
        // The board serves plain http, where the async clipboard API is not available.
        ta.select();
        if (document.execCommand('copy')) done();
        else apStatus('Select the text and copy it.', true);
      }
    });
    const importText = (text) => {
      const r = TH.parseImport(text);
      if (!r.theme) { apStatus('Not loaded: ' + r.errors.join('; ') + '.', true); return; }
      setTheme(r.theme);
      $('#ap-json').value = TH.serialize(AP.theme, true);
      apStatus(r.errors.length ? 'Loaded. Skipped: ' + r.errors.join('; ') + '.' : 'Loaded.', r.errors.length > 0);
    };
    $('#ap-import').addEventListener('click', () => importText($('#ap-json').value));
    $('#ap-import-file').addEventListener('change', (e) => {
      const file = e.target.files[0];
      if (!file) return;
      if (file.size > 64 * 1024) { apStatus('Not loaded: file is larger than 64 KB.', true); return; }
      file.text().then(importText, () => apStatus('Could not read the file.', true));
      e.target.value = '';
    });
  }

  function initAppearance() {
    let raw = null;
    try { raw = localStorage.getItem(KEY_THEME); } catch (_) { /* blocked: defaults */ }
    const m = TH.migrate(raw);
    AP.theme = m.theme;
    buildAppearance();
    TH.apply(AP.theme, document.documentElement);
    if (m.migrated || m.errors.length) saveTheme();
    syncAppearance();
  }

  function downloadCsv() {
    const csv = R.buildCsv(S.session, rorAt);
    const a = el('a');
    const d = new Date(S.session.startedAt);
    const pad = (v) => String(v).padStart(2, '0');
    a.download = 'roast-' + d.getFullYear() + pad(d.getMonth() + 1) + pad(d.getDate()) + '-' + pad(d.getHours()) +
      pad(d.getMinutes()) + '.csv';
    a.href = URL.createObjectURL(new Blob([csv], { type: 'text/csv' }));
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }

  function wire() {
    initAppearance();
    window.addEventListener('hashchange', () => setTab(location.hash.slice(1)));
    $('#stop').addEventListener('click', stopRoast);
    const askEnd = () => {
      if (recording()) confirmThen('End the roast? Recording stops; the board is not changed. The samples stay until the next Start.', endRoast);
    };
    $('#end-roast').addEventListener('click', askEnd);
    $('#end-after-drop').addEventListener('click', askEnd);

    $('#ror-smooth').checked = S.rorSmooth;
    $('#ror-smooth').addEventListener('change', (e) => {
      S.rorSmooth = e.target.checked;
      localStorage.setItem(KEY_ROR, S.rorSmooth ? '1' : '0');
      render();
    });
    $('#auto-tp').checked = S.autoTp;
    $('#auto-tp').addEventListener('change', (e) => {
      S.autoTp = e.target.checked;
      localStorage.setItem(KEY_TP, S.autoTp ? '1' : '0');
    });
    $('#undo-event').addEventListener('click', () => {
      if (!recording()) return;
      S.session.events.pop();
      saveSession(true);
      render();
    });
    $('#new-roast').addEventListener('click', () => startRoast());
    $('#csv').addEventListener('click', downloadCsv);
    $('#clear-roast').addEventListener('click', clearRoast);

    const chart = $('#chart');
    chart.addEventListener('pointermove', onChartPointer);
    chart.addEventListener('pointerleave', () => { S.cursorT = null; $('#readout').textContent = ''; renderChart(); });

    for (const b of $$('button[data-cmd]')) {
      b.addEventListener('click', () => confirmThen(b.dataset.confirm, () => {
        const cmd = JSON.parse(b.dataset.cmd);
        send(cmd);
        if (cmd.command === 'stop') expectStop();
      }));
    }
    for (const st of $$('.stepper')) {
      for (const b of $$('button[data-step]', st)) b.addEventListener('click', () => stepperClick(st, Number(b.dataset.step)));
    }
    for (const form of $$('form[data-send]')) {
      form.addEventListener('input', () => { form.dataset.touched = '1'; });
      form.addEventListener('submit', (ev) => {
        ev.preventDefault();
        const payload = formPayload(form);
        if (!payload) return;
        confirmThen(form.dataset.confirm, () => {
          send({ command: payload });
          delete form.dataset.touched;
          if (form.dataset.send === 'wifiConnect') form.reset();
        });
      });
    }
    $('#profile-add').addEventListener('click', () => {
      const last = S.profile[S.profile.length - 1];
      S.profile.push({ duration: 60, setpoint: last ? last.setpoint : '', interpolation: 'linear', fanValue: '' });
      saveJson(KEY_PROFILE, S.profile);
      renderProfile();
    });
    $('#profile-load').addEventListener('click', loadProfile);

    // A phone suspends a hidden page without closing its socket, and the board then blocks
    // on writes to it. So the socket is closed while hidden when nothing needs it (see
    // Link.mayPause) and on pagehide, and reopened when the page is shown again.
    const pause = () => {
      if (worker && !S.paused) { S.paused = true; worker.postMessage({ type: 'pause' }); }
    };
    const resume = () => {
      if (worker && S.paused) { S.paused = false; worker.postMessage({ type: 'resume' }); }
    };
    document.addEventListener('visibilitychange', () => {
      if (document.hidden) {
        saveSession(true);
        if (L.mayPause(recording(), S.info.controls, S.data)) pause();
        return;
      }
      resume();
      if (worker) worker.postMessage({ type: 'visible' }); // poll at once; the board has been fed by the worker meanwhile
      render();
    });
    window.addEventListener('pagehide', () => { saveSession(true); pause(); });
    window.addEventListener('pageshow', (e) => { if (e.persisted) resume(); });
    window.addEventListener('resize', () => { S.dirty = true; });
    // The top bar wraps to two rows on narrow screens; the sticky tabs sit below it.
    const bar = $('.topbar');
    const barHeight = () => document.documentElement.style.setProperty('--topbar-h', bar.offsetHeight + 'px');
    if (window.ResizeObserver) new ResizeObserver(barHeight).observe(bar);
    barHeight();

    // Redraw at most once per frame when something changed, and tick the clocks once a second.
    const frame = () => { if (S.dirty && !document.hidden) render(); requestAnimationFrame(frame); };
    requestAnimationFrame(frame);
    setInterval(() => { if (!document.hidden) { renderClock(); if (S.tab !== 'live') S.dirty = true; } }, 1000);
  }

  async function start() {
    S.session = loadJson(KEY_SESSION) || newSession();
    if (!Array.isArray(S.session.samples) || !Array.isArray(S.session.events)) S.session = newSession();
    const wasRecording = R.isRecording(S.session);
    R.resumeSession(S.session, Date.now(), RESUME_GAP_MS);
    if (wasRecording && !R.isRecording(S.session)) saveSession(true);
    try {
      const r = await fetch('info.json', { cache: 'no-store' });
      if (r.ok) Object.assign(S.info, await r.json());
    } catch (_) { /* not served by the board: see the ws and controls parameters below */ }
    // ?ws=ws://192.168.1.50:81&controls=1 points the page at a board when web/ is served from a computer.
    const query = new URLSearchParams(location.search);
    if (query.get('controls') === '1') S.info.controls = true;
    // Over https the page uses wss://<host>/ws, which the proxy forwards to the board (see Link.socketUrl).
    const url = L.socketUrl(location, S.info.wsPort, query.get('ws'));

    for (const e of $$('[data-needs="controls"]')) e.hidden = !S.info.controls;
    wire();
    renderProfile();
    setTab(location.hash.slice(1));

    worker = new Worker('link.worker.js');
    worker.onmessage = onWorker;
    worker.onerror = () => logReply('err', 'connection worker failed to start');
    worker.postMessage({ type: 'open', url, controls: S.info.controls });
  }

  start();
})();
