# Design — Croaster roaster control (ESP32-S3)

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

## Layers

```
boards/esp32s3-flex/          board: config.h, ConfigCheck.h, main.cpp, RoasterOledDisplay
        |  constructs + injects
        v
roaster-control/  (Arduino/ESP32)   RoasterPlugin  --registers-->  CroasterApp commands / onJsonData
                                    Roaster (FreeRTOS task)  <-- Command queue
                                    HeaterOutput, FanDimmer|FanPwm, Thermocouples, ConfigStore(NVS)
                                    RoasterProbe : Thermocouple  --> CroasterCore (BT/ET)
        |
        v
roaster-core/     (pure C++, host-tested)  PID, relay autotune, roast profile,
                                    MAX31855/MAX6675 decode, moving average,
                                    time-proportional window, command protocol
croaster/         (Croaster library) WiFi, WebSocket, BLE, OTA, RoR, display iface
```

Rules this keeps: nothing in `croaster/` knows about heaters or fans; board
config stays in the board; roaster logic is reusable by any future board
(e.g. a C3 roaster) by adding another `boards/<board>/` folder.

### Where each feature lands

| Feature | Lands in | Why |
|:---|:---|:---|
| MAX31855 support (monitor builds) | `croaster/src/CroasterMAX31855.*` + `CroasterThermocoupleType` | A converter chip is a library feature (pin-agnostic, any board). Same `Thermocouple` interface as MAX6675. |
| MAX31855/MAX6675 decode for control | `roaster-core/core/max31855.h`, `max6675.h` | Pure bit decoding, host-tested; control task reads the bus itself. |
| PID, autotune, profiles | `roaster-core` | Pure logic, unit-tested on the host (16 tests). |
| SSR, dimmer, PWM fan, probe reading, NVS | `roaster-control/hw`, `app/config_store` | ESP32 hardware, shared by any ESP32 roaster board. |
| Control loop + safety | `roaster-control/app/roaster` | Own FreeRTOS task (see Threading). |
| Commands, data fields, watchdog, ArduinoOTA | `roaster-control/RoasterPlugin` | Uses only Croaster's public extension points. |
| Pins, chip/driver/display choice, limits | `boards/esp32s3-flex/src/config.h` | Board config never in a library (AGENTS.md). |
| Roaster OLED screen | `boards/esp32s3-flex/src/RoasterOledDisplay.*` | Board display code, a `CroasterDisplay` subclass. |

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
   `RoasterPlugin` callbacks → `roasterproto` parse → `Command` queue → control
   task. Gains / PID input / cooldown fan are persisted to NVS by the plugin.

## Threading
- WiFi, LwIP and BLE (NimBLE) run on core 0 (IDF defaults).
- Croaster's board json pins Arduino loop + events to core 1; the control task
  also runs on core 1 at `configMAX_PRIORITIES - 3`, so it preempts the loop
  (WebSocket handling, display, JSON) and never waits for it.
- Only two shared objects: a FreeRTOS queue (commands in) and a
  spinlock-protected `Snapshot` (state out). The SPI bus belongs to the control
  task alone; that is why `CroasterCore` got a constructor taking sensors.
- Dimmer: zero-cross ISR + one-shot GPTimer fires the triac gate; both IRAM.

## Changes to Croaster's core (and why)

| Change | Needed? | Reason |
|:---|:---|:---|
| `CroasterCore(bool, Thermocouple* bt, Thermocouple* et)` | needed | Two owners of one SPI bus would race; this lets the control task own it. Additive, existing constructor unchanged. |
| `CroasterCore::onJsonData(fn)` | needed | Artisan must see heater/fan/setpoint in the same `getArtisanData` payload; no existing hook. Additive. |
| `CroasterThermocoupleType` + `CroasterMAX31855` | requested | MAX31855 alongside MAX6675 for monitor builds; default stays MAX6675. |
| `#ifndef CROASTER_HAS_BLE` | small | Lets a board turn BLE off (~310 KB less flash in our build). Default unchanged. |
| `CroasterCommandHandler::onOtherJson(fn)` | needed for yaeger wire compatibility | Messages without `"command"` (yaeger's `{"BurnerVal":60}`) were dropped. Additive fallback, called only when no other branch matched; WebSocket and BLE. See D8. |
| Removed negative-RoR clamp | **genuinely poor** | Any falling temperature reported RoR = 0 (`deltaET > 0` test), hiding the post-charge turning point and cooling. Artisan computes its own RoR, but the ICRM app and OLED showed 0 instead of the real negative rate. |

Existing boards still build: `esp32c3` built with the changes (RAM 44 696 B,
flash 1 741 985 B = 91.7 %; pinned to espressif32 8.0.0 for the check,
2026-10-26).

## Decisions where Croaster's rules and Shawn's goals rub

Each lists alternatives; the recommended one is implemented (all reversible).

### D1. Where the roaster libraries live — **DECISION**
AGENTS.md lists only `croaster/`, `boards/`, `builds/`.
1. **Top-level `roaster-core/` + `roaster-control/` libs (implemented).**
   Clear split: pure logic vs hardware; consumed like `boards/common` via
   `symlink://`. Bends the layout list (two new top-level folders).
2. Put them in `boards/common-roaster/` (one lib). Matches the "shared board
   code under boards/" precedent, but `build_all.sh` must skip it (it only skips
   `common`), and the pure/hardware split is lost.
3. Fold into `croaster/src/roaster/`. One library, but every board (ESP8266
   too) compiles heater/fan code and it breaks "monitor library, no board
   specifics" intent.
4. Keep everything inside `boards/esp32s3-flex/src/`. Zero layout change,
   but nothing is reusable for a second roaster board and tests live with one board.
Recommendation: 1; update AGENTS.md's layout section if accepted.

### D2. One master config vs `config.h` + `pins.h` — resolved
Croaster boards use `config.h` (pins/dummy/LED) and sometimes `pins.h`.
Shawn asked for a single file. Implemented: `src/config.h` holds everything
(name keeps the convention), `src/ConfigCheck.h` validates. Options considered:
split pins.h (convention, two files to edit), runtime JSON config (no compile
checks), Kconfig menus (pioarduino only).

### D3. Library feature switches from board config — **DECISION**
`CROASTER_HAS_BLE` must be seen when `croaster/` compiles.
1. **Force-include `config.h` into every unit (`-include`, implemented).** One
   file really is the master; cost: every library sees the `ROASTER_*` macros
   (prefixed, so collisions are unlikely).
2. `-D CROASTER_HAS_BLE=0` in `platformio.ini`. Croaster style, but the master
   config is then split across two files.
3. Runtime switch (`CroasterApp` option to skip BLE). BLE code still linked
   (no flash saving), library API change.
4. Always build BLE. Simplest; ~310 KB flash plus NimBLE heap at runtime (fine on N16R8).
Recommendation: 1.

### D4. Compile-time feature selection in `main.cpp`
AGENTS.md: "no board-specific compile-time guards" (meaning: the library never
`#ifdef`s on a board). Our `#if ROASTER_FAN == ...` are feature selections inside
one board, which the rule does not forbid, but they are guards. Alternatives:
separate board folders per variant (monitor / roaster; duplicates main.cpp),
runtime selection (all drivers linked, invalid configs only caught at boot),
or templates (more C++ than the codebase uses). Kept `#if` + `ConfigCheck.h`
because Shawn asked for compile-time selection with static checks.

### D5. Board id — resolved: `esp32s3-flex`, chosen by Shawn 2026-10-26
Ids must be lowercase and match ICRM's `CroasterBoardTypes`
(`esp32s3`/`esp32c3`/`esp8266`/`unknown`). Folder `boards/esp32s3-flex/`,
envs `esp32s3-flex[-debug|-ota|-pioarduino|-pioarduino-sdkconfig]`, board id
`"esp32s3-flex"`, mDNS/OTA hostname `croaster-flex`. "roaster" was dropped
because the whole project is a roaster, so it told you nothing. Options that
were considered:
1. **`"esp32s3-flex"` (chosen).** Distinct, so the app never offers the
   ES3C28P LCD image as an update. ICRM may show it as unknown until its enum
   is extended.
2. `"esp32s3"`. Matches the enum, but an ICRM OTA would flash the LCD firmware
   over this firmware (heater control lost).
3. `"unknown"`. Safe, but the app learns nothing.
4. Add `esp32s3Flex` to ICRM's enum (needs the app author). Still worth doing later.

### D6. Comms watchdog vs built-in `getArtisanData` — **DECISION**
The heater must stop if Artisan disappears, but `getArtisanData` is handled
inside Croaster and boards cannot see it.
1. **`getData` alias (implemented):** plugin registers `getData`, marks
   activity, returns `core().getJsonData(id)`. Artisan polls `getData`
   (the provided `.aset` does). No core change; ICRM/BLE traffic that does not
   use it counts only via roaster commands.
2. Add `CroasterCommandHandler::onAnyCommand(fn)` hook. Clean, small core change;
   counts every client.
3. Use WebSocket client count (`connected()` in `CroasterWebSocketManager`):
   catches drops, not a hung Artisan.
4. No watchdog (yaeger had none). Simplest, least safe.
Recommendation: 1 for now; 2 if the author accepts a core hook.
Update 2026-10-26: timeout cut from 30 s to 10 s (Artisan polls every 1 s),
and the trip now also sets the cooldown fan; see D9.

### D7. Control in a FreeRTOS task, not `loop()`
Croaster is loop-driven. A blocking WiFiManager portal, WebSocket burst or BLE
OTA can stall `loop()` for seconds; an SSR left on for seconds is the failure to
avoid. Options: loop-only (simplest, unsafe stalls), hardware-timer ISR (no SPI
reads in ISR), dedicated task (implemented), task on core 0 (shares core with
WiFi). Task on core 1 at high priority chosen.

### D8. yaeger wire compatibility (flat keys) — **DECISION**
Shawn wants yaeger's level of control. yaeger's clients send flat keys with no
`"command"` (`{"BurnerVal":60}`, `{"Mode":"PID"}`, `{"Setpoint":200}`) plus
`setBurner`/`setFan`/`setPreferences`/`getPreferences`. Croaster's handler only
routes messages that have `"command"`. The named commands fit `onCommand` with
no core change. Flat keys don't.
1. Nested only. Configure every client (Artisan `.aset`) to send
   `{"command":{...}}`. No core change, but yaeger's web UI and older settings
   files silently do nothing.
2. **`onOtherJson(fn)` fallback in `CroasterCommandHandler` (implemented).**
   About 5 lines, additive, called only for JSON objects without `"command"`,
   and works over WebSocket and BLE. Parsing stays in `roasterproto::parseLegacy`.
3. Pre-parse in `CroasterWebSocketManager` before the handler. WebSocket
   only (BLE misses out), and it touches transport code.
4. Fork/replace the handler in the plugin. No upstream change, but it
   duplicates Croaster's routing (two ways to do one thing).
Recommendation: 2. It is reversible: delete the hook and the registration.
The `.aset` uses nested commands, so Artisan doesn't depend on it.

### D9. What happens when the client goes away — **DECISION**
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
wanted. Set `ROASTER_COMMS_TIMEOUT_S 0` to disable it.

### D10. How the flex board relates to Croaster's esp32s3 board config — **DECISION (open, no change made)**

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

**What flex and his esp32s3 actually have in common:** the chip and module (ESP32-S3 N16R8,
`esp32-s3-devkitm1-n16r8.json`), 16 MB flash, USB-CDC flags, `BOARD_HAS_PSRAM`, and the
upload and monitor speeds. **What differs:** lib_deps (his has ILI9341 + LVGL; ours has
SSD1306/U8g2 + RoasterCore/RoasterControl), build_flags (his `LV_CONF_PATH` points into his
`src/ui`; ours does `-include src/config.h`), PSRAM mode (`dio_opi` vs `qio_opi`),
sdkconfig (his uses `esp32s3-sdkconfig.defaults`; ours is off by default), the partition
table, and the platform pin (his is unpinned `espressif32`; ours is `@ 8.0.0`).

| | How | Pros | Cons |
|---|---|---|---|
| **A** (today) | Standalone `boards/esp32s3-flex/platformio.ini` | The AGENTS.md model: each board owns its config, and an edit to one board can't break another. No change to his files. | About 10 chip-level lines are duplicated. A fix to his esp32s3 ini (e.g. upload flags) has to be copied by hand. |
| **B** | Flex ini has `[platformio] extra_configs = ../esp32s3/platformio.ini` plus `[env:esp32s3-flex] extends = env:esp32s3`, and overrides lib_deps, build_flags, memory_type, sdkconfig_path, partitions and platform | No edit to his files. Reuses his env literally. | Most of his env has to be overridden, so very little is actually reused. His `[env]` section merges into our project too: if we leave out our own lib_deps we silently inherit his (ILI9341, LVGL, ...). His `env:esp32s3` and `env:esp32s3-debug` show up in our project's env list. Any later edit he makes to `env:esp32s3` silently changes our build. It also couples us to his file layout and needs `boards_dir = ..`, which his ini lacks (T13). |
| **C** | Shared chip-level base `boards/common/esp32s3-base.ini` with an `[esp32s3_base]` section (board, framework, flash size, upload/monitor, PSRAM/USB flags). Both boards do `extra_configs = ../common/esp32s3-base.ini` and `extends = esp32s3_base`, and each keeps its own lib_deps, flags, PSRAM mode and sdkconfig. | Shares only what is truly the same chip. Nothing leaks between boards: no `[env]` merge and no foreign envs. Each board's diffs stay visible in its own ini. Fits Croaster's "board owns its config" rule, with the chip facts in one place. `boards/common` is a library dir without a `platformio.ini`, so `build_all.sh` doesn't pick up the base ini as a board. | Needs a small edit to his `boards/esp32s3/platformio.ini` (extra_configs, extends, `boards_dir = ..`, and `${esp32s3_base.build_flags}`), which upstream has to accept. An edit to the base affects both boards, though that's intended. The platform pin has to be decided: either the base pins it for everyone, or each board pins its own. |
| **D** | Flex as a second env inside `boards/esp32s3`, separated with `build_src_filter` | One ini and one folder for the "esp32s3 chip". | Awkward: `src_dir`, `include`, `test` and `config.h` are per *project*, so two firmwares end up sharing one `src/` tree, filtered file by file. `-include src/config.h` and his `src/ui` collide. It breaks the AGENTS.md "one board = one folder + config" model and Croaster's env naming (`<board>`, `<board>-debug`). |

**Throwaway test builds (2026-10-26, espressif32 @ 8.0.0; test folders deleted afterwards):**

- **B builds.** I made a copy of flex with `extra_configs = ../esp32s3/platformio.ini` and
  `extends = env:esp32s3` plus the overrides above. `pio run` gave SUCCESS: RAM 62,332 B,
  flash 1,773,207 B, byte-for-byte the same sizes as standalone A.
  `pio project config` showed sections `env`, `env:esp32s3` and `env:esp32s3-debug` from his
  file inside our project. In a variant without our own lib_deps, the resolved flex env
  inherited his `[env]` lib_deps (Croaster, WebSockets, ArduinoJson, Adafruit ILI9341,
  MAX6675, WiFiManager, lvgl). That confirms the silent-leak risk.
- **C builds, both sides.** I made a base ini with `[esp32s3_base]`. The flex copy with
  `extends = esp32s3_base` gave SUCCESS: RAM 62,332 B, flash 1,773,207 B (same as A).
  The copy of *his* esp32s3 board with `extends = esp32s3_base` (plus `boards_dir = ..`) also
  built `-e esp32s3`: SUCCESS, RAM 105,184 B, flash 2,518,962 B. Comparing his resolved
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

## Build platform and sdkconfig
- Default: `platformio/espressif32 @ 8.0.0` (Arduino-ESP32 3.3.12, IDF 5.5).
  Arduino builds ignore `board_build.sdkconfig_path`; stock libs already have
  PSRAM malloc on.
- pioarduino 55.03.312 (same Arduino core) builds too; only it can apply
  `custom_sdkconfig` (recompiles IDF libs).
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
  module. It is the default (`ROASTER_DISPLAY_ROASTER_SSD1306`, U8g2 full-buffer HW
  I2C, probes 0x3C then 0x3D).

## Related docs
`how.md` (walkthrough), `architecture.md` (shape, red flags, open questions),
`control-parity.md` (yaeger vs ours, item by item), `artisan-settings.md`
(every `.aset` setting with its source).
