# Design — flex control board on Croaster (ESP32-S3)

Status: skeleton, builds and host-tests pass; not yet run on hardware.
Owner decisions still open are marked **DECISION**.
Board renamed `esp32s3-roaster` → `esp32s3-flex` on 2026-10-26 (D5).

## Goal
Shawn's roaster (SSR heater, AC-dimmer fan, 2 × MAX31855, I2C OLED, Lonely
Binary ESP32-S3 N16R8) gets yaeger's control features (PID, autotune, profiles,
Artisan control, safety) on top of Croaster's platform (WiFiManager, WebSocket,
BLE/ICRM app, OTA, RoR, display interface, per-board projects), keeping
Croaster's library/board split and extension model.

## Croaster's model (what we kept)
- `croaster/` is a display- and pin-agnostic library. Boards inject everything:
  pins (`CroasterPinConfig`), a `CroasterDisplay` subclass, LED pin, board id
  (`CroasterDeviceIdentity::setBoardName`).
- `CroasterApp` owns WiFi, WebSocket :81, BLE, OTA, and the command handler.
  Boards extend the protocol with `commands().onCommand(name, fn)` and
  `commands().onJsonCommand(key, fn)`.
- `boards/<board>/` are standalone PlatformIO projects; `boards/common/` is
  shared board-side code. `build_all.sh` builds env `<board>`;
  `upload_monitor.sh` builds env `<board>-debug`.

## Names

The whole project is a roaster, so "roaster" in a name says nothing. Rule:
board-level things say **flex**, control features say **control**, and no
class or macro starts with `Croaster` (that prefix is the upstream author's).
The one exception is the control library folder, `croaster-control/` (lib
`CroasterControl`), named by Shawn on 2026-10-27 (D1). Renamed 2026-10-26 at
Shawn's request, then folded into one library on 2026-10-27:

| Before | After | What it is |
|:---|:---|:---|
| `roaster-core/` (lib `RoasterCore`), `src/core/` | `control-core/` (lib `ControlCore`), `src/control/`; since 2026-10-27 `croaster-control/src/core/` | Pure, host-tested logic: PID, autotune, profiles, decoders, protocol |
| `roaster-control/` (lib `RoasterControl`) | `control-esp32/` (lib `ControlEsp32`); since 2026-10-27 `croaster-control/src/hw/` and `src/app/` | ESP32 side: control task, SSR, fan drivers, probes, NVS, plugin |
| `roaster_protocol.*`, namespace `roasterproto` | `control_protocol.*`, `controlproto` | JSON commands and data fields |
| `roaster_types.h` | `control_types.h` | `Command`, `Snapshot`, `Mode`, `SafetyTrip` |
| `Roaster` (`app/roaster.*`) | `Controller` (`app/controller.*`) | The control task |
| `RoasterPlugin`, `RoasterPluginConfig` | `ControlPlugin`, `ControlPluginConfig` | Registers commands with `CroasterApp` |
| `RoasterSettings` | `ControlSettings` | NVS-backed settings |
| `RoasterLimits` | `SafetyLimits` | Over-temp, max setpoint, min fan, mains check |
| `RoasterOledDisplay` | removed | The flex board uses Croaster's `CroasterDisplaySSD1306` |
| `ROASTER_*` macros in `config.h` | `FLEX_*` | Board config |
| `{"command":"getRoaster"}`, `writeRoasterInfo` | `getControl`, `writeControlInfo` | Status query |
| FreeRTOS task `"roaster"` | `"control"` | |
| NVS namespace `"roaster"` | `"control"` | Nothing is deployed, so no settings are lost |
| `RoasterProbe` | kept | See below |

`RoasterProbe` (`croaster-control/src/app/RoasterProbe.h`) is a `Thermocouple`
adapter for the two K-type probes, ET and BT, and nothing else. It does not
touch the chips. The control task reads both MAX31855s (`hw/thermocouples`)
and publishes filtered values; each `RoasterProbe` returns one channel of that
snapshot (NaN on a fault) so `CroasterCore` computes RoR and telemetry from
the same numbers the PID uses. Shawn keeps the name for the K-type probes.

Still named "roaster": Artisan's `roasterID` field, the branch
`rework/roaster`, the `roaster-*.zip` file names, and prose about the
physical machine.

## Layers

```
boards/esp32s3-flex/          board: config.h, ConfigCheck.h, main.cpp
        |  constructs + injects
        v
croaster-control/src/app/  (Arduino/ESP32)  ControlPlugin  --registers-->  CroasterApp commands / onJsonData
                                    Controller (FreeRTOS task) <-- Command queue
                                    ConfigStore (NVS settings)
                                    RoasterProbe : Thermocouple  --> CroasterCore (BT/ET)
        |
        v
croaster-control/src/hw/   (ESP32 drivers)  Thermocouples (SPI), HeaterOutput (SSR),
                                    FanDimmer | FanPwm behind FanOutput
        |
        v
croaster-control/src/core/ (pure C++, host-tested)  PID, relay autotune, roast profile,
                                    MAX31855/MAX6675 decode, probe filter, safety rules,
                                    time-proportional window, command protocol
croaster/         (Croaster library) WiFi, WebSocket, BLE, OTA, RoR, display iface
```

Rules this keeps: nothing in `croaster/` knows about heaters or fans; board
config stays in the board; control logic is reusable by any future board
(e.g. a C3 control board) by adding another `boards/<board>/` folder.

### Where each feature lands

| Feature | Lands in | Why |
|:---|:---|:---|
| MAX31855 or MAX6675 in the flex monitor build | `croaster-control/src/app/MonitorProbes.h` over `hw/thermocouples` | One reader and one decoder per chip for both flex builds; `croaster/` keeps upstream's MAX6675 path (D11). |
| MAX31855/MAX6675 decode for control | `croaster-control/src/core/max31855.h`, `max6675.h` | Pure bit decoding, host-tested; control task reads the bus itself. |
| Per-probe filter and fault count | `croaster-control/src/core/probe_channel.h` | Pure logic; both channels tested on the host. |
| PID, autotune, profiles, safety rules, command protocol | `croaster-control/src/core` | Pure logic, unit-tested on the host. |
| SSR, dimmer, PWM fan, probe reading | `croaster-control/src/hw` | ESP32 hardware, shared by any ESP32 control board. |
| Control loop + safety, NVS settings | `croaster-control/src/app/controller`, `app/config_store` | Own FreeRTOS task (see Threading). |
| Commands, data fields, watchdog | `croaster-control/src/app/ControlPlugin` | Uses only Croaster's public extension points. |
| Pins, chip/driver/display choice, limits | `boards/esp32s3-flex/src/config.h` | Board config never in a library (AGENTS.md). |
| OLED screen | `boards/common` `CroasterDisplaySSD1306` + `setHeaderText()` | Croaster's shared screen; the board supplies the header text (heater/fan/mode or trip). |

### Every file we added, against Croaster (review point 9, 2026-10-26)

Relation: **new** (Croaster has nothing like it), **extends** (adds to a
Croaster part through its own extension point), **duplicates** (a second copy
of something Croaster or our code already has).

Board folder `boards/esp32s3-flex/`:

| File | Relation | Why it is here |
|:---|:---|:---|
| `src/main.cpp` | extends | Same shape as Croaster's board `main.cpp`: builds `CroasterApp`, adds the control plugin, the probe adapters and the screen header. Has two builds: control (`FLEX_HEATER_*`) and monitor (`FLEX_HEATER_NONE`: no control task; Croaster reads the probes through `app/MonitorProbes`). |
| `src/config.h` | new | The one master file for pins, chips, drivers and limits (D2). Croaster boards set pins in `main.cpp`. |
| `src/ConfigCheck.h` | new | Compile-time checks on `config.h` (reserved pins, duplicates, ranges). |
| `platformio.ini` | extends | Croaster's board ini pattern (`<board>`, `<board>-debug`), plus one pioarduino env for Kconfig. |
| `artisan/croaster-flex.aset` | new | Artisan settings for heater, fan, PID and the data request. |
| `test/test_core/test_main.cpp` | new | Host tests for `croaster-control/src/core`. |
| `README.md`, `.gitignore` | new | Wiring, settings and generated-file ignores. |

Library `croaster-control/` (paths under `src/`; until 2026-10-27 these were
two libraries, `control-core/` and `control-esp32/`):

| File | Relation | Why it is here |
|:---|:---|:---|
| `core/max31855.h` | new | The only MAX31855 decoder (D11). Croaster has none; one 32-bit frame gives temperature, cold junction and fault bits. |
| `core/max6675.h` | duplicates | Same decode as the `MAX6675_Thermocouple` library, which reads the bus itself and has no fault bit API. Used only if a control board picks MAX6675. |
| `core/moving_average.h`, `probe_channel.h` | new | Per-probe filter and fault count for the control loop. Croaster's `SmoothThermocouple` wraps a sensor that reads the bus, and has no fault count, so it can't sit on the control task's readings. |
| `core/pid.*`, `relay_autotune.*`, `roast_profile.*`, `time_proportional.h` | new | Control features Croaster doesn't have. |
| `core/zero_cross.h`, `mains_watch.h` | new | Mains detection for the AC dimmer: crossing age, and the grace period before a `nomains` trip. |
| `core/control_protocol.*`, `control_types.h` | extends | Parses the payloads Croaster's handler hands over (nested commands and yaeger's named commands); builds the extra data fields. |
| `app/ControlPlugin.*` | extends | Registers commands and data fields through `CroasterApp`'s public hooks; runs the comms watchdog off core's `lastCommandMillis()`. |
| `app/RoasterProbe.h` | extends | A Croaster `Thermocouple` that reads the control task's snapshot, so `CroasterCore` never touches the bus the control task owns. |
| `app/MonitorProbes.h` | extends | Monitor build only: two Croaster `Thermocouple`s over `hw/thermocouples`, read on the loop task (no control task). Same chip choice, decoders and fault filter as the control build (D11). |
| `app/controller.*` | new | Control task: sampling, safety, modes, outputs. |
| `app/config_store.*` | new | NVS for PID gains, PID input, cooldown fan (stop and trip) and watchdog. Croaster core stores nothing. |
| `hw/heater_output.*`, `fan_dimmer.*`, `fan_pwm.*`, `fan_output.h`, `thermocouples.*` | new | SSR, triac dimmer, PWM fan and SPI probe reading. |

Clear wins done this round: the board's own SSD1306 class (now Croaster's
display plus a hook, D12), `partitions.csv` (identical to `default_16MB.csv`),
ArduinoOTA (D13), and the probe filter moved out of `hw/` into a pure,
tested `ProbeChannel`. Since then (2026-10-27): one MAX31855 decoder (D11)
and one sdkconfig file (D10).

## Data flow
1. Control task (core 1, every 10 ms tick): reads both converters on its own
   SPI bus every sample period, filters (moving average of 4, reset after 3
   faults), runs safety checks, mode logic (Manual / PID / Profile / Autotune),
   PID, and the SSR time-proportional window; writes the fan; publishes a
   `Snapshot` under a spinlock.
2. Croaster loop (Arduino loop task, core 1, lower priority): `CroasterCore`
   reads BT/ET through `RoasterProbe` (snapshot, NaN on fault), computes RoR,
   and broadcasts JSON; `onJsonData` adds `BurnerVal`, `FanVal`, `SV`, `Mode`,
   `trip`.
3. Clients (Artisan, ICRM, BLE) send JSON → `CroasterCommandHandler` →
   `ControlPlugin` callbacks → `controlproto` parse → `Command` queue → control
   task. Gains / PID input / cooldown fan are persisted to NVS by the plugin.

## Threading
- WiFi, LwIP and BLE (NimBLE) run on core 0 (IDF defaults).
- Croaster's board json pins Arduino loop + events to core 1; the control task
  also runs on core 1 at `configMAX_PRIORITIES - 3`, so it preempts the loop
  (WebSocket handling, display, JSON) and never waits for it.
- Only two shared objects: a FreeRTOS queue (commands in) and a
  spinlock-protected `Snapshot` (state out). The SPI bus belongs to the control
  task alone; that is why `CroasterCore` got a constructor taking sensors.
- Dimmer: zero-cross ISR + one-shot GPTimer fires the triac gate. The
  handlers are `IRAM_ATTR`, but neither interrupt is registered as IRAM
  (`CONFIG_ARDUINO_ISR_IRAM` and `CONFIG_GPTIMER_ISR_CACHE_SAFE` are off), so
  both wait out flash writes. That is safe, because they call flash-resident
  gptimer functions. The fan may stutter for a few half-cycles during an NVS
  or OTA write.

## Changes to Croaster's core (and why)

| Change | Needed? | Reason |
|:---|:---|:---|
| `CroasterCore(bool, Thermocouple* bt, Thermocouple* et)` | needed | Two owners of one SPI bus would race; this lets the control task own it. Additive, existing constructor unchanged. |
| `CroasterCore::onJsonData(fn)` | needed | Artisan must see heater/fan/setpoint in the same `getArtisanData` payload; no existing hook. Additive. |
| `CroasterDisplaySSD1306::setHeaderText(fn)` (boards/common) | small | Lets a board show its own state in the header line instead of a second display class. Additive; default header unchanged. |
| `#ifndef CROASTER_HAS_BLE` | small | Lets a board turn BLE off (~310 KB less flash in our build). Default unchanged. |
| `CroasterCommandHandler::lastCommandMillis()` | needed for the comms watchdog | Lets an add-on see client traffic on Croaster's own WebSocket/BLE path, built-in commands included. Additive getter; `handle()` stores `millis()` when it accepts a message. See D6. |
| `CroasterBleManager` initializer order, `CroasterDeviceIdentity` `PRIX32` + `snprintf` | warning fixes | `-Wreorder` and `-Wformat` on ESP32 builds. No behaviour change; the chip id string is identical. |
| `CroasterCore::correctionBtCelsius()`/`correctionEtCelsius()` | needed for correct PID | The control task reads the probes itself, so it needs the user's correction offsets to work on the displayed scale. Two const getters; stock boards never call them. See D22. |
| `changeTemperatureUnit` rescales `correctionBt`/`correctionEt` | bug fix | The offsets are differences in the display unit; a unit change kept the number, so the applied offset grew or shrank by 1.8×. See D22. |
| ~~Removed negative-RoR clamp~~ | **reverted 2026-10-27** (Shawn) | Upstream's `deltaET > 0` / `deltaBT > 0` test is back, so a falling temperature reads RoR 0 as in upstream. Artisan computes its own RoR, no consumer needs negative values, and the display keeps upstream behavior. `updateROR` matches `5445a4d`. |

Existing boards still build with every change above (2026-10-26, temporary
copies pinned to `espressif32 @ 8.0.0`; esp8266 on its own platform). Release
sizes: `esp32c3` RAM 44 728 B, flash 1 742 297 B (91.7 %); `esp8266` RAM
36 484 B, flash 415 739 B. The `esp32s3` result from that round is void: it
was built from the board as edited during Shawn's testing before this project
(see "Stock boards and upstream" below).

### Stock boards and upstream

Upstream is `IiemB/Croaster` `master` at `5445a4d`, the commit this fork
branched from. Shawn's fork added three commits on top before this project
started (`da20956`, `77ad627`, `0845ba8`, author "Your Name"). They changed
`.gitignore`, the root and board `.vscode/` files, `boards/esp32s3`
(`platformio.ini` comments stripped, custom board json, `-W
no-deprecated-declarations`, `ARDUINO_USB_MODE=1`, `dummyMode = true`,
`ledPin = -48`, the backlight on the Arduino-ESP32 3.x LEDC API, `lv_conf.h`),
and added `build.log`, `croaster/src/Croaster.code-workspace`,
`To-Implement.md` and `boards/esp32-s3-devkitm1-n16r8.json`.

On 2026-10-26 the stock board files went back to upstream byte for byte. Two
exceptions: the stock inis keep D21's removal of the registry
`MAX6675_Thermocouple` (listing it next to the core's fork fails to link), and
`boards/esp32-s3-devkitm1-n16r8.json` stays because flex uses it.
`build.log` and `Croaster.code-workspace` are deleted. `To-Implement.md` is
Shawn's design note and stays.

Upstream's `boards/esp32s3` needs Arduino-ESP32 2.x: `LvglUi.cpp` calls
`ledcSetup`/`ledcAttachPin`, which 3.x removed. Its unpinned `platform =
espressif32` resolves to 8.0.0 (Arduino 3.3.12) today, and the build stops
there, on upstream `5445a4d` and on this branch alike. Pinned to
`espressif32 @ 6.13.0` (Arduino 2.0.17), upstream builds. T13 tracks the fix.

## Decisions where Croaster's rules and Shawn's goals rub

Each lists alternatives; the recommended one is implemented (all reversible)
unless the heading says otherwise.

Changes from Shawn's nine review points (2026-10-26):

| Decision | Status | What moved |
|:---|:---|:---|
| D1, D3 | names only | Folder and macro names (see "Names"); the choice is unchanged. |
| D6 | changed | Watchdog reads core `lastCommandMillis()`; the `getData` alias is gone. |
| D8 | changed | Rewritten: why flat keys need the hook; dropping them is now an option. |
| D9 | changed | Watchdog timeout and an off switch are settings (config.h, command, NVS). |
| D10 | facts updated | Partition table and display libs now match the shared defaults. |
| D11 | new, open | Two MAX31855 implementations. |
| D12 | new | Croaster's SSD1306 display with a header hook; U8g2 and SH1106 dropped. |
| D13 | new | Croaster's OTA only; ArduinoOTA and its mDNS name dropped. |
| D14 | new, design only | Config wizard. |
| D15 | new | `MAX6675_Thermocouple` stays a core dependency. |
| D16 | new | Temperature unit: Croaster's `tempUnit`, °F at boot on flex; control stays in °C. |
| D9 | note added | How a comms trip clears, and why reconnecting doesn't clear it. |
| D17 | new | Visible autotune status: `autotune` in telemetry and on the OLED. |
| D18 | new, open | PID input (ET or BT) and default gains. |
| D19 | new, open | Cooldown fan default (100 % or yaeger's 65 %). |
| D20 | new, open | Roast rules and profiles. |
| D21 | new, resolved | `MAX6675_Thermocouple` comes from Shawn's fork at `v2.0.3`. |
| D23 | new, open | Autotune and control review: three fixes made, twelve proposals. |

Resolved by Shawn on 2026-10-26:

| Decision | Choice |
|:---|:---|
| D8 | Drop flat keys and the `onOtherJson` hook (option 2). |
| D17 | Autotune status in `getArtisanData` and on the OLED header (option 3). |
| D18 | PID input defaults to BT (option 2); `pidInput` command and NVS kept; yaeger's gains kept until autotuned on BT. |
| D19 | Cooldown fan 65 % after `stop`, 100 % after any trip (option 3). |
| D21 | Fork `MAX6675_Thermocouple` and fix it there (option 3). |
| D22 | Pass Croaster's correction offsets to the controller through core getters (option 1). |

Resolved by Shawn on 2026-10-27:

| Decision | Choice |
|:---|:---|
| D1 | One library `croaster-control/` with `src/core`, `src/hw`, `src/app`; `control-core/` and `control-esp32/` are gone. |
| D10 (sdkconfig part) | "Kill ours, keep his": flex's `esp32s3-sdkconfig.defaults` is deleted; flex sets `board_build.sdkconfig_path = ../esp32s3/esp32s3-sdkconfig.defaults`. The ini question stays open. |
| D23 (mains) | `nomains` trips after 2 s of missing mains while heating (`FLEX_MAINS_GRACE_MS`). |
| D24 | Autotune required before PID and profiles (`FLEX_REQUIRE_AUTOTUNE 1`); gains set with the `pid` command count as tuned. |
| D23 item 4 | Fan and heater commands during autotune abort it, heater off (`aborted: fan changed` / `aborted: heater changed`). |
| D11 | One MAX31855 decoder, `croaster-control/src/core/max31855.h`. `CroasterMAX31855`, `CroasterThermocoupleType` and the type argument are removed; `croaster/` builds its thermocouples exactly as upstream. The flex monitor build reads the chips through `app/MonitorProbes`. |

### D1. Where the control libraries live — resolved 2026-10-27: one library, `croaster-control/`
Shawn chose one top-level library next to `croaster/`, split by purpose into
`src/core` (pure, host-tested), `src/hw` (ESP32 drivers) and `src/app`
(control task, NVS settings, Croaster integration). `croaster/` stays the
monitor library; board-only files stay in `boards/esp32s3-flex/`.

Layout choice: the folders sit under `src/`, PlatformIO's default library
source dir. PlatformIO then compiles all three and puts `src/` on the include
path, so code includes `core/pid.h`, `hw/thermocouples.h`, `app/controller.h`
with no `srcDir`/`includeDir` settings. Folders at the library root would need
`build.srcDir` and an include path pointing at the root, where `library.json`
and `build_filter.py` also sit.

Host tests: `croaster-control/build_filter.py` (`library.json`
`build.extraScript`) sets the library's source filter to `+<core/>` when the
platform is `native`. PlatformIO provides this hook: its library builder
clears `SRC_FILTER` and then runs the extra script so the script can set it.
The native env compiles only `src/core`; `app/` and `hw/` never reach the host
compiler.

Placement rules: a file goes in `core/` when it compiles without Arduino or
ESP-IDF headers, whatever it is about (the command protocol parser is pure,
so it is in `core/` and host-tested; `app/ControlPlugin` registers it with
Croaster). `hw/` holds drivers that touch pins, SPI or timers. `app/` holds
what ties the parts together on the device: the FreeRTOS controller, NVS
settings (`Preferences`, used only by the plugin and `main.cpp`), the plugin
and the probe bridge into `CroasterCore`.

Earlier options, kept for the record (AGENTS.md then listed only `croaster/`,
`boards/`, `builds/`):
1. Top-level `control-core/` + `control-esp32/` libs (implemented until 2026-10-27).
   Clear split: pure logic vs hardware; consumed like `boards/common` via
   `symlink://`. Bends the layout list (two new top-level folders).
2. Put them in `boards/common-control/` (one lib). Matches the "shared board
   code under boards/" precedent, but `build_all.sh` must skip it (it only skips
   `common`), and the pure/hardware split is lost.
3. Fold into `croaster/src/roaster/`. One library, but every board (ESP8266
   too) compiles heater/fan code and it breaks "monitor library, no board
   specifics" intent.
4. Keep everything inside `boards/esp32s3-flex/src/`. Zero layout change,
   but nothing is reusable for a second control board and tests live with one board.

### D2. One master config vs `config.h` + `pins.h` — resolved
Croaster boards use `config.h` (pins/dummy/LED) and sometimes `pins.h`.
Shawn asked for a single file. Implemented: `src/config.h` holds everything
(name keeps the convention), `src/ConfigCheck.h` validates. Options considered:
split pins.h (convention, two files to edit), runtime JSON config (no compile
checks), Kconfig menus (pioarduino only).

### D3. Library feature switches from board config — **DECISION**
`CROASTER_HAS_BLE` must be seen when `croaster/` compiles.
1. **Force-include `config.h` into every unit (`-include`, implemented).** One
   file really is the master; cost: every library sees the `FLEX_*` macros
   (prefixed, so collisions are unlikely).
2. `-D CROASTER_HAS_BLE=0` in `platformio.ini`. Croaster style, but the master
   config is then split across two files.
3. Runtime switch (`CroasterApp` option to skip BLE). BLE code still linked
   (no flash saving), library API change.
4. Always build BLE. Simplest; ~310 KB flash plus NimBLE heap at runtime (fine on N16R8).
Recommendation: 1.

### D4. Compile-time feature selection in `main.cpp`
AGENTS.md: "no board-specific compile-time guards" (meaning: the library never
`#ifdef`s on a board). Our `#if FLEX_FAN == ...` are feature selections inside
one board, which the rule does not forbid, but they are guards. Alternatives:
separate board folders per variant (monitor / control; duplicates main.cpp),
runtime selection (all drivers linked, invalid configs only caught at boot),
or templates (more C++ than the codebase uses). Kept `#if` + `ConfigCheck.h`
because Shawn asked for compile-time selection with static checks.

### D5. Board id — resolved: `esp32s3-flex`, chosen by Shawn 2026-10-26
Ids must be lowercase and match ICRM's `CroasterBoardTypes`
(`esp32s3`/`esp32c3`/`esp8266`/`unknown`). Folder `boards/esp32s3-flex/`,
envs `esp32s3-flex[-debug|-pioarduino]`, board id
`"esp32s3-flex"`. "roaster" was dropped
because the whole project is a roaster, so it told you nothing. Options that
were considered:
1. **`"esp32s3-flex"` (chosen).** Distinct, so the app never offers the
   ES3C28P LCD image as an update. ICRM may show it as unknown until its enum
   is extended.
2. `"esp32s3"`. Matches the enum, but an ICRM OTA would flash the LCD firmware
   over this firmware (heater control lost).
3. `"unknown"`. Safe, but the app learns nothing.
4. Add `esp32s3Flex` to ICRM's enum (needs the app author). Still worth doing later.

### D6. How the watchdog sees client traffic — **DECISION** (changed 2026-10-26)
The heater must stop when the client disappears, so the watchdog needs to see
every message a client sends. Croaster answers `getArtisanData` inside its
command handler, where a board can't see it. Previously option 3 was
implemented; Shawn asked for Croaster's own WebSocket/BLE path instead.
1. **`CroasterCommandHandler::lastCommandMillis()` (implemented).** `handle()`
   records when it last accepted a message. That covers WebSocket and BLE and
   every command, `getArtisanData` included. The plugin reads it each loop.
   Additive, about 10 lines, and any add-on can use it. The `.aset` polls
   Croaster's own `getArtisanData` again.
2. An `onAnyCommand(fn)` callback hook. Same coverage, but a callback per
   message where a timestamp is enough.
3. A `getData` alias in the plugin (the previous version). No core change, but
   only clients that poll `getData` count; the ICRM app's traffic doesn't.
4. WebSocket client count (`connected()`). Catches drops, not a hung Artisan.
Recommendation: 1.

### D7. Control in a FreeRTOS task, not `loop()`
Croaster is loop-driven. A blocking WiFiManager portal, WebSocket burst or BLE
OTA can stall `loop()` for seconds; an SSR left on for seconds is the failure to
avoid. Options: loop-only (simplest, unsafe stalls), hardware-timer ISR (no SPI
reads in ISR), dedicated task (implemented), task on core 0 (shares core with
WiFi). Task on core 1 at high priority chosen.

### D8. yaeger's flat keys need a core hook — resolved 2026-10-26: dropped (option 2)
Shawn chose option 2: nothing we ship sends flat keys, and dropping them
removes a core change. `onOtherJson` and the flat-key half of `parseLegacy`
are gone; `CroasterCommandHandler` matches upstream apart from
`lastCommandMillis()` (D6). The analysis below is kept for the record.

yaeger's clients send two kinds of message. Named commands
(`setBurner`, `setFan`, `setPreferences`, `getPreferences`) carry a string
`"command"`, so they use Croaster's `onCommand` with no core change (done,
`kLegacyBasicNames`). Flat keys (`{"BurnerVal":60}`, `{"FanVal":80}`,
`{"Setpoint":200}`, `{"Mode":"PID"}`, `{"Target":"BT"}`) carry no `"command"`
member at all.

Why flat keys can't use Croaster's mechanism: `CroasterCommandHandler::handle()`
dispatches on the `"command"` member only. A string goes to the `onCommand`
table, keyed by that string; an object goes to the `onJsonCommand` table, keyed
by the members *inside* `"command"`. A message without `"command"` matches
neither branch, and upstream `handle()` returns false before any registered
callback runs. WebSocket and BLE pass the raw text straight to `handle()`, so
no other layer sees it either. No registration call can reach `BurnerVal`.

Who sends flat keys today: nothing of ours. The `.aset` sends nested commands
(`{"command":{"burner":60}}`). Only yaeger's own web UI and older yaeger
`.aset` files do.
1. `onOtherJson(fn)` fallback in `CroasterCommandHandler` (was implemented).
   About 5 lines, additive, runs only for JSON objects without `"command"`,
   over WebSocket and BLE. Parsing stays in `controlproto::parseLegacy`.
2. **Drop flat-key support (implemented).** Remove the hook, the registration and the flat-key
   half of `parseLegacy`; keep the named yaeger commands. No core change for
   this feature. yaeger's web UI and old settings files stop working; ours
   don't notice.
3. Pre-parse in `CroasterWebSocketManager` before the handler. WebSocket only
   (BLE misses out), and it touches transport code.
4. Translate in the plugin by replacing the handler. No upstream change, but it
   duplicates Croaster's routing (two ways to do one thing).
Recommendation: 1 while yaeger-parity is a goal (T15). If Shawn doesn't need
yaeger's web UI or old `.aset` files, take 2: it removes a core change.
Reversible either way.

### D9. What happens when the client goes away — **DECISION** (changed 2026-10-26)
yaeger: on a clean WebSocket disconnect, in Manual with heater > 0, heater 0
and fan 100. No protection in PID, and none for silent WiFi loss.
1. **Traffic watchdog (implemented):** no client traffic for 10 s while
   heating (Manual, PID or Autotune) → heater off, fan to cooldown,
   `trip:"comms"`. Profiles are exempt (they are meant to run unattended).
   Catches clean disconnects and silent drops; worst case 10 s.
2. Core `onClientDisconnected` hook for an instant cut on a clean disconnect,
   plus option 1 for silent loss. Fastest, but `CroasterApp` doesn't expose
   `wsManager`, so it needs a new core API.
3. yaeger's exact rule (disconnect + Manual only). Matches yaeger, but PID
   keeps heating with nobody watching, and silent drops are missed.
4. No cut-off: the firmware PID keeps going and only the over-temperature trip
   protects. That is how a standalone PID controller behaves, but it is unsafe
   for Manual.
Recommendation: 1; add 2 later if a faster cut on clean disconnects is
wanted.

Settings (changed 2026-10-26, Shawn's point 4): `FLEX_WATCHDOG_ENABLED` and
`FLEX_WATCHDOG_TIMEOUT_S` in `config.h` set the first-boot defaults.
`{"command":{"watchdog":{"enabled":false}}}` turns the cutoff off entirely and
`{"command":{"watchdog":{"timeout":20}}}` changes the timeout (2 to 600 s).
Both are saved in NVS and reported by `getControl`. Usage: flex README,
"Comms watchdog".

Clearing (note added 2026-10-26 after a bench test). A comms trip latches
like every other trip. `clearTrip`, or any heater, mode, autotune or profile
command, clears it; fan and setpoint commands don't. Reconnecting doesn't clear
it on purpose: the trip says the roast went unattended, and the operator should
see that before heat comes back. Artisan sends nothing to the firmware when it
switches ON, so after an OFF/ON cycle the firmware still shows the cooldown
state until a command arrives. A fan command applies even while tripped and a
heater command clears the trip, so if neither changes, the firmware never
received them (no `[CMD-SOCKET]` line in its log). yaeger has no
latch: on a clean disconnect in Manual it sets heater 0 and fan 100, and the
next heater command simply applies.

### D10. How the flex board relates to Croaster's esp32s3 board config — **DECISION (open; sdkconfig part resolved 2026-10-27)**

Resolved part (Shawn, 2026-10-27: "kill ours, keep his"): flex's
`esp32s3-sdkconfig.defaults` is deleted, and every flex env sets
`board_build.sdkconfig_path = ../esp32s3/esp32s3-sdkconfig.defaults`. The ini
question below is still open.

Shawn asked (2026-10-26) whether a "flex" board's `platformio.ini` could `extends = core`,
or otherwise reuse what `boards/esp32s3/platformio.ini` already says. This is a discussion
item. Nothing in the build has changed; flex is still option A.

**First, what "core" is.** Croaster core (`croaster/`) is a **library**, not an ini section.
It has a `library.json`, and each board pulls it in with
`lib_deps = Croaster=symlink://../../croaster`. PlatformIO's `extends` only works between
ini sections (`[env:x]` or a plain `[section]`), so `extends = core` isn't something
PlatformIO can do. The flex board already gets every line of core code through that same
symlink. The only things two boards could share at the ini level are build *settings*:
board json, platform, framework, flash size, upload and monitor options, and common flags.

**What flex and his esp32s3 actually have in common:** the ESP32-S3 chip, 16 MB flash,
`ARDUINO_USB_CDC_ON_BOOT=1`, `BOARD_HAS_PSRAM`, the upload and monitor speeds, the partition
table (`default_16MB.csv`) and the sdkconfig file (since 2026-10-27 flex points at
`../esp32s3/esp32s3-sdkconfig.defaults`, his copy; flex's own copy is deleted).
**What differs:** the board json (his is PlatformIO's stock `esp32-s3-devkitc-1` for the
ES3C28P; ours is `esp32-s3-devkitm1-n16r8.json` for the Lonely Binary N16R8), lib_deps (his
has ILI9341 + LVGL; ours has Adafruit SSD1306 + CroasterControl), build_flags (his
`LV_CONF_PATH` points into his `src/ui`; ours does `-include src/config.h` and adds
`ARDUINO_USB_MODE=1`), PSRAM mode (`dio_opi` vs `qio_opi`), and the platform (his is unpinned
`espressif32` and needs Arduino-ESP32 2.x; ours is `@ 8.0.0`, Arduino 3.3.12).

Correction 2026-10-26: an earlier version of this section listed the custom board json and
`boards_dir = ..` as shared facts. Those came from Shawn's pre-project test edits, not from
upstream (see "Stock boards and upstream"). His board needs no `boards_dir`. The
throwaway B and C builds below used that edited ini. The A/B/C trade-offs still hold, but a
C base can no longer carry `board` or `platform`, because the two boards differ on both.

| | How | Pros | Cons |
|---|---|---|---|
| **A** (today) | Standalone `boards/esp32s3-flex/platformio.ini` | The AGENTS.md model: each board owns its config, and an edit to one board can't break another. No change to his files. | About 10 chip-level lines are duplicated. A fix to his esp32s3 ini (e.g. upload flags) has to be copied by hand. |
| **B** | Flex ini has `[platformio] extra_configs = ../esp32s3/platformio.ini` plus `[env:esp32s3-flex] extends = env:esp32s3`, and overrides lib_deps, build_flags, memory_type, sdkconfig_path, partitions and platform | No edit to his files. Reuses his env literally. | Most of his env has to be overridden, so very little is actually reused. His `[env]` section merges into our project too: if we leave out our own lib_deps we silently inherit his (ILI9341, LVGL, ...). His `env:esp32s3` and `env:esp32s3-debug` show up in our project's env list. Any later edit he makes to `env:esp32s3` silently changes our build. It also couples us to his file layout, and his unpinned platform needs Arduino-ESP32 2.x while flex needs 3.x, so the platform has to be overridden too. |
| **C** | Shared chip-level base `boards/common/esp32s3-base.ini` with an `[esp32s3_base]` section (board, framework, flash size, upload/monitor, PSRAM/USB flags). Both boards do `extra_configs = ../common/esp32s3-base.ini` and `extends = esp32s3_base`, and each keeps its own lib_deps, flags, PSRAM mode and sdkconfig. | Shares only what is truly the same chip. Nothing leaks between boards: no `[env]` merge and no foreign envs. Each board's diffs stay visible in its own ini. Fits Croaster's "board owns its config" rule, with the chip facts in one place. `boards/common` is a library dir without a `platformio.ini`, so `build_all.sh` doesn't pick up the base ini as a board. | Needs a small edit to his `boards/esp32s3/platformio.ini` (extra_configs, extends, and `${esp32s3_base.build_flags}`), which upstream has to accept. An edit to the base affects both boards, though that's intended. The platform pin has to be decided: either the base pins it for everyone, or each board pins its own. |
| **D** | Flex as a second env inside `boards/esp32s3`, separated with `build_src_filter` | One ini and one folder for the "esp32s3 chip". | Awkward: `src_dir`, `include`, `test` and `config.h` are per *project*, so two firmwares end up sharing one `src/` tree, filtered file by file. `-include src/config.h` and his `src/ui` collide. It breaks the AGENTS.md "one board = one folder + config" model and Croaster's env naming (`<board>`, `<board>-debug`). |

**Throwaway test builds (2026-10-26, espressif32 @ 8.0.0; test folders deleted afterwards):**

- **B builds.** A copy of flex with `extra_configs = ../esp32s3/platformio.ini` and
  `extends = env:esp32s3` plus the overrides above built with SUCCESS: RAM 62,332 B,
  flash 1,773,207 B, byte-for-byte the same sizes as standalone A.
  `pio project config` showed sections `env`, `env:esp32s3` and `env:esp32s3-debug` from his
  file inside our project. In a variant without our own lib_deps, the resolved flex env
  inherited his `[env]` lib_deps (Croaster, WebSockets, ArduinoJson, Adafruit ILI9341,
  MAX6675, WiFiManager, lvgl). That confirms the silent-leak risk.
- **C builds, both sides.** The test used a base ini with `[esp32s3_base]`. The flex copy with
  `extends = esp32s3_base` gave SUCCESS: RAM 62,332 B, flash 1,773,207 B (same as A).
  The copy of *his* esp32s3 board (as edited before this project) with
  `extends = esp32s3_base` (plus `boards_dir = ..`) also built `-e esp32s3`: SUCCESS, RAM 105,184 B, flash 2,518,962 B. Comparing his resolved
  env against the original, the only differences were build_flags order (same flags;
  `-W no-deprecated-declarations` normalized to `-Wno-...`) and the platform pin, because
  the test base pinned `@ 8.0.0`. If he wants to stay unpinned, leave `platform` out of the
  base and set it per board.

**Recommendation: C**, when Shawn is ready and upstream is OK with the small edit to the
esp32s3 ini. Until then, stay on **A**, which is what's in the tree now and is fully
reversible. C keeps the one useful property Shawn is after (the "esp32s3" facts live in one
place) without B's hidden coupling. Avoid B: it looks like reuse but needs nearly every
option overridden, and it makes our build depend on edits to his file.
**Reversible:** yes. Moving A→C only touches ini files.

### D11. Two MAX31855 implementations — resolved 2026-10-27: one decoder, in `croaster-control`
Shawn: the MAX31855 code is ours, written for this project, and belongs in
`croaster-control`, not in Croaster's library. Done:
- Removed from `croaster/`: `CroasterMAX31855.{h,cpp}`, `CroasterThermocoupleType`,
  the third `CroasterCore` constructor argument, `makeThermocouple`, the
  `library.json` header entry and the MAX31855 mentions in its description and
  keywords. `croaster/` now builds its two `SmoothThermocouple(MAX6675_Thermocouple)`
  exactly as upstream `5445a4d`.
- The flex monitor build (`FLEX_HEATER_NONE`) builds the same
  `hw/Thermocouples` as the control build and wraps it in
  `app/MonitorProbes`, two `Thermocouple`s passed to the sensor-injection
  constructor. No controller is built or started. Both `FLEX_TC_MAX31855` and
  `FLEX_TC_MAX6675` work.
- Behaviour change in the monitor build: readings go through `ProbeChannel`
  (4-sample average, NaN after 3 bad reads) instead of Croaster's
  `SmoothThermocouple`, the same filter the control build uses.

Analysis before the decision (2026-10-26). There were two: `croaster/src/CroasterMAX31855.*` (added by us, bit-banged,
decodes inline, used only by flex's monitor build `FLEX_HEATER_NONE` and by
any stock board that picks it) and `croaster-control/src/core/max31855.h`
(pure, host-tested decoder) read by `croaster-control/src/hw/thermocouples.cpp`
on hardware SPI. They decoded the same 32-bit frame twice.

Adafruit_MAX31855 was weighed against the in-house decoder: keep ours. One
32-bit read gives temperature, cold junction and fault bits together, where
Adafruit's `readCelsius()` and `readInternal()` each read the chip again. Ours
is host-tested and needs no Adafruit BusIO.
1. Keep both (until 2026-10-27). Core stays independent of `croaster-control`; the decode is
   written twice.
2. One decoder in core: move `max31855::decode` into `croaster/src`
   (e.g. `CroasterMAX31855::decode(uint32_t)`), have `CroasterMAX31855` and
   `hw/thermocouples` both call it, and point the host tests at it. One copy;
   `croaster-control` then includes a core header.
3. Drop `CroasterMAX31855` and flex's monitor build. Core goes back to
   MAX6675-only, removing one of our core changes; a temperature-only board
   then uses Croaster's stock MAX6675 boards.
4. Use Adafruit_MAX31855 in core. Smallest new code, but two reads per sample
   and a new dependency.
Recommendation: 2, after Shawn says whether core should offer MAX31855 to
other boards at all (if not, 3).
Chosen: 3's core change (core back to MAX6675 only), with the monitor build
kept and moved onto `croaster-control`.

### D12. Flex screen uses Croaster's SSD1306 display — **DECISION (new 2026-10-26)**
Flex had its own `RoasterOledDisplay` (U8g2), a second SSD1306 class next to
Croaster's `boards/common` one.
1. **Croaster's `CroasterDisplaySSD1306` plus `setHeaderText(fn)` (implemented).**
   The header shows heater %, fan % and mode, or the trip, alternating with the
   IP. One display class; drops U8g2.
2. Keep the U8g2 class. Supports SH1106 1.3" modules too, but duplicates
   Croaster's display.
3. Subclass `CroasterDisplaySSD1306` in the board. Needs its draw methods made
   virtual or protected, a bigger core edit than a hook.
4. No status on screen (plain Croaster screen). No core change; heater, fan and
   trip are visible only in Artisan.
Lost with 1: SH1106 modules and address 0x3D. Shawn's module is a 0x3C
SSD1306, so nothing he has changes. Reversible.

### D13. Firmware updates over Croaster's OTA only — **DECISION (new 2026-10-26)**
ArduinoOTA (espota) came from yaeger. Croaster already updates over WebSocket
and BLE (`OTA_BEGIN:<size>`, then binary chunks; the ICRM app uses it), and the
control task turns the heater off while `Update.isRunning()`. The
ArduinoOTA heater trip and the comms watchdog were separate code; they only
sat next to each other in `ControlPlugin`.
1. **Croaster's OTA only (implemented).** No espota env, no OTA password
   macro. Side effect: ArduinoOTA was also what advertised `croaster-flex.local`
   over mDNS, so the `.aset` host is now Croaster's AP address `192.168.4.1`;
   on home WiFi the user types the IP from the screen, as Croaster's README says.
2. Keep ArduinoOTA for `pio run -t upload` over WiFi. Handy for development,
   but a second update path with its own password.
3. Croaster's OTA plus `ESPmDNS` in the board's `main.cpp` (about 3 lines) so
   `croaster-flex.local` keeps working. A small new system Croaster doesn't
   have.
4. Croaster's OTA plus an mDNS option in core's WiFi setup, for every board.
   Most useful, but a core feature change for upstream to accept.
Recommendation: 1; take 3 if typing the IP into Artisan gets annoying.

### D14. Config wizard for `config.h` — **DECISION (new 2026-10-26, design only)**
Goal: someone builds flex without reading `config.h`: they answer questions
(probe chip, heater, fan driver, display, pins) and get a valid `config.h`.
`ConfigCheck.h` stays the authority: whatever the wizard writes must still
pass it at compile time.
1. **Standalone Python script `tools/flex-config.py` (recommended).** Asks
   questions in the terminal (or reads answers from a file), validates them
   with the same rules as `ConfigCheck.h` (reserved pins, duplicates, mains
   requires the dimmer), and writes `src/config.h` from a template. Run once,
   commit the result. No PlatformIO coupling; works from any shell.
2. PlatformIO `extra_scripts = pre:` that generates `config.h` from a
   `flex.ini` or JSON answers file on every build. The answers file becomes the
   master and `config.h` a build product, which reverses D2 (the header is no
   longer the file you edit) and hides errors in build logs.
3. JSON/ini answers file turned into a header by a small generator, run by
   hand. Like 1 with a file instead of questions; good for keeping several
   machines' configs side by side.
4. A web page (static HTML/JS in `tools/`) that renders `config.h` for
   copy-paste. Friendliest; a second implementation of the validation, in
   JavaScript.
Recommendation: 1, with 3's answers file as its non-interactive mode, so one
script serves both. Feasible: the template is today's `config.h` with
placeholders; the validation table is the `ConfigCheck.h` rules (pin list,
`unique()`, mains/dimmer). Ticket T25.

### D15. `MAX6675_Thermocouple` stays a core dependency — **DECISION (new 2026-10-26)**
Shawn asked to remove the MAX6675 library. Core needs it to compile:
`croaster/library.json` lists it, and `CroasterCore.h` includes
`MAX6675_Thermocouple.h`, `SmoothThermocouple.h` and `Thermocouple.h`. The
`Thermocouple` base class that `RoasterProbe` implements comes from it.
1. **Leave it to core (implemented).** Removed from flex's `lib_deps`;
   PlatformIO still installs it through `croaster/library.json`. No core change.
2. Define a `CroasterThermocouple` interface in core and include the MAX6675
   library only where a MAX6675 is built. Removes
   the dependency from control builds; touches every core sensor path.
3. Vendor the ~100 lines of the MAX6675 library into core. No external
   dependency, but upstream fixes stop flowing in.
Recommendation: 1. The library costs nothing in a control build beyond
compile time (the linker drops unused code).
Update 2026-10-26: still option 1, but the dependency is now Shawn's fork at
`v2.0.3` (D21).

### D16. Temperature unit — **DECISION (new 2026-10-26)**
Shawn wants °F on the screen and in Artisan. Croaster already has a unit:
`CroasterCore::changeTemperatureUnit()` converts BT, ET and RoR, the screen
prints the unit, and `getArtisanData` reports it in `"temp"`. The control
plugin already converts setpoints in and SV/ambient out with
`toCelsius`/`fromCelsius`, and the controller caps and trips in °C.
1. **Boot in a configured unit (implemented).** `FLEX_TEMP_UNIT` in `config.h`
   (default "F"); `main.cpp` calls `core.changeTemperatureUnit()` in `setup()`.
   No core change. The `.aset` ships in °F mode with `channel_modes` 2 (°F).
   The runtime `tempUnit` command still works until the next reboot.
2. Keep the firmware in °C and let Artisan convert (`channel_modes` 1, Artisan
   in °F). Artisan shows °F, but the OLED stays in °C, and Artisan sends
   setpoints in °F, which a °C firmware reads as °C.
3. Persist `tempUnit` in NVS from core. Affects every board; not needed while
   `config.h` sets the default.
Artisan's mode and the firmware unit must match: Artisan sends slider and
button temperatures in its own mode, and the firmware reads them in its unit.
The header line now says "Fan" ("H50 Fan60 PID") so the fan % isn't read as a
°F value.

### D17. Visible autotune status — resolved 2026-10-26: telemetry and OLED (option 3)
Shawn chose option 3. The controller keeps an `AutotuneStatus` (idle,
running, done, failed, aborted, need setpoint, need probe), a reason and a
counter that bumps on every update, so a repeated refusal counts as a change.
`core/autotune_status.h` writes the text in the display unit:
`autotune` in `getArtisanData` and `getControl` ("failed: timeout",
"aborted: comms", "need setpoint >= 212 F"), and a 20-character OLED form
("AT: SV<212F", "AT fail: small swing") that replaces the header for 10 s;
a trip line takes precedence. The field keeps the last status, so a refusal
stays visible in telemetry. Aborts name the cause when there is one (`stop`,
a trip name, `setpoint changed`). The gate and exit behaviour are unchanged.

Original options:

Autotune refuses to start below a 100 °C (212 °F) setpoint or without a valid
probe, and later ends as done, failed (timeout, over temperature, sensor fault,
oscillation too small) or aborted. The reason is only in `getControl`'s
`autotune` field, so from Artisan a refused start looks like nothing happened.
1. **`autotune` field in `getArtisanData`** (e.g. `"running"`, `"done"`,
   `"need setpoint >= 100 C (212 F)"`). Visible in Artisan's serial log and any
   client; Artisan has no way to show an arbitrary field on screen. ~40 bytes
   per sample.
2. **OLED header** shows the status for ~10 s after it changes ("AT: SV<212F",
   "AT done", "AT timeout"). Visible at the roaster; 20 characters is tight.
3. 1 and 2 together.
4. Map rejection to a trip-like code in `trip` (e.g. `"autotune"`). Reuses an
   existing field, but mixes a refused command with safety trips.
Recommendation: 3. **Implemented.**

### D18. PID input and default gains — resolved 2026-10-26: BT by default (option 2)
Shawn chose option 2. `FLEX_PID_INPUT` in `config.h` (`FLEX_PID_INPUT_BT`)
is the first-boot default; `{"command":{"pidInput":"ET"}}` still switches at
runtime and saves to NVS. yaeger's gains stay as defaults, with a `config.h`
note that they were chosen for ET and need an autotune on BT.

NVS: the input is stored (key `in`) only when a `pidInput` command (or,
before D8, a flat `Target`) was received. A board that never got one follows
the new default. A board that did keeps its saved input, because that was an
explicit choice; the firmware doesn't overwrite it. Telemetry `Target` and
`getControl` `pidInput` show the active input; `{"command":{"pidInput":"BT"}}`
sets BT and saves it.

Without a BT probe, PID mode trips `sensor` after 2 s and autotune refuses to
start ("valid probe"). Use `{"command":{"pidInput":"ET"}}` until BT is fitted.

Analysis before the decision:
The firmware PID follows `Target` (ET by default, as in yaeger, saved in NVS;
`{"command":{"pidInput":"BT"}}` changes it). Default gains are yaeger's
`Kp 1, Ki 0.1, Kd 0.01` (`FLEX_PID_KP/KI/KD`), in % heater per °C, per °C·s
and %·s per °C. They are the controller's live gains, not leftover fields.
They are gentle: a 10 °C error asks for 10 % heater plus 1 % per second from
the integral, so the output moves slowly (the 0.5 %/s decay seen on the bench
is the integral at work). Autotune replaces them.
1. Keep ET, autotune on the real heater. ET responds within
   seconds of a heater change, so the loop is stable; BT targets are reached
   by choosing ET setpoints (profiles or Artisan alarms).
2. **Default to BT (implemented).** Matches roast thinking, but BT lags the heater by tens of
   seconds and sits in the bean mass, so a single PID on BT overshoots and
   oscillates unless tuned very softly.
3. Cascade: an outer BT loop sets the ET setpoint of the inner ET loop. Best
   tracking of BT, but new code and two sets of gains.
4. Raise the defaults (e.g. Kp 4, Ki 0.05, Kd 0) so an untuned controller is
   less sluggish. Guesswork until measured on the hardware.
Recommendation was: 1 now, run autotune on ET once mains and the heater are
connected; consider 3 if BT tracking matters after real roasts.

### D19. Cooldown fan default — resolved 2026-10-26: 65 % after stop, 100 % after a trip (option 3)
Shawn chose option 3. `cooldownFan` is now two settings, `stop` (65 %) and
`trip` (100 %): defaults `FLEX_COOLDOWN_FAN_STOP`/`FLEX_COOLDOWN_FAN_TRIP`,
runtime `{"command":{"cooldownFan":{"stop":N,"trip":N}}}` (a plain number
still sets `stop`; yaeger's `cooldownFanSpeed` is `stop`), NVS keys `cool`
and `coolTrip`, and `getControl` reports both. The controller applies the trip
speed once when any trip starts (comms, sensor, no mains, OTA), so a fan
command while tripped still works; over-temperature always runs the fan at
100 %. A board that already stored `cool` in NVS keeps that stop value.

Before: `FLEX_COOLDOWN_FAN` (100 %) was the fan after `stop` and after a comms trip.
yaeger stores 65 % for its cooldown preference but uses 100 % on disconnect.
1. 100 %. Fastest cooling; can blow light beans out of a fluid-bed
   chamber.
2. 65 % (yaeger's stored default). Gentler; slower cooling.
3. **65 % after `stop`, 100 % after a trip (implemented).** Two settings.
Either way it is a runtime setting (`{"command":{"cooldownFan":N}}`, NVS).
Recommendation was: Shawn picks by testing on the roaster.

### D20. Roast rules and profiles — **DECISION (new 2026-10-26, open)**
Hard requirement: the PID runs on the ESP32; Artisan only sends setpoints and
commands. Example rule: heater 100 % from the start with a BT target of
383 °F; at 90 % of it, heater 65 % and fan from 20 to 50 %.
1. **Artisan alarms.** Conditions on BT/ET/extra curves, time since CHARGE, or
   another alarm ("If Alarm"); actions move a slider (which sends its WebSocket
   command) or press an event button. The example is two alarms
   (BT > 345 °F → Heater slider 65 and Fan slider 50). No firmware change;
   lives in the `.aset`/alarm file. Needs Artisan running; the comms watchdog
   covers a crash.
2. **Artisan background replay.** Load a previous roast as background and turn
   on event playback by time or by BT. Repeats a good roast's slider moves.
   No firmware change.
3. **Firmware profile (exists, from yaeger).** `{"command":{"profile":[...]}}`
   then `startProfile`: setpoint (on the PID input) and fan by elapsed time,
   with ramps. Runs without Artisan (watchdog exempt). Time-based only; no
   "when BT reaches X" rules. Croaster itself has no profiles.
4. Firmware rule steps: extend profile steps with triggers (BT above X) and
   actions (heater %, fan %, setpoint, mode). Standalone and conditional, but
   new code, a new JSON format and a UI to write it.
Artisan's ramp/soak and "follow background" drive Artisan's own software PID,
which can't set our firmware's setpoint over WebSocket; they don't fit the
requirement.
Recommendation: 1 now (no code), 2 once a good roast exists, 4 only if roasts
must run without a laptop.

### D21. `MAX6675_Thermocouple` fixes — resolved 2026-10-26: maintained fork (option 3)
The upstream library (yuriisalimov 2.0.2, last release 2019) had three faults
that reach Croaster's MAX6675 boards and the flex monitor build:
- `SmoothThermocouple` latched NAN. One open-probe reading was blended into
  the average, and because the "no average yet" check was `data == 0`, the
  average never reseeded. The probe read `N/A` until reboot.
- `SmoothThermocouple::MIN_SMOOTHING_FACTOR` had no out-of-line definition,
  so -O0 builds failed to link. Croaster carried
  `croaster/src/SmoothThermocoupleFix.cpp` for this.
- `MAX6675_Thermocouple::readCelsius()` waited 1 ms per SCK half period, so
  each read blocked for about 33 ms (two probes: about 66 ms every 250 ms).

Options:
1. Keep upstream and work around it in core. The link fix already lived in
   core; the NAN latch and the read time would need core code around a
   library we don't control.
2. A smoothing class in core that replaces `SmoothThermocouple`. Fixes the
   latch, but not the read time, and duplicates library code in core.
3. **Fork the library and fix it there (chosen by Shawn).**
   <https://github.com/shawns1234/MAX6675_Thermocouple>, tag `v2.0.3`:
   - the average reseeds after a NAN;
   - a smoothed value of 0 no longer restarts the average;
   - the factor clamp no longer odr-uses the constant;
   - the waits are 1 us, citing the MAX6675 datasheet's 100 ns timing
     figures. The bits read are unchanged.
   The fork has host tests (`test/host`).

Core changes: `croaster/library.json` depends on
`https://github.com/shawns1234/MAX6675_Thermocouple.git#v2.0.3`; the stock
boards (`esp32s3`, `esp32c3`, `esp8266`) no longer list the registry
package; `SmoothThermocoupleFix.cpp` is removed. Never list
`yuriisalimov/MAX6675_Thermocouple` next to the fork: PlatformIO installs both
and the link fails with multiple definitions.

Behaviour change on MAX6675 boards: unplugging a probe shows `N/A` and
plugging it back in shows the new temperature on the next read (250 ms),
without a ramp from the old value. The flex control build is unaffected (its
probes go through `RoasterProbe` and `ProbeChannel`, not `SmoothThermocouple`).

### D22. Correction offsets reach control — resolved 2026-10-26: core getters (option 1)
Croaster adds `correctionBt`/`correctionEt` to every temperature it shows. The
offsets are stored as numbers in the current display unit, and clients send
setpoints on that corrected scale. The control task reads the probes itself,
so before this change PID and autotune ran on uncorrected °C. With
`correctionBt` +10 in °F mode, PID held the uncorrected BT at SV, and the
display read SV + 10 °F.

Options (Shawn: the PID must never control on the wrong scale):
1. **Core getters (chosen).** `CroasterCore::correctionBtCelsius()` and
   `correctionEtCelsius()` return the offsets as °C differences (÷ 1.8 in
   °F). `ControlPlugin::loop()` polls them and sends `SetCorrection` when
   either changes. Two additive const getters; stock boards never call them.
2. Parse `correctionBt`/`correctionEt` again in the plugin (`onJsonCommand`).
   No core change, but it duplicates core's parsing and unit rules, needs
   `tempUnit` parsed as well, and breaks silently if core changes how it
   stores corrections.
3. Core hands corrected values to the controller. Core would have to know
   about a controller, and the SPI bus would no longer belong to the control
   task alone (D7).

Rules, in `core/probe_correction.h` (host-tested) and the controller:
- PID, autotune and the setpoint compare use corrected values.
- The snapshot, and so `RoasterProbe`, stays uncorrected: CroasterCore adds
  the offset itself, and correcting both would count it twice.
- Safety limits use the higher of the raw and the corrected reading.
  Over-temperature trips when either is above `FLEX_OVER_TEMP_C`, so a
  negative offset cannot hide a hot probe, and a positive one trips early.
- Autotune's abort above `FLEX_OVER_TEMP_C` compares the corrected input; the
  raw over-temperature trip ends autotune as well, so the higher reading wins
  here too.
- `FLEX_MAX_SETPOINT_C` caps the setpoint, which is on the corrected scale.
  With a negative offset the probe can run above the cap; the raw
  over-temperature trip still applies.
- The autotune gate (setpoint >= 100 °C / 212 °F) is on the setpoint, so it
  matches what the client sent. The cooldown fan has no temperature
  thresholds. `Amb` is the cold junction, which Croaster doesn't correct.
- A correction change during autotune aborts it ("aborted: correction
  changed", OLED "AT abort: offset"), since the step would distort the
  measured cycle. In PID or Profile mode the PID restarts bumpless, so the
  derivative term doesn't kick.
- The plugin treats a non-finite offset as 0. Corrections are still not saved
  by core and reset at reboot.
- A `tempUnit` change rescales both offsets (Shawn, 2026-10-27). Before, the
  stored number kept its value, so +10 °F became +10 °C (18 °F) after
  `{"command":{"tempUnit":"C"}}`. `CroasterCore::changeTemperatureUnit` now
  converts it as a difference: C→F ×9/5, F→C ×5/9, C↔K unchanged. The same
  unit again changes nothing. The °C getters return the same value before and
  after (to rounding), so the controller sees no correction change.

Stock boards: `esp32c3` and `esp8266` release builds have the same RAM and
flash use and the same warnings with the getters added (2026-10-26).

### D23. Autotune and control review — fixes made, proposals **DECISION (new 2026-10-26, open)**
Review of autotune end to end and of the control code. Research and sources:
`docs/research/autotune.md`; usage: flex README, "Autotune".

Verdict: the autotune is a real Åström–Hägglund relay test with consistent
units (heater request % per °C, seconds). yaeger's vendored AutoTunePID
toggles every 2 s whatever the PV does, so its gains are the same on every
roaster. Ours is new code, not yaeger's. On a simulated roaster it found Ku 17–28 % under the
analytic value and Pu within 1–16 %, finishing in 4–14 min. Low Ku means
softer gains. Results don't change with ±1 °C of noise. Every abort path was
traced. After success the gains are live and saved to NVS, and the board goes
to Manual, heater 0, fan 30 %.

Fixed (clear bugs, host-tested):
- **`pidInput` change didn't stop autotune or reset the PID.** The PV steps
  (BT → ET can be 50 °C), so autotune measured the step as part of a cycle,
  and PID took a derivative kick. Now it follows D22's correction rule:
  autotune aborts ("aborted: input changed", OLED "AT abort: input"), and PID
  and Profile restart bumpless. Same input again is a no-op. `onPvStep()` in
  `control_types.h`; `test_pv_step_stops_autotune_and_resets_pid`.
- **Manual heat with both probes dead didn't trip.** The `SensorFault`
  comment and `how.md` said it did, but the "both probes" clause sat behind
  `mode != Manual` and could never fire. With no readable probe the
  over-temperature trip is blind. Now Manual trips `sensor` after 2 s when
  the heater is on and neither probe reads; heater 0 never trips.
  `sensorFault()`; `test_sensor_fault_rules`.
- **False `nomains` trip.** `FanDimmer::mainsDetected()` read the timer, then
  the ISR's last-crossing time. A crossing in between made the age wrap to
  about 2^64 µs: "no mains", a latched trip while heating with
  `FLEX_REQUIRE_MAINS 1`. It now reads the crossing first (until two reads
  agree, against a torn 64-bit read) and treats a crossing after "now" as
  recent. `core/zero_cross.h`; `test_zero_cross_age`.
- **Mains grace period** (Shawn, 2026-10-27: "2-3 s is fine"). With
  `FLEX_REQUIRE_MAINS 1`, one missed 100 ms check while heating tripped
  `nomains`. It now trips only after mains has been missing for
  `FLEX_MAINS_GRACE_MS` (default 2000, 0..10000; 0 is the old behavior)
  without a break while the heater is on, like the 2 s sensor-fault delay.
  Mains back or heater 0 resets the count. Cost: with no AC at all the heater
  runs up to 2 s before the trip. `core/mains_watch.h`;
  `test_mains_dropout_under_grace_does_not_trip`,
  `test_mains_absent_past_grace_trips`, `test_mains_heater_off_resets_grace`.

Proposals, highest priority first (no code changed):
1. **Profile end holds the last setpoint forever, unattended.** The
   controller ignores `ProfileTarget::finished`, and Profile mode is exempt
   from the comms watchdog. Options: (a) at the end, stop (heater 0, stop
   cooldown fan); (b) hold, but re-arm the watchdog once finished; (c) keep.
   Recommendation: a.
2. **Relay swing.** The relay is 0/100 %, so PV swings ±10 to ±45 °C around
   SV on the simulated plants. Options: (a) keep, and pick SV with room
   below 275 °C; (b) relay ± a step around a bias (br3ttb `oStep`); (c)
   Marlin's moving bias, which also evens out unequal heat and cool halves.
   Recommendation: a until a hardware run shows the swing; c if it is too
   big.
3. **No convergence check.** It averages 4 cycles after skipping one. If
   the roaster is still heat-soaking, they drift. Option: finish only when
   the last 3 amplitudes and periods agree within 10–20 % (as br3ttb does), or
   else keep going until the timeout. Recommendation: add; it is small.
4. **Fan change during autotune** changed the plant but didn't abort.
   **Done 2026-10-27 (Shawn): abort.** A fan command that changes the speed
   ("aborted: fan changed", OLED "AT abort: fan") or any heater command
   ("aborted: heater changed", OLED "AT abort: heater") ends autotune with the
   heater off, as SV, mode, `pidInput` and correction changes already did.
   The heater value is dropped; the fan value applies. The start rule (fan
   below 30 % → 60 %) is the controller's own write, not a command, so it
   never aborts. `outputCommandStopsAutotune()` in `control_types.h`;
   `test_output_command_stops_autotune`, `test_autotune_status_text`.
5. **Diagnostics.** Ku and Pu are computed but not reported. Option: add
   `autotuneKu`/`autotuneTu` to `getControl`. Recommendation: yes; it lets
   us compare runs and rules without re-tuning.
6. **Rule choice.** Fixed Tyreus–Luyben PID. Option:
   `{"command":{"autotune":{"rule":"TL"|"ZN"|"PI"}}}`. TL PI suits BT's lag;
   ZN overshoots. Recommendation: wait for one hardware run (needs 5).
7. **Repeated `autotune` restarts** a running test silently. Option: ignore it
   while running. Recommendation: ignore.
8. **After success** the board goes to Manual, heater 0, as yaeger does.
   Option: continue in PID at the tuned SV. Recommendation: keep; the
   operator chooses.
9. **Ku formula.** Ours is 4d/(π√(a² − h²)). The textbook form 4d/(πa)
   (br3ttb, Marlin) differs by under 2 % once a ≥ 5h, and the simulated
   swings were 10h to 45h. Recommendation: keep.
10. **Safety commands can be dropped.** `submit()` doesn't block, and a full
    queue (16) drops the command; the plugin ignores that for `stop`. The
    queue drains every 10 ms, so it is unlikely. Option: send Stop,
    EmergencyOff and Trip to the front and retry on failure. Low priority.
11. **Controller is not host-testable** (FreeRTOS, hardware outputs inline),
    so each fix needs a pure helper (`startOutputOnModeChange`, `onPvStep`,
    `sensorFault`). Option: put the heater, fan, probes and clock behind
    small interfaces and keep FreeRTOS in `run()`/`snapshot()` only, so
    `apply()`/`controlStep()` compile on the host and trips and aborts get
    end-to-end tests. Cost: one refactor of `controller.*`. Recommendation:
    do it before more control features.
12. **Debug env warning** (existing): `-D CORE_DEBUG_LEVEL=3` redefines the
    platform's value, one warning per file. Option: `build_unflags =
    -DCORE_DEBUG_LEVEL=0`. Low priority.

### D24. Autotune before PID and profiles — resolved 2026-10-27 (Shawn)
The default gains are yaeger's, chosen for ET on other hardware (D18), so a
PID or profile run on a fresh board controls on guesses. Shawn made autotune
mandatory, behind a switch.

- `FLEX_REQUIRE_AUTOTUNE` (`config.h`, default 1; ConfigCheck: 0 or 1). With
  1 and no tuned gains, `mode: "pid"` and `startProfile` are refused: the mode
  stays, `autotune` reads `autotune required`, OLED `Autotune first` for 10 s.
  A loaded profile stays loaded. Manual and autotune are always allowed.
  With 0, nothing changes: the default gains are usable.
- Tuned is stored as NVS key `tuned` (namespace `control`), written by
  `ConfigStore::saveGains`. Gains with no `tuned` key but a saved `kp` (boards
  from before this change) count as tuned.
- **Manually set gains count as tuned** (decision). The `pid` command and
  yaeger's `setPreferences` gains are a deliberate choice by the operator,
  for example gains from an earlier board or another tool; requiring an
  autotune on top would only force a run they don't need. A failed or
  aborted autotune saves nothing and doesn't count.
- The controller holds the flag in the snapshot (`gainsTuned`; `getControl`
  `tuned`). It is set at boot from NVS, on `SetGains`, and when autotune
  completes. The gate itself is `modeNeedsTuning()` in `control_types.h`
  (host test `test_autotune_required_gate`).
- Profile behavior is otherwise unchanged.

## Build platform and sdkconfig
- Default: `platformio/espressif32 @ 8.0.0` (Arduino-ESP32 3.3.12, IDF 5.5).
  Arduino builds ignore `board_build.sdkconfig_path`; stock libs already have
  PSRAM malloc on.
- pioarduino 55.03.312 (same Arduino core) builds too; only it can apply
  `custom_sdkconfig` (recompiles IDF libs).
- Flex sets `board_build.sdkconfig_path` like Croaster's esp32s3 board
  (Shawn, 2026-10-26) and, since 2026-10-27, points it at his file:
  `../esp32s3/esp32s3-sdkconfig.defaults` (D10). It has no `custom_sdkconfig`
  env. To apply the file, add
  `custom_sdkconfig = file://../esp32s3/esp32s3-sdkconfig.defaults` to a
  pioarduino env (pioarduino reads a relative `file://` path from the project
  folder); the first build takes about 2 minutes.
- Checked 2026-10-27: no installed platform reads `board_build.sdkconfig_path`.
  espressif32 8.0.0, 6.13.0 and pioarduino 55.03.312 look for
  `board_build.esp-idf.sdkconfig_path`, and only when ESP-IDF itself is
  compiled (`framework = espidf`, or pioarduino's `custom_sdkconfig`). The line
  records which file applies, here and in Croaster's esp32s3 ini; it changes
  nothing in an Arduino build.
- `esp32s3-sdkconfig.defaults` findings (checked in the generated sdkconfig):
  - `MBEDTLS_EXTERNAL_MEM_ALLOC=y`, WiFi/LwIP in PSRAM, NimBLE 1 connection:
    applied.
  - `MBEDTLS_SSL_IN/OUT_CONTENT_LEN=4096` are **not applied**: IDF 5.5 only
    uses them with `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y`; the max stays
    16384. The WSS fix works through PSRAM allocation, not smaller buffers.
  - `MBEDTLS_HARDWARE_AES=n` slows TLS; only useful if a DMA/PSRAM AES issue
    was observed.
  - This firmware uses no TLS (plain `ws://`), so none of it is required.

## Hardware notes driving defaults
See `boards/esp32s3-flex/README.md` (pins, power, probe isolation, IPEX).
- Display **confirmed by Shawn (2026-10-26)**: 0.96" I2C 128×64 SSD1306
  module, address 0x3C. It uses Croaster's shared `CroasterDisplaySSD1306`
  (Adafruit SSD1306). The header line shows heater %, fan % and mode, or the
  trip, alternating with the IP.

## Related docs
`how.md` (walkthrough), `architecture.md` (shape, red flags, open questions),
`control-parity.md` (yaeger vs ours, item by item), `artisan-settings.md`
(every `.aset` setting with its source).
