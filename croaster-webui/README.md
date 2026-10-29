# CroasterWebUi: browser UI served by the board

Open `http://<board ip>/` once the board is on WiFi (the IP is on the OLED).
The page has three sections:

- **Live**: BT, ET, RoR, heater, fan, setpoint and mode tiles; a BT/ET/RoR
  chart with heater and fan strips under it; event buttons (Charge, Turning
  point, Dry end, FC start/end, SC start/end, Drop) with a roast clock; CSV
  export. RoR is the device's (`DBT`/`DET`) or, with "Smoothed RoR", a 30 s
  least-squares slope of BT computed in the browser.
- **Monitoring**: every value the board reports (`getArtisanData`,
  `getControl`, `getDeviceInfo`) with its key and age. Nothing else.
- **Controls** (Croaster PID only): every control command, with the board's
  reply or error shown inline. STOP stays in the top bar on every section.
  `restartesp`, `erase`, `dummyToggle` and `wifiConnect` ask for confirmation.

The roast clock, events and samples live in the browser (`localStorage`), not
on the board. A reload keeps them; another browser does not see them.

## Security

There is no password. Anyone on the same network can open the page and, on
Croaster PID, run the heater. This matches Croaster's WebSocket on port 81,
which already accepts commands from anyone on the network. Use it on a
network you trust, or build with `CMON_ENABLE_WEBUI 0`.

## How it is built

| Path | What |
|:---|:---|
| `web/` | Page sources: `index.html`, `app.css`, `app.js` (UI), `roast.js` (pure roast math, also loaded by the tests), `link.worker.js` (WebSocket and polling) |
| `embed_assets.py` | Library extra script: gzips `web/` into `src/webui_assets.h`. Runs on every build and rewrites the header only when a source changes. Output is reproducible (gzip level 9, no timestamp, sorted files). Fails the build above 100 KB gzipped |
| `src/webui_assets.h` | Generated, committed. Its first lines carry the SHA-256 of the sources |
| `src/WebUiServer.*` | ESP-IDF `esp_http_server` serving the assets and `/info.json` |
| `test/` | Host tests (below) |

`python3 croaster-webui/embed_assets.py` regenerates the header by hand;
`--check` exits 1 if it is stale.

The server starts once the station is connected and WiFiManager's portal is
closed (the portal owns port 80 while it runs) and stops if the portal opens
again. It runs in its own task (core 0 on Croaster PID, away from the control
task; priority 3) and only hands out constant data: it never calls Croaster's
core or command handler. The page talks to the board through Croaster's
WebSocket on port 81, like Artisan does.

`/info.json` tells the page the board id, whether to show Controls, and the
WebSocket port. Both boards ship the same page; the monitor hides Controls.

Responses carry an ETag (hash of the gzipped asset) and `Cache-Control:
no-cache`, so the browser revalidates and gets `304` until the firmware
changes.

## Polling and the comms watchdog

The page polls `getArtisanData` every second, `getControl` every 5 s (Croaster
PID only) and `getDeviceInfo` every 30 s. Like any client, those polls feed
Croaster PID's comms watchdog: while the page is open, the watchdog does not
trip, even with Artisan closed.

The WebSocket and the poll timer run in a dedicated Web Worker. Browsers slow
timers on a hidden tab's page (Chrome to once a minute after five minutes),
which would trip the watchdog during a roast watched from another tab; worker
timers keep running.
When the tab becomes visible again the page asks for an immediate poll.

What still stops the polls, and so trips the watchdog: closing the tab or the
browser, the computer sleeping or its lid closing, losing WiFi, and iOS or
iPadOS Safari, which suspends the whole page in the background. Those trips
are the watchdog doing its job.

## Development

Open `web/index.html` from any static server and point it at a board:
`http://localhost:8000/?ws=ws://192.168.1.50:81&controls=1`.

## Tests

```bash
node --test croaster-webui/test/*.test.js          # roast math, CSV, page checks
python3 -m unittest discover -s croaster-webui/test # asset pipeline
```

`roast.test.js` covers the least-squares RoR, turning-point detection, unit
conversion, axis ticks, clock format and CSV. `web_check.test.js` parses every
script and checks that the page loads nothing from outside the board, that
every element id the script uses exists, that every form has a command, and
that risky commands ask first. `test_embed_assets.py` checks the output is
reproducible, the committed header is current and the page is under budget.
