// Connection helpers shared by the page and the host tests (node croaster-webui/test).
// Pure functions only: no DOM, no network.
(function (root, factory) {
  'use strict';
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.Link = api;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  // WebSocket URL for a page loaded from `loc` (location-like: protocol, hostname, host).
  // Over http the board's own WebSocket port is used. A page loaded over https (a reverse
  // proxy in front of the board) cannot open ws:// (mixed content), and the proxy is the
  // only thing that can terminate TLS, so it must forward wss://<host>/ws to port 81.
  function socketUrl(loc, wsPort, override) {
    if (override) return override;
    if (loc.protocol === 'https:') return 'wss://' + loc.host + '/ws';
    return 'ws://' + loc.hostname + ':' + wsPort + '/';
  }

  // True when the page may close its socket while hidden. Only the polls feed the board's
  // comms watchdog, which acts only while heating, so the socket stays open while a roast
  // records, the heater is on or the board is in a mode other than Manual.
  function mayPause(recording, controls, data) {
    if (recording) return false;
    if (!controls || !data) return true;
    return data.BurnerVal === 0 && (data.Mode === undefined || data.Mode === 'Manual');
  }

  // Short text for a WebSocket close code.
  function closeText(code) {
    const known = { 1000: 'normal', 1001: 'going away', 1006: 'dropped (no close frame)', 1011: 'server error' };
    return code ? code + (known[code] ? ' ' + known[code] : '') : '—';
  }

  return { socketUrl, mayPause, closeText };
});
