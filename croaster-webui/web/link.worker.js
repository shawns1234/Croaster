// Owns the WebSocket to Croaster (port 81) and the polling. It runs in a dedicated
// worker because browsers throttle timers on the page itself when the tab is
// hidden; a slowed poll would let the board's comms watchdog cut the heater while
// the roast is still being watched from another tab.
//
// Page -> worker: {type:'open', url, controls}, {type:'send', payload},
//                 {type:'visible'} (poll now).
// Worker -> page: {type:'status', state, detail}, {type:'message', msg, polled, at}.
'use strict';

const POLL_MS = 1000;        // getArtisanData: 1 Hz samples, and what feeds the watchdog
const CONTROL_EVERY = 5;     // getControl every 5th poll
const INFO_EVERY = 30;       // getDeviceInfo every 30th poll
const STALE_MS = 5000;       // no message for this long on an open socket: reconnect
const ID_BASE = 100000;      // poll ids, distinct from the small ids Artisan uses

let url = '';
let controls = false;
let ws = null;
let tick = 0;
let nextId = ID_BASE;
let lastRxMs = 0;
let retryMs = 1000;
let retryTimer = 0;
const pending = new Set(); // poll ids not yet answered

function status(state, detail) {
  postMessage({ type: 'status', state, detail: detail || '' });
}

function send(payload) {
  if (!ws || ws.readyState !== WebSocket.OPEN) return false;
  if (ws.bufferedAmount > 4096) return false; // link stalled: do not queue up stale polls
  ws.send(typeof payload === 'string' ? payload : JSON.stringify(payload));
  return true;
}

function poll() {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  if (Date.now() - lastRxMs > STALE_MS) { ws.close(); return; }
  const id = nextId;
  nextId = nextId >= ID_BASE + 99999 ? ID_BASE : nextId + 1;
  if (send({ command: 'getArtisanData', id })) {
    pending.add(id);
    if (pending.size > 30) pending.clear(); // replies lost on a reconnect
  }
  if (controls && tick % CONTROL_EVERY === 0) send({ command: 'getControl' });
  if (tick % INFO_EVERY === 0) send({ command: 'getDeviceInfo' });
  tick++;
}

function connect() {
  clearTimeout(retryTimer);
  status('connecting', url);
  try {
    ws = new WebSocket(url);
  } catch (err) {
    scheduleRetry(String(err));
    return;
  }
  ws.onopen = () => {
    retryMs = 1000;
    lastRxMs = Date.now();
    tick = 0;
    status('open', url);
    poll();
  };
  ws.onmessage = (ev) => {
    lastRxMs = Date.now();
    let msg;
    try { msg = JSON.parse(ev.data); } catch (_) { msg = { text: String(ev.data) }; }
    const polled = msg && typeof msg.id === 'number' && pending.delete(msg.id);
    postMessage({ type: 'message', msg, polled, at: lastRxMs });
  };
  ws.onclose = () => {
    ws = null;
    pending.clear();
    scheduleRetry('closed');
  };
  ws.onerror = () => { /* onclose follows and handles the retry */ };
}

function scheduleRetry(detail) {
  status('closed', detail);
  clearTimeout(retryTimer);
  retryTimer = setTimeout(connect, retryMs);
  retryMs = Math.min(retryMs * 2, 10000);
}

setInterval(poll, POLL_MS);

onmessage = (e) => {
  const m = e.data || {};
  if (m.type === 'open') {
    url = m.url;
    controls = !!m.controls;
    if (ws) ws.close(); else connect();
  } else if (m.type === 'send') {
    postMessage({ type: 'sent', payload: m.payload, ok: send(m.payload) });
  } else if (m.type === 'visible') {
    poll();
  }
};
