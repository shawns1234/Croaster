// Host tests for web/link.js and the connection worker (web/link.worker.js, run in a
// vm with a fake WebSocket and fake timers). Run: node --test croaster-webui/test/*.test.js
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const L = require('../web/link.js');

test('socketUrl: board port over http, same-origin /ws over https, override wins', () => {
  const http = { protocol: 'http:', hostname: '192.168.1.50', host: '192.168.1.50' };
  const https = { protocol: 'https:', hostname: 'roaster.example', host: 'roaster.example' };
  assert.equal(L.socketUrl(http, 81), 'ws://192.168.1.50:81/');
  assert.equal(L.socketUrl(https, 81), 'wss://roaster.example/ws');
  assert.equal(L.socketUrl({ protocol: 'https:', hostname: 'r.example', host: 'r.example:8443' }, 81),
    'wss://r.example:8443/ws');
  assert.equal(L.socketUrl(https, 81, 'ws://10.0.0.9:81/'), 'ws://10.0.0.9:81/');
});

test('mayPause: never while recording, heating or outside Manual', () => {
  assert.equal(L.mayPause(true, false, null), false);
  assert.equal(L.mayPause(false, false, { BT: 20 }), true); // monitor: no heater
  assert.equal(L.mayPause(false, true, null), true); // nothing received yet
  assert.equal(L.mayPause(false, true, { BurnerVal: 0, Mode: 'Manual' }), true);
  assert.equal(L.mayPause(false, true, { BurnerVal: 30, Mode: 'Manual' }), false);
  assert.equal(L.mayPause(false, true, { BurnerVal: 0, Mode: 'PID' }), false);
  assert.equal(L.mayPause(false, true, { BurnerVal: 0, Mode: 'Profile' }), false);
  assert.equal(L.mayPause(false, true, { Mode: 'Manual' }), false); // heater not reported
});

test('closeText', () => {
  assert.equal(L.closeText(1006), '1006 dropped (no close frame)');
  assert.equal(L.closeText(4000), '4000');
  assert.equal(L.closeText(0), '—');
});

// ---------- worker ----------
function loadWorker() {
  let now = 1000000;
  const timers = [];
  let seq = 0;
  const sockets = [];
  const out = [];
  class FakeSocket {
    constructor(url) {
      this.url = url; this.readyState = 0; this.sent = []; this.bufferedAmount = 0; this.closed = false;
      sockets.push(this);
    }
    send(t) { this.sent.push(JSON.parse(t)); }
    close() { this.closed = true; this.readyState = 3; }
    open() { this.readyState = 1; this.onopen && this.onopen(); }
    reply(obj) { this.onmessage && this.onmessage({ data: JSON.stringify(obj) }); }
    serverClose(code) { this.readyState = 3; this.onclose && this.onclose({ code, reason: '' }); }
  }
  FakeSocket.CONNECTING = 0; FakeSocket.OPEN = 1; FakeSocket.CLOSING = 2; FakeSocket.CLOSED = 3;
  const add = (fn, ms, every) => { const id = ++seq; timers.push({ id, fn, at: now + ms, every }); return id; };
  const ctx = {
    WebSocket: FakeSocket,
    Date: { now: () => now },
    JSON, Map, Math, String, Object,
    setTimeout: (fn, ms) => add(fn, ms, 0),
    setInterval: (fn, ms) => add(fn, ms, ms),
    clearTimeout: (id) => { const i = timers.findIndex((t) => t.id === id); if (i >= 0) timers.splice(i, 1); },
    postMessage: (m) => out.push(m),
  };
  vm.createContext(ctx);
  vm.runInContext(fs.readFileSync(path.join(__dirname, '..', 'web', 'link.worker.js'), 'utf8'), ctx);
  const advance = (ms) => {
    const end = now + ms;
    for (;;) {
      timers.sort((a, b) => a.at - b.at);
      const t = timers[0];
      if (!t || t.at > end) break;
      now = t.at;
      if (t.every) t.at += t.every; else timers.shift();
      t.fn();
    }
    now = end;
  };
  const post = (m) => ctx.onmessage({ data: m });
  const last = (type) => out.filter((m) => m.type === type).pop();
  return { sockets, out, advance, post, last, ctx };
}

test('worker: polls, matches replies by id and reports the round trip', () => {
  const w = loadWorker();
  w.post({ type: 'open', url: 'ws://b:81/', controls: true });
  const s = w.sockets[0];
  w.advance(100);
  s.open();
  assert.deepEqual(s.sent.map((m) => m.command), ['getArtisanData', 'getControl', 'getDeviceInfo']);
  w.advance(40);
  s.reply({ id: s.sent[0].id, data: { BT: 20 } });
  assert.equal(w.last('message').polled, true);
  assert.equal(w.last('stats').rttMs, 40);
  s.reply({ id: 0, data: { BT: 20 } }); // the board's broadcast: not a polled sample
  assert.equal(w.last('message').polled, false);
  assert.equal(w.last('status').state, 'open');
});

test('worker: a silent socket is dropped after 10 s and a new one opened without waiting for the close', () => {
  const w = loadWorker();
  w.post({ type: 'open', url: 'ws://b:81/', controls: false });
  w.sockets[0].open();
  w.advance(9500);
  assert.equal(w.last('status').state, 'open');
  w.advance(1500);
  assert.equal(w.last('status').state, 'closed');
  assert.match(w.last('stats').closeReason, /no message for 10 s/);
  assert.equal(w.sockets[0].closed, true);
  assert.equal(w.sockets[0].onclose, null);
  w.advance(1000);
  assert.equal(w.sockets.length, 2);
  assert.equal(w.last('stats').attempts, 2);
});

test('worker: a handshake that hangs is abandoned after 8 s; backoff grows to 10 s', () => {
  const w = loadWorker();
  w.post({ type: 'open', url: 'ws://b:81/', controls: false });
  w.advance(8000);
  assert.equal(w.last('status').state, 'closed');
  assert.match(w.last('stats').closeReason, /no handshake within 8 s/);
  w.advance(1000); // retry after 1 s
  assert.equal(w.sockets.length, 2);
  w.sockets[1].serverClose(1006);
  assert.equal(w.last('stats').closeCode, 1006);
  w.advance(1999);
  assert.equal(w.sockets.length, 2);
  w.advance(1); // then 2 s
  assert.equal(w.sockets.length, 3);
});

test('worker: a constructor error (mixed content) is reported and retried', () => {
  const w = loadWorker();
  w.ctx.WebSocket = function () { throw new Error('insecure WebSocket from an https page'); };
  w.post({ type: 'open', url: 'ws://b:81/', controls: false });
  assert.equal(w.last('status').state, 'closed');
  assert.match(w.last('stats').closeReason, /insecure/);
});

test('worker: pause closes the socket and stops retrying; resume reconnects', () => {
  const w = loadWorker();
  w.post({ type: 'open', url: 'ws://b:81/', controls: false });
  w.sockets[0].open();
  w.post({ type: 'pause' });
  assert.equal(w.sockets[0].closed, true);
  assert.equal(w.last('status').state, 'paused');
  w.advance(60000);
  assert.equal(w.sockets.length, 1);
  assert.equal(w.sockets[0].sent.length, 2); // the first poll only; nothing while paused
  w.post({ type: 'resume' });
  assert.equal(w.sockets.length, 2);
  assert.equal(w.last('status').state, 'connecting');
});
