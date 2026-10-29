// Owns the WebSocket to Croaster (port 81) and the polling. It runs in a dedicated
// worker because browsers throttle timers on the page itself when the tab is
// hidden; a slowed poll would let the board's comms watchdog cut the heater while
// the roast is still being watched from another tab.
//
// Page -> worker: {type:'open', url, controls}, {type:'send', payload},
//                 {type:'visible'} (poll now), {type:'pause'} (close, no retry),
//                 {type:'resume'} (reconnect after a pause).
// Worker -> page: {type:'status', state, detail}, {type:'message', msg, polled, at},
//                 {type:'sent', payload, ok}, {type:'stats', ...} (connection panel).
'use strict';

const POLL_MS = 1000;        // getArtisanData: 1 Hz samples, and what feeds the watchdog
const CONTROL_EVERY = 5;     // getControl every 5th poll
const INFO_EVERY = 30;       // getDeviceInfo every 30th poll
const STALE_MS = 10000;      // no message for this long on an open socket: drop it and reconnect
const CONNECT_MS = 8000;     // handshake not done after this: give up on the attempt and retry
const RETRY_MAX_MS = 10000;
const ID_BASE = 100000;      // poll ids, distinct from the small ids Artisan uses

let url = '';
let controls = false;
let ws = null;
let paused = false;
let tick = 0;
let nextId = ID_BASE;
let lastRxMs = 0;
let retryMs = 1000;
let retryTimer = 0;
let connectTimer = 0;
const pending = new Map(); // poll id -> send time, not yet answered
const stats = { url: '', state: 'connecting', attempts: 0, opens: 0, openedAt: 0, lastRxAt: 0,
  rttMs: null, closeCode: 0, closeReason: '', closedAt: 0, detail: '' };

function status(state, detail) {
  stats.state = state;
  stats.detail = detail || '';
  postMessage({ type: 'status', state, detail: detail || '' });
  postStats();
}

function postStats() {
  postMessage(Object.assign({ type: 'stats' }, stats));
}

function send(payload) {
  if (!ws || ws.readyState !== WebSocket.OPEN) return false;
  if (ws.bufferedAmount > 4096) return false; // link stalled: do not queue up stale polls
  ws.send(typeof payload === 'string' ? payload : JSON.stringify(payload));
  return true;
}

// Detach the socket and close it in the background, so a board that has stopped
// answering cannot hold the page in "open" or "closing" while the close handshake waits.
function drop(code, reason) {
  const old = ws;
  ws = null;
  clearTimeout(connectTimer);
  pending.clear();
  if (old) {
    old.onopen = old.onmessage = old.onclose = old.onerror = null;
    try { old.close(); } catch (_) { /* already closed */ }
  }
  stats.closeCode = code;
  stats.closeReason = reason;
  stats.closedAt = Date.now();
}

function poll() {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  const now = Date.now();
  if (now - lastRxMs > STALE_MS) {
    drop(0, 'no message for ' + STALE_MS / 1000 + ' s');
    scheduleRetry(stats.closeReason);
    return;
  }
  const id = nextId;
  nextId = nextId >= ID_BASE + 99999 ? ID_BASE : nextId + 1;
  if (send({ command: 'getArtisanData', id })) {
    pending.set(id, now);
    if (pending.size > 30) pending.clear(); // replies lost
  }
  if (controls && tick % CONTROL_EVERY === 0) send({ command: 'getControl' });
  if (tick % INFO_EVERY === 0) send({ command: 'getDeviceInfo' });
  tick++;
}

function connect() {
  clearTimeout(retryTimer);
  if (paused) return;
  stats.attempts++;
  stats.url = url;
  status('connecting', url);
  let sock;
  try {
    sock = new WebSocket(url);
  } catch (err) {
    stats.closeCode = 0;
    stats.closeReason = String(err && err.message || err);
    stats.closedAt = Date.now();
    scheduleRetry(stats.closeReason);
    return;
  }
  ws = sock;
  connectTimer = setTimeout(() => {
    if (ws !== sock || sock.readyState !== WebSocket.CONNECTING) return;
    drop(0, 'no handshake within ' + CONNECT_MS / 1000 + ' s');
    scheduleRetry(stats.closeReason);
  }, CONNECT_MS);
  sock.onopen = () => {
    clearTimeout(connectTimer);
    retryMs = 1000;
    lastRxMs = Date.now();
    tick = 0;
    stats.opens++;
    stats.openedAt = lastRxMs;
    status('open', url);
    poll();
  };
  sock.onmessage = (ev) => {
    lastRxMs = Date.now();
    stats.lastRxAt = lastRxMs;
    let msg;
    try { msg = JSON.parse(ev.data); } catch (_) { msg = { text: String(ev.data) }; }
    let polled = false;
    if (msg && typeof msg.id === 'number' && pending.has(msg.id)) {
      stats.rttMs = lastRxMs - pending.get(msg.id);
      pending.delete(msg.id);
      polled = true;
    }
    postMessage({ type: 'message', msg, polled, at: lastRxMs });
    if (polled) postStats();
  };
  sock.onclose = (ev) => {
    if (ws !== sock) return;
    drop(ev.code, ev.reason || '');
    scheduleRetry('closed');
  };
  sock.onerror = () => { /* onclose follows and handles the retry */ };
}

function scheduleRetry(detail) {
  status('closed', detail);
  clearTimeout(retryTimer);
  if (paused) return;
  retryTimer = setTimeout(connect, retryMs);
  retryMs = Math.min(retryMs * 2, RETRY_MAX_MS);
}

setInterval(poll, POLL_MS);

onmessage = (e) => {
  const m = e.data || {};
  if (m.type === 'open') {
    url = m.url;
    controls = !!m.controls;
    paused = false;
    drop(0, '');
    connect();
  } else if (m.type === 'send') {
    postMessage({ type: 'sent', payload: m.payload, ok: send(m.payload) });
  } else if (m.type === 'visible') {
    poll();
  } else if (m.type === 'pause') {
    if (paused) return;
    paused = true;
    clearTimeout(retryTimer);
    drop(1000, 'page hidden');
    status('paused', 'page hidden');
  } else if (m.type === 'resume') {
    if (!paused) return;
    paused = false;
    retryMs = 1000;
    connect();
  }
};
