# CONTEXT.md — Croaster firmware (current state)

## What this repo is
The **Croaster** coffee-roaster firmware, structured as a reusable
display/pin-agnostic library (`croaster/`) plus standalone per-board PlatformIO
projects (`boards/<board>/`). It is a git submodule of the ICRM
Flutter app (remote `git@github.com:IiemB/Croaster.git`) and is consumed via
`lib_deps = https://github.com/IiemB/Croaster.git`.

## Repo layout
| Path | Purpose |
|:---|:---|
| `croaster/src/` | Library core: `CroasterCore`, `CroasterDisplay` (interface), `CroasterApp` (begin()/loop()), `CroasterCommandHandler`, `CroasterBleManager`, `CroasterWebSocketManager`, `CroasterOtaHandler`, `CroasterWiFiManager`, `CroasterDeviceIdentity`, `CroasterPinConfig`, `CroasterConstants` |
| `boards/common/` | Shared SSD1306 display + animation (small lib, added via `../common`) |
| `boards/esp32c3/` | ESP32-C3 Super Mini — SSD1306 OLED; custom OTA partition (`custom32c3sm.csv` lives in this folder) |
| `boards/esp8266/` | NodeMCU / ESP-12E — SSD1306 OLED |
| `boards/esp32s3/` | ES3C28P 2.8" ILI9341 LCD — LVGL 9 UI + FT6336G touch (`src/ui/`) |
| `boards/esp32s3-flex/` | Flex **control** board (Lonely Binary S3 N16R8): SSR heater, AC-dimmer/PWM fan, 2× MAX31855, OLED; master `src/config.h` + `ConfigCheck.h`; Artisan `.aset` in `artisan/` |
| `control-core/` | Pure control logic lib (PID, relay autotune, profiles, MAX31855/MAX6675 decode, command protocol); host tests in `boards/esp32s3-flex/test` (`pio test -e native`) |
| `control-esp32/` | ESP32 roaster lib: control task, SSR, fan drivers, probes, NVS, `ControlPlugin` (hooks into `CroasterApp`) |
| `docs/` | `design.md` (architecture + decisions D1–D10), `tickets.md` (remaining work), `analysis.md` (yaeger + Croaster review), `how.md` (walkthrough), `architecture.md` (shape + open decisions), `control-parity.md` (yaeger vs ours), `artisan-settings.md` (every `.aset` setting with source) |
| `builds/` | gitignored firmware output of `./build_all.sh` |

## Boards
| Board | MCU | Display | Env | Flash (build_all) |
|:---|:---|:---|:---|:---|
| `esp32c3` | ESP32-C3 | 128×64 SSD1306 | `esp32c3` | ~85.7% |
| `esp8266` | ESP8266 | 128×64 SSD1306 | `esp8266` | ~39.8% |
| `esp32s3` (ES3C28P) | ESP32-S3 | 320×240 ILI9341 + LVGL 9 | `esp32s3` | ~29.0% |
| `esp32s3-flex` | ESP32-S3 N16R8 | 0.96" 128×64 SSD1306 I2C (`boards/common` display; confirmed by Shawn) | `esp32s3-flex` | 26.2% (1 719 671 B), RAM 56 788 B |

## How a board is wired
- `main.cpp` creates `CroasterCore core(dummyMode, pins)`, a display subclass
  (e.g. `CroasterDisplaySSD1306` / `CroasterDisplayS3`), and
  `CroasterApp app(core, &display, ledPin, ledOnLevel)`, then calls
  `app.begin()` / `app.loop()`.
- Board id is hardcoded per board via
  `CroasterDeviceIdentity::setBoardName("esp32s3")` in its `main.cpp`, read
  by `CroasterDeviceIdentity::boardName()` (no compile-time board guards;
  defaults to `unknown` until set).
- Builds: `build_all.sh` builds only the **release** env of each board
  (`build_type = release`; `pio run -e <board>`); `./upload_monitor.sh <board>`
  builds + uploads the **debug** variant (`<board>-debug`, `extends` the
  release env) to the connected board, then opens the serial monitor.
- Flex board: control task (core 1, prio max-3) owns the SPI bus and
  publishes a snapshot; `RoasterProbe` feeds it to `CroasterCore` via the
  sensor-injection constructor; `ControlPlugin` registers control commands
  (`onJsonCommand`/`onCommand`), adds data fields via `CroasterCore::onJsonData`,
  runs the comms watchdog. Details: `docs/design.md`.
- Heater/fan control is 100 % firmware-side (PID on the ESP32). Clients send
  setpoints/modes/percentages only. Two wire formats reach the same parser
  (`controlproto`): nested `{"command":{...}}` and yaeger's flat keys /
  `setBurner`/`setFan`/`setPreferences`/`getPreferences` (`parseLegacy`, routed
  via the new `CroasterCommandHandler::onOtherJson` hook for messages without
  `"command"`). Data adds `BurnerVal`, `FanVal`, `SV`, `Mode`, `trip`, `Amb`,
  `Target`, `pidKp/Ki/Kd`, `wifiStrength`.
- Comms watchdog: no message through Croaster's command handler
  (`lastCommandMillis()`, WebSocket or BLE) for the timeout while heating
  (not in Profile) → heater off, fan to cooldown, `trip:"comms"`. Defaults
  `FLEX_WATCHDOG_ENABLED`/`FLEX_WATCHDOG_TIMEOUT_S`; runtime command
  `{"command":{"watchdog":...}}` saves to NVS.
- Artisan: `.aset` verified against Artisan source (`docs/artisan-settings.md`).
  Input 1 = BT, Input 2 = ET, one `getArtisanData` per second, extra devices
  112/113 for Heater/Fan/SV, Artisan's own PID buttons off.
- Library features added in 0.52: roast timer (`CroasterCore::roastTimerStart/
  Pause/Reset`), `getDeviceInfo` now reports `board`/`darkMode`/`brightness`
  (display state via `CroasterDisplay::isDarkMode()/getBrightness()`), and a
  BLE-guarded `CroasterApp::ble()` accessor.

## Git state (2026-10-26)
- Shawn's fork: branch `rework/roaster` off `master` (`0845ba8`), pushed to
  `origin/rework/roaster` (shawns1234/Croaster). Adds the roaster board + libs + docs and these core changes:
  sensor-injection ctor, `onJsonData()`, MAX31855 (`CroasterThermocoupleType`),
  `#ifndef CROASTER_HAS_BLE`, negative RoR no longer forced to 0,
  `CroasterCommandHandler::onOtherJson()`.
- Board renamed `esp32s3-roaster` → `esp32s3-flex` (2026-10-26, D5 resolved
  by Shawn): folder, envs, board id, `.aset`.
- Decisions pending (Shawn): design.md D1 (top-level control libs), D3
  (force-included config), D6 (watchdog via core `lastCommandMillis()`), D10 (flex ini
  vs Croaster's `boards/esp32s3` ini), D8 (`onOtherJson` for yaeger flat keys), D9 (10 s
  traffic watchdog vs instant cut on disconnect).

## Git state (2026-08-30, upstream)
- Branch `dev` (HEAD `3f52109`, tracks `origin/dev`); the `boards/` + `croaster/`
  restructure, per-board `setBoardName()`, and the 0.62 bump are committed
  (merged from `decoupling` via PR #31). Working tree **clean** after this batch.
- The ESP32-S3 LVGL implementation was ported from the old single-project
  `ES3C28P` branch into `boards/esp32s3/`
  (`git show ES3C28P:<path>` is the source of the UI).
- `build_all.sh` emits `builds/Croaster_<board>_<version>.bin` (version read
  from `croaster/src/CroasterConstants.h`).
- This batch adds `format.sh` + `.clang-format` (all sources reformatted) and
  the esp32s3 `darkMode` / `brightness` custom commands.
- ⚠️ **Downstream:** the ICRM app consumes this repo via
  `lib_deps = https://github.com/IiemB/Croaster.git`, which needs `library.json`
  at the repo root — update it to the `croaster/` subfolder (or add a root
  `library.json`).

## Next steps
- Flex: hardware bring-up and the rest of `docs/tickets.md` (T2–T14).
- Update the ICRM app's `lib_deps` for the `croaster/` subfolder.
- Flash + verify the esp32s3 build on real hardware (touch, chart swipe,
  dark mode, OTA over BLE/WebSocket).
- Confirm `boardName()` strings against the ICRM app's `CroasterBoardTypes`.

## Gotchas
- `onJsonCommand(key, fn)`: every registered callback gets the **whole**
  `command` object when its key is present; parse only your own key.
- `onOtherJson(fn)` gets every JSON object without `"command"` (any client,
  WebSocket or BLE): return `""` for messages that aren't yours.
- Artisan WebSocket: replies must echo the request `id`. Command strings in the
  `.aset` go through Python `str.format`, so literal braces are doubled.
  Artisan's "Control" button is its own PC-side PID; keep it off.
- Keep Croaster's unit and Artisan both in °C: command temperatures are read
  in Croaster's unit.
- NVS (`Preferences`) is not usable from global constructors: load settings in
  `setup()`.
- Unpinned `platform = espressif32` resolves to whatever espressif32 is
  installed (a pioarduino install made `esp32c3` fail); pin the version.
- `custom_sdkconfig` (pioarduino) writes `managed_components/`, `sdkconfig.*`,
  `CMakeLists.txt`, `src/idf_component.yml` into the board folder (gitignored in
  `esp32s3-flex`). `MBEDTLS_SSL_IN/OUT_CONTENT_LEN` need
  `MBEDTLS_ASYMMETRIC_CONTENT_LEN=y` to take effect.
- `.clang-format` is LLVM-based with attached braces; AGENTS.md says Allman.
  Sources follow `.clang-format`.
- PlatformIO not on PATH → `~/.platformio/penv/bin/pio` or `PIO=`.
- Libs are **live-linked** via `symlink://` lib_deps
  (`Croaster=symlink://../../croaster`, `CroasterDisplaySSD1306=symlink://../common`)
  — editing `croaster/src/` needs no `.pio/libdeps` cleanup.
- ESP32-C3: WiFiManager AP can fail to broadcast under the PIO core (2.0.17) —
  fixed via DIO flash mode + `WiFi.setTxPower(WIFI_POWER_18_5dBm)` in the
  configMode callback (see `boards/esp32c3/` notes).
- ESP32-S3 (LVGL): heavy UI work must run on its FreeRTOS task (Core 0), never
  the Arduino loop task; the theme switch is deferred via
  `themeRebuildPending` + `LvglUi::rebuildTheme()` on the LVGL task.
- ESP32-S3: WSS connect needs the mbedTLS PSRAM/buffer Kconfig overrides in
  `boards/esp32s3/esp32s3-sdkconfig.defaults` (otherwise
  `MBEDTLS_ERR_SSL_ALLOC_FAILED`).
