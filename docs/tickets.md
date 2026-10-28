# Tickets — flex control board on Croaster

Tracer-bullet tickets: each one is a thin end-to-end slice that can be checked
on hardware. Status: `done`, `todo`, `blocked`. "Blocked by" lists edges.

| # | Ticket | Status | Blocked by |
|:---|:---|:---|:---|
| T1 | Skeleton: board project, config.h + ConfigCheck, control-core/control-esp32 libs, builds on espressif32 8.0.0 + pioarduino, 15 host tests | done | — |
| T2 | Bench bring-up, no mains: flash, WiFiManager portal, OLED shows IP + BT/ET from both MAX31855; `getArtisanData` in a browser WS client | todo | T1 |
| T3 | Probe check: both probes read room temp; boiling water ≈ 100 °C; unplug ET only → ET `N/A` on the screen and missing from `getArtisanData`, BT unaffected; unplug the PID input probe while heating → `trip:"sensor"` | todo | T2 |
| T4 | Fan on mains: dimmer Z-C detected (`mainsDetected`), fan 30/60/100 % audibly different; unplug mains → `trip:"nomains"` | todo | T2 |
| T5 | Heater on bench lamp/load via SSR: `burner` 0/50/100 % gives correct window duty; fan interlock (heat forces fan ≥ 30 %) | todo | T4 |
| T6 | Safety drills: over-temp (set `FLEX_OVER_TEMP_C` low), close Artisan while heating → `trip:"comms"` after 10 s and fan to cooldown; `{"command":{"watchdog":{"enabled":false}}}` → no trip, survives reboot, Croaster OTA (ICRM app) start → heater off | todo | T5 |
| T7 | Artisan: load `artisan/croaster-flex.aset`; BT on Input 1, ET on Input 2; Heater/Fan/SV curves; Fan/Setpoint/Heater sliders; PID/Manual/Autotune/Stop/Clear trip buttons act on the firmware (`docs/artisan-settings.md`) | todo | T5 |
| T8 | PID: setpoint hold on the roaster empty; autotune run; gains saved to NVS survive reboot | todo | T6 |
| T9 | Profile: load the README example profile, `startProfile`, setpoint follows ramps, `stop` → cooldown fan | todo | T8 |
| T10 | ICRM app over BLE + WebSocket with board id `esp32s3-flex` (design D5) | todo | T2 |
| T11 | Decide D3/D6/D8/D9/D10/D11/D13/D14 with Shawn (list in `docs/architecture.md`). D1 resolved 2026-10-27 (T37); AGENTS.md layout lists `croaster-control/` | blocked | Shawn |
| T12 | Watchdog sees all client traffic via core `CroasterCommandHandler::lastCommandMillis()` (D6 option 1); `getData` alias removed | done | — |
| T13 | Stock boards build on a pinned platform. The `boards_dir` and `-W no-deprecated-declarations` problems came from pre-project test edits and are gone since `boards/esp32s3` was restored to upstream (design "Stock boards and upstream"). Left: upstream `boards/esp32s3` needs Arduino-ESP32 2.x (`ledcSetup`/`ledcAttachPin` in `LvglUi.cpp`) and fails on unpinned `espressif32` today, upstream `5445a4d` included; it builds pinned to `espressif32 @ 6.13.0`. Pin the c3/s3/8266 platforms, or port the backlight to the 3.x LEDC API, as upstream changes | todo | IiemB |
| T14 | Web UI (phase 2): control page served from LittleFS or embedded | todo | T9 |
| T15 | yaeger parity on hardware: `setBurner`/`setFan`, `setPreferences`/`getPreferences`, `Amb`/`wifiStrength` in data, setpoint 0→>0 PID reset (`docs/control-parity.md`) | todo | T8 |
| T16 | Test that parses the `.aset` slider/button commands and runs each through `controlproto`, so the file and the firmware can't drift | todo | — |
| T17 | Artisan setting check against source + docs (`docs/artisan-settings.md`) | done | — |
| T18 | Board renamed `esp32s3-roaster` → `esp32s3-flex` (folder, envs, board id, `.aset`) | done | — |
| T19 | Decide D10 (flex vs Croaster's esp32s3 ini: standalone / extends / shared base ini). sdkconfig part done 2026-10-27: flex points at `../esp32s3/esp32s3-sdkconfig.defaults`, its copy deleted | blocked | Shawn |
| T20 | Flex screen: Croaster's shared SSD1306 display + `setHeaderText()`; drop `RoasterOledDisplay`/U8g2 | done | — |
| T21 | Rename roaster-named code to flex/control (map in `docs/design.md`) | done | — |
| T22 | Croaster's OTA only (ArduinoOTA and `-ota` env removed), `default_16MB.csv`, flex `lib_deps` trimmed | done | — |
| T23 | Watchdog settings: `FLEX_WATCHDOG_ENABLED`/`FLEX_WATCHDOG_TIMEOUT_S` defaults, `{"command":{"watchdog":...}}` saved in NVS, shown in `getControl` | done | — |
| T24 | Two-probe path documented end to end (`docs/how.md`); per-probe fault filter moved to pure `ProbeChannel`; host tests for fault bits and independent channels | done | — |
| T25 | Config wizard `tools/flex-config.py` (design D14): questions or an answers file in, `src/config.h` out, same checks as `ConfigCheck.h`; README section | todo | Shawn (D14) |
| T26 | Library builds without warnings from Croaster or our code: `-Wreorder` in `CroasterBleManager`, `-Wformat` in `CroasterDeviceIdentity`; WiFiManager's own warning remains | done | — |
| T27 | °F by default (design D16): `FLEX_TEMP_UNIT`, OLED and telemetry in °F, `.aset` in °F mode, header says "Fan" instead of "F". Bench check: OLED in °F, Artisan reads BT/ET/SV in °F, a 383 °F setpoint comes back as SV 383 | done (bench check todo) | T2 |
| T28 | Bench findings 2026-10-26: Heater/Fan LCDs swapped and SV LCD empty (`.aset` `channel_nodes`); `Amb` 32 °F with no BT chip; leaving Autotune carried the relay's 100 % into Manual/PID; SV changes during Autotune ignored. Bench check: Heater/Fan/SV LCDs match the OLED, `Amb` near room temperature, Manual during Autotune → heater 0 | done (bench check todo) | — |
| T29 | Visible autotune status (design D17): `autotune` in data and `getControl` in the display unit; OLED header shows it for 10 s after each change. Bench check: SV below 212 °F + autotune → `need setpoint >= 212 F` and "AT: SV<212F" | done (bench check todo) | — |
| T30 | Roast rules: Artisan alarms first, firmware rules later if needed (design D20) | todo | Shawn (D20) |
| T31 | Test plan from `docs/test-plan.md`: turn the running list into ordered bench procedures; clear the "bench todo" items (T28 fixes, 383 °F echo) | todo | AC wiring for heat items |
| T34 | PID input BT by default (design D18): `FLEX_PID_INPUT`, `pidInput` command and NVS kept. Bench check: `Target` reads BT; autotune on BT once BT and AC are wired | done (bench check todo) | BT probe, AC |
| T33 | Cooldown fan: 65 % after `stop`, 100 % when any trip starts; `cooldownFan` `stop`/`trip` in config, command, NVS and `getControl` (design D19). Bench check: Stop → 65 %, comms trip → 100 % | done (bench check todo) | — |
| T32 | Drop yaeger flat keys and the `onOtherJson` core hook (design D8, Shawn 2026-10-26) | done | — |
| T35 | `MAX6675_Thermocouple` from Shawn's fork `v2.0.3` (design D21): NAN latch, zero sentinel, -O0 link and 33 ms read fixed in the fork with host tests; core pins the tag, stock boards drop the registry package, `SmoothThermocoupleFix.cpp` removed. Bench check: MAX6675 read timing and probe-unplug recovery (`docs/test-plan.md`) | done (bench check todo) | a MAX6675 board |
| T36 | Correction offsets reach control (design D22): core `correctionBtCelsius()`/`correctionEtCelsius()`, `SetCorrection` from `ControlPlugin`, PID/autotune on the corrected scale, over-temperature on raw or corrected, snapshot uncorrected; 5 host tests. Bench check: `docs/test-plan.md` "Correction offsets" | done (bench check todo) | T8 |
| T37 | One control library (design D1, Shawn 2026-10-27): `control-core/` + `control-esp32/` → `croaster-control/src/{core,hw,app}` with `git mv`; lib `CroasterControl`; `build_filter.py` compiles only `src/core` on the native env | done | — |
| T38 | Host-testable controller (design D23 item 11). Deferred by Shawn 2026-10-27. Ready for it: the controller's decisions already call pure `core/` helpers (`sensorFault`, `onPvStep`, `startOutputOnModeChange`, `crossedWithin`), and `app/controller` reaches the device only through `hw/` types (`HeaterOutput`, `FanOutput`, `Thermocouples`), `millis()` and `Update.isRunning()`. Next: put those behind small interfaces (outputs, probes, clock, OTA flag), keep FreeRTOS in `run()`/`snapshot()`, move the step logic to `core/`, and add end-to-end host tests for trips and aborts | todo | Shawn |
| T39 | One MAX31855 decoder (design D11, Shawn 2026-10-27): `croaster/` MAX31855 code removed (back to upstream's MAX6675 construction); flex monitor build reads through `hw/thermocouples` + `app/MonitorProbes`. Bench check: `docs/test-plan.md` "Flex monitor build" | done (bench check todo) | a monitor-config board |
| T40 | Mains grace period (D23, Shawn 2026-10-27): `nomains` after `FLEX_MAINS_GRACE_MS` (2000) of missing mains while heating; `core/mains_watch.h` + host tests. Bench check: `docs/test-plan.md` "Mains detection" | done (bench check todo) | — |
| T41 | Fan and heater commands abort autotune (D23 item 4, Shawn 2026-10-27): `outputCommandStopsAutotune()` + host tests; status `aborted: fan changed` / `aborted: heater changed`. Bench check: `docs/test-plan.md` "Autotune entry, exit and gates" | done (bench check todo) | — |
| T42 | Autotune required before PID and profiles (D24, Shawn 2026-10-27): `FLEX_REQUIRE_AUTOTUNE`, NVS `tuned`, `modeNeedsTuning()` + host tests; `pid` command counts as tuned. Bench check: `docs/test-plan.md` "Autotune required" | done (bench check todo) | — |
