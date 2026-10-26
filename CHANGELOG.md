# Changelog — Croaster Firmware

All notable changes to the Croaster firmware are documented here.  
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Unreleased] — flex control board (branch rework/roaster)

### Added
- **Flex °F by default**: `FLEX_TEMP_UNIT` in `config.h` sets Croaster's
  unit at boot (screen, telemetry, setpoints); control limits stay in °C. The
  Artisan `.aset` is now in °F mode.
- **`CroasterCommandHandler::lastCommandMillis()`**: when the handler last
  accepted a message (WebSocket or BLE), for add-ons such as a comms watchdog.
- **`boards/esp32s3-flex`** ("flex") — heater + fan control board for the Lonely Binary
  ESP32-S3 N16R8: PID heater via SSR (time-proportional window), RobotDyn AC
  dimmer or PWM fan, roast profiles, relay autotune, safety trips (over-temp,
  sensor fault, no mains, OTA, comms lost), Croaster's shared SSD1306 screen,
  Artisan settings file. Single master `src/config.h` validated at
  compile time by `src/ConfigCheck.h`.
- **`control-core/`** (pure, host-tested) and **`control-esp32/`** (ESP32)
  libraries; host tests (`pio test -e native`, 21 now).
- **MAX31855 support** in the library: `CroasterMAX31855`,
  `CroasterThermocoupleType`, optional third `CroasterCore` constructor argument
  (default MAX6675).
- **`CroasterCore(bool, Thermocouple* bt, Thermocouple* et)`** — inject sensors
  owned elsewhere (e.g. a control task that owns the SPI bus).
- **`CroasterCore::onJsonData(fn)`** — boards add fields to every data payload.
- `docs/design.md`, `docs/tickets.md`, `docs/analysis.md`.
- **yaeger control parity**: `setBurner`/`setFan`/`setPreferences`/
  `getPreferences`; data gains `Amb`, `Target`, `pidKp/Ki/Kd`, `wifiStrength`;
  PID resets when the setpoint goes from 0 to > 0. 16 host tests.
- `docs/how.md`, `docs/architecture.md`, `docs/control-parity.md`,
  `docs/artisan-settings.md`.

### Fixed
- **Flex Artisan settings: Heater and Fan LCDs swapped, SV LCD empty.**
  `channel_nodes` is now `BT, ET, BurnerVal, FanVal, SV`.
- **Flex `Amb` read 0 °C (32 °F) with no BT chip fitted.** It now uses
  whichever MAX31855 answers.
- **Flex autotune exits.** Switching to Manual or PID during autotune no
  longer keeps the relay's 100 % heater output, and a setpoint change during
  autotune stops it instead of being ignored.
- **Compiler warnings in the library.** `CroasterBleManager`'s constructor
  initializes its members in declaration order (`-Wreorder`).
  `CroasterDeviceIdentity::uniqueChipId()` formats the chip id with `PRIX32`
  and `snprintf` (`-Wformat` on ESP32, where `uint32_t` is `unsigned long`);
  the id string is unchanged.
- **Artisan `.aset`**: BT/ET were swapped (inherited from yaeger); Artisan's
  PC-side PID/Control buttons turned off (our PID is on the ESP32); Heater,
  Fan and SV curves via +WebSocket 34/56; setpoint slider; PID/Manual/
  Autotune/Stop/Clear trip buttons; 1 s sampling; path `WebSocket`.

### Changed
- **PID input defaults to BT (D18).** `FLEX_PID_INPUT` in `config.h`
  (default `FLEX_PID_INPUT_BT`); `{"command":{"pidInput":"ET"}}` still
  switches at runtime and a saved choice in NVS wins over the default.
  Default gains stay yaeger's; autotune on BT.
- **Cooldown fan: 65 % after stop, 100 % after any trip (D19).** `cooldownFan`
  has `stop` and `trip` values: `FLEX_COOLDOWN_FAN_STOP`/`_TRIP` in `config.h`
  (replaces `FLEX_COOLDOWN_FAN`), `{"command":{"cooldownFan":{"stop":N,"trip":N}}}`
  (a plain number sets `stop`), saved in NVS, both in `getControl`. Every
  safety trip now sets the fan to the trip speed when it starts; before, only
  comms and over-temperature trips touched the fan.
- **yaeger flat keys dropped (D8).** `{"BurnerVal":v}`, `FanVal`, `Setpoint`,
  `Mode` and `Target` messages without `"command"` are ignored, and the
  `CroasterCommandHandler::onOtherJson()` hook they needed is removed, so that
  part of core matches upstream. yaeger's named commands (`setBurner`,
  `setFan`, `setPreferences`, `getPreferences`) still work.
- Per-probe fault filter moved from `hw/thermocouples` into the pure
  `ProbeChannel` (`control-core`), same behaviour, now host-tested; tests for
  MAX31855 fault bits and two independent channels (19 host tests).
- **Comms watchdog counts every client message** through
  `CroasterCommandHandler` (WebSocket and BLE, `getArtisanData` included), so
  the `getData` alias is gone and the `.aset` polls `getArtisanData`. It can be
  turned off and its timeout changed: `FLEX_WATCHDOG_ENABLED`,
  `FLEX_WATCHDOG_TIMEOUT_S`, and `{"command":{"watchdog":...}}` (saved in NVS).
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- **Names say flex or control, not roaster** (map in `docs/design.md`, "Names"):
  `control-core/` (`controlproto`), `control-esp32/` (`Controller`,
  `ControlPlugin`, `ControlSettings`, `SafetyLimits`), `FLEX_*` macros,
  `getControl`, task and NVS namespace `control`.
- **Flex screen is Croaster's `CroasterDisplaySSD1306`** (boards/common), with a
  new `setHeaderText(fn)` so the header shows heater/fan/mode or the trip.
  `RoasterOledDisplay`, the U8g2 dependency and the SH1106 option are gone.
- **Board renamed `esp32s3-roaster` → `esp32s3-flex`**: folder (history kept
  via `git mv`), envs `esp32s3-flex[-debug|-pioarduino]`,
  board id `esp32s3-flex`,
  `artisan/croaster-flex.aset`.
- Flex env list: the `-pioarduino-sdkconfig` env is gone; every env sets
  `board_build.sdkconfig_path`, as Croaster's esp32s3 board does.
- design.md: decisions D11–D15 (MAX31855 decoders, display, OTA, config
  wizard, MAX6675 dependency), D8 rewritten, a file-by-file inventory against
  Croaster, and a table of decisions changed on 2026-10-26.
- Comms watchdog: 10 s (was 30 s), and a trip also sets the cooldown fan.
- Display default confirmed: 0.96" I2C 128×64 SSD1306.
- **RoR keeps negative values** (falling temperature was reported as 0).
- `CROASTER_HAS_BLE` can be preset by a board (`#ifndef`).
- `format.sh` also formats `control-core/` and `control-esp32/`.

---

## [Unreleased] — 2026-06-13

### Fixed
- Increased the display inversion duration from 5 seconds to 60 seconds

---

## Unreleased

### Added
- **`format.sh`** — run clang-format over the C/C++ sources in `croaster/` and
  `boards/` (`./format.sh` in place, `--check` to verify, `--diff` to preview);
  new root `.clang-format` (Google base, 4-space indent, 120-col limit)
- **ESP32-S3 display commands** — `boards/esp32s3` now handles
  `{"command":{"darkMode":…}}` and `{"command":{"brightness":…}}` over
  WebSocket / BLE, driving `CroasterDisplayS3::setDarkMode()/setBrightness()`

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- **Reformatted all sources** to the new `.clang-format` (library + boards,
  including the LVGL UI)

## [0.62] — 2026-08-29 (Current)

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- **`build_all.sh` output naming** — firmware is now collected as
  `builds/Croaster_<board>_<version>.bin` (e.g. `Croaster_esp32s3_0.62.bin`);
  the version is read from `src/CroasterConstants.h` so the filename stays in
  sync with the firmware version
- **Firmware version bumped to `0.62`** — `croaster/src/CroasterConstants.h`,
  `croaster/library.json`, `boards/common/library.json`
- **Board id hardcoded per implementation** — `boardName()` no longer reads a
  `-DCROASTER_BOARD_NAME` build flag; each implementation calls
  `CroasterDeviceIdentity::setBoardName("esp32s3")` in its `main.cpp`
  (defaults to `unknown` until set)
- **Release/debug build split** — each board env is pinned to
  `build_type = release` (so `build_all.sh` always produces release firmware)
  and gains a `<board>-debug` variant (`extends`) used by the new
  `./upload_monitor.sh <board>` script to build + upload debug firmware to a connected
  board
- **Repo restructure** — the library moved to `croaster/` (`library.json`,
  `src/`, `test/`) and per-board projects to `boards/<board>/` (was
  `implementation/<board>/`); boards now consume the library via
  `lib_deps = ../../croaster`

---

## [0.52] — 2026-08-29

### Added
- **Repository is now a reusable library** (`library.json` at the repo root) — another project can consume it via `lib_deps = https://github.com/IiemB/Croaster.git`
- **`examples/` renamed to `implementation/<board>/`** — per-board implementations (`implementation/esp32c3/`, `implementation/esp8266/`), each consuming the library from `../..` exactly like an external project
- **`implementation/esp32s3/`** — ESP32-S3 (ES3C28P) 2.8" ILI9341 LCD board
  (LVGL 9 UI + FT6336G touch), ported from the `ES3C28P` branch into the
  per-board structure (`CroasterDisplayS3` wrapping the LVGL UI in `src/ui/`;
  `esp32s3-sdkconfig.defaults` for mbedTLS/NimBLE Kconfig fixes)
- **Roast timer** in the library core — `CroasterCore::roastTimerStart()/roastTimerPause()/roastTimerReset()` + `roastTimer` (seconds) & `roastTimerIsRunning()`; surfaced by the S3 UI's single START/STOP/RESET button
- **Board + display-state reporting** — `CroasterDeviceIdentity::boardName()` now returns a per-implementation id set via the `-DCROASTER_BOARD_NAME=` build flag (`esp32s3`/`esp32c3`/`esp8266`/`unknown`, no compile-time board guards); `getDeviceInfo` now includes `board`, `darkMode` and `brightness` (via new `CroasterDisplay::isDarkMode()/getBrightness()`); `CroasterApp::ble()` accessor
- **`build_all.sh`** — builds every board implementation and collects the
  firmware into the gitignored `builds/` folder; board-specific
  `CROASTER_ES3C28P` compile-time guards removed from the implementation
  sources (each board builds its own folder unconditionally); `AGENTS.md` and
  `CONTEXT.md` added for the firmware repo
- **Shared SSD1306 display in `implementation/common/`** — both the `esp32c3` and `esp8266` implementations reference the same display/ animation (no duplicated data); the ESP8266 (NodeMCU / ESP-12E) implementation was restored as `implementation/esp8266/`
- **`CroasterDisplay` abstract interface** — the library core is display-agnostic; consuming projects implement their own display (the SSD1306 implementation lives in the reference example as `CroasterDisplaySSD1306`). A `nullptr` display is fully supported (headless boards)
- **`CroasterPinConfig` struct** — pin layout is decoupled from the core and passed to `CroasterCore`; each implementation provides its own pins in `config.h`
- **`CroasterApp` single `begin()`/`loop()` entry point** — lives in the library (`src/CroasterApp.h/.cpp`); the sketch is reduced to `app.begin()` / `app.loop()`
- **`CroasterApp` is display-agnostic** — it takes `CroasterCore&` + `CroasterDisplay*` (+ LED pin/level); the display type is defined in the implementation (`main.cpp`)
- **Custom command registration** — `CroasterCommandHandler::onCommand()` / `onJsonCommand()` let an implementation add commands without touching the library
- **Compile-time BLE detection** via `CROASTER_HAS_BLE` (1 on ESP32, 0 elsewhere) in `CroasterConstants.h` — `CroasterBleManager` is only compiled when the board has BLE
- `SmoothThermocoupleFix.cpp` added to the library (fixes the MAX6675_Thermocouple v2.0.2 link error for all build types)

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- All classes renamed with the `Croaster` prefix: `BleManager` → `CroasterBleManager`, `CommandHandler` → `CroasterCommandHandler`, `WebSocketManager` → `CroasterWebSocketManager`, `OtaHandler` → `CroasterOtaHandler`, `WiFiManagerUtil` → `CroasterWiFiManager` (class with static methods), `DeviceIdentity` → `CroasterDeviceIdentity`, `DisplayManager` → `CroasterDisplay` (interface) / `CroasterDisplaySSD1306` (reference impl), `DisplayAnimation` → `CroasterDisplayAnimation`
- `Constants.h` → `CroasterConstants.h`, `PinConfig.h` → `CroasterPinConfig.h`
- `LED_ON`/`LED_OFF` macros removed from `CroasterConstants.h`; `CroasterCommandHandler` now takes `ledPin`/`ledOnLevel` per-instance (supplied by the implementation); removed the redundant `-D LED_ON` build flag
- `CroasterApp` moved from the implementation into the library core (`src/CroasterApp.h/.cpp`); registered in `library.json`
- **Arduino IDE support removed** — PlatformIO is the only workflow; deleted `copy_to_ino.sh`, `library.properties`, `setup_custom_partition_macos.sh` and `references.md`
- Removed `CroasterPinConfig::defaults()` — `CroasterCore` now requires an explicit `CroasterPinConfig` (no library pin defaults)
- **Board-specific docs moved to per-board READMEs** (`implementation/<board>/README.md`); the root `README.md`/`README_ID.md` are now board-agnostic (wiring, partitions, board names live in each implementation's README); removed `lib_extra_dirs` from the implementation `platformio.ini` files
- Pin and dummy-mode configuration moved to the implementation (`implementation/esp32c3/src/config.h`); the `dummyMode` global was removed from `CroasterConstants.h`
- Display constants (`SCREEN_WIDTH`/`SCREEN_HEIGHT`/`OLED_RESET`) moved to `CroasterDisplaySSD1306.h`; LED pin/polarity moved to the implementation's `config.h` (`ledPin`/`ledOnLevel`) — `CroasterConstants.h` no longer defines display/LED config
- `CroasterWiFiManager::restart()/erase()` replace the old `restartESP()/eraseESP()` helpers

---

## [0.51] — 2026-06-01

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Refactored OTA handling to return a JSON progress string directly while simplifying internal state management
- Updated `BleManager` and `WebSocketManager` to use the streamlined OTA progress flow
- Simplified command and core OTA integration paths for the new handler contract

---

## [0.50] — 2026-05-30

### Added
- **OTA over BLE** (ESP32 only): firmware updates can now be performed via BLE in addition to WebSocket/WiFi
- Timeout checks during BLE OTA to handle stalled transfers

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Improved OTA status reporting: `OtaHandler` now returns an `OtaResult` struct containing a JSON progress payload and an error flag
- Simplified `OtaHandler` internals — reduced code size while improving clarity
- `BleManager` updated to relay OTA binary data to `OtaHandler` and forward progress JSON back to the BLE client
- `WebSocketManager` updated to forward OTA progress JSON back to the WebSocket client consistently

---

## [0.46] — 2026-05-28

### Added
- `getExtra` command with corresponding data generation methods (`genRandomString`, random number/boolean)

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Refactored command handling and improved JSON response generation
- Simplified command handling by removing `restart` and `erase` parameters from `handleJsonCommand`

### Fixed
- Removed `.DS_Store` files and added to `.gitignore`

---

## [0.45] — 2026-02-28 to 2026-05-13

### Added
- `displayToggle` command to toggle OLED display on/off
- BLE MTU set to 517 for improved throughput
- `genRandomString` function for generating random strings
- Indonesian FAQ (`FAQ_ID.md`) and Indonesian README (`README_ID.md`) translations
- Issue and pull request templates for contribution guidelines

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Updated firmware progress display during OTA updates
- Updated library dependencies (`platformio.ini`)
- Set `dummyMode` to `false` (production default)

### Fixed
- Corrected spelling of `genRandomString`
- Updated JSON handling comments
- Updated WiFiManager library version and improved `platformio.ini` formatting

---

## [0.44] — 2025-05-15

### Added
- SSID name included in JSON data response

### Fixed
- Updated firmware version to `0.44`

---

## [0.42 – 0.43] — 2025-05-01 to 2025-05-06

### Added
- Custom partition configuration (`custom32c3sm.csv`) for ESP32C3 Super Mini to support OTA
- OTA update handling via `OtaHandler` class
- `resetState` method in `OtaHandler`; updated `begin` method to call it

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Refactored `OtaHandler`: replaced `debugf` with `debugln`, updated `finalize` method
- Refactored `DisplayManager` to handle OLED presence checks and simplify `begin` method
- Streamlined WiFi setup by removing redundant mode setting
- Standardized temperature variable naming across `CroasterCore` and `DisplayManager`
- Updated temperature handling and command processing for improved clarity

### Fixed
- Initialized `Wire` in `isOledPresent` to ensure proper I2C communication
- Updated `dummyMode` to `false` and set WiFi mode in setup

---

## [0.41] — 2025-05-01

### Added
- OTA update handling; firmware version bumped to `0.41`

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Removed OTA handling from `BleManager` (moved to dedicated `OtaHandler`)

---

## [0.40] — 2025-05-01

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Refactored firmware to version `0.40`
- Added partitions configuration and reference link

---

## [0.31] — 2025-04-25 to 2025-04-28

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Updated GPIO pin assignments in README
- Enhanced device identity functions for IP and SSID retrieval
- Improved JSON command handling in `CommandHandler`
- Enhanced sensor reading logic in `CroasterCore`
- Optimized display management in `DisplayManager`
- Updated blink logic and delay in `CommandHandler`
- Added `blinkIndicator` method in `DisplayManager`
- Adjusted `intervalSendData` handling for consistent timing

---

## [0.30 – Refactor Period] — 2025-04-22 to 2025-04-26

### Added
- `DeviceIdentity` class for chip ID, device name, IP address helpers
- Loop functions for `WebSocket` and `WiFiManager` for better code organization
- Screen rotation functionality in `DisplayManager`
- `testdrawline` function in `DisplayManager` for visual debugging
- IP address display toggle in `DisplayManager`
- `CommandHandler` class for centralized JSON command parsing and dispatching (BLE & WebSocket)
- ESP32 and ESP8266 conditional compilation (`#if defined(ESP32)` / `#if defined(ESP8266)`)
- Detailed class and method documentation (Doxygen-style headers)

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Integrated `DisplayManager` into command handling and BLE setup
- Refactored BLE setup to use device name and `CommandHandler`
- Simplified BLE and WebSocket handling by integrating `CommandHandler`
- Updated `getDeviceName` and `getShortChipId` functions for improved prefix/suffix handling
- Optimized setup sequence — `Serial` initialization before manager setup
- Modularized command handling and improved temperature reading logic
- Updated JSON command handling to use proper JSON serialization and logging
- Changed `croasterInterval` type to `unsigned long` in `DisplayManager`
- Reordered constructor initialization in `WebSocketManager`
- Removed inheritance from `BLEServerCallbacks` and `BLECharacteristicCallbacks` in `BleManager`

### Fixed
- Changed `croasterInterval` type to `unsigned long` for consistency in `broadcastData`
- Updated git clone URL to use SSH format in README files

---

## [2.48] — 2025-02-24

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Updated pin definitions
- Refactored display output and WiFi handling
- Renamed target directory and file for clarity
- Removed unused library

---

## [2.47] — 2025-02-24

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Updated pin definitions and refactored code

---

## [2.46] — 2025-02-21

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Minor bump; version consistency update

---

## [2.45] — 2025-02-21

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Added Croaster Arduino sketch (`.ino`) and updated conversion script
- Updated `copy_to_ino.sh` to generate the Arduino sketch; implemented BLE server functionality
- Added conversion script for `main.cpp` and `Croaster.h`; refactored BLE characteristic handling

---

## [Early Refactor — ESP32 Core] — 2025-02-20 to 2025-02-24

### Added
- `Croaster` class with sensor integration, temperature conversion, and JSON data handling
- LED blinking functionality based on WiFi and BLE connection status
- BLE server functionality (`BleManager`)

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Updated serial communication speeds and flash size in `platformio.ini`
- Refactored BLE data handling to use `String` type
- Updated Croaster class to use new MAX6675 library
- Adjusted pin definitions for new hardware
- Enhanced dummy data handling

---

## [Initial Modular Refactor] — 2025-04-19 to 2025-04-21

### Added
- File conversion script (`copy_to_ino.sh`) using Bash for Arduino sketch generation
- Dedicated `src/` directory structure for modular C++ source files

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Replaced `TempsManager` with `CroasterCore` across the codebase
- Modularized codebase: `BleManager`, `WiFiManagerUtil`, `WebSocketManager`, `DisplayManager`, `CroasterCore`
- Updated `DisplayManager` methods for improved data handling and display updates
- Replaced Dart file copying logic with a Bash script

---

## [Pre-Modular] — 2021-02-07 to 2025-02-19

### Added
- Initial project commit with ESP32-based BLE coffee roaster monitor (2021-02-07)
- Arduino `.ino` file support
- PlatformIO configuration (`platformio.ini`) for ESP8266 and ESP32
- Basic Croaster class with sensor integration and JSON data output
- README and documentation (English)
- Artisan Roaster Scope connection instructions

### Changed
- **Croaster's OTA only**: ArduinoOTA (yaeger carry-over), its `-ota` env and
  `FLEX_ENABLE_ARDUINO_OTA`/`FLEX_OTA_PASSWORD` removed. The `.aset` host is
  Croaster's AP address `192.168.4.1` (no mDNS without ArduinoOTA).
- Flex uses Arduino's `default_16MB.csv` (the local `partitions.csv` was an
  identical copy); `MAX6675_Thermocouple` is no longer listed in the flex
  `lib_deps` (Croaster's `library.json` already pulls it).
- Updated project from Arduino-only to PlatformIO build system
- Updated to production dummy data settings
- Progressively enhanced display output, WiFi handling, and BLE connectivity
- Updated README multiple times for wiring diagrams, hardware components, and connection guides

---

*For full commit history, run:*
```bash
git log --oneline
```
