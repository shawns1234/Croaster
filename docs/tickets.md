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
| T11 | Decide D1/D3/D6/D8/D9/D10/D11/D13/D14 with Shawn (list in `docs/architecture.md`); update AGENTS.md layout if D1 accepted | blocked | Shawn |
| T12 | Watchdog sees all client traffic via core `CroasterCommandHandler::lastCommandMillis()` (D6 option 1); `getData` alias removed | done | — |
| T13 | Existing boards: fix `boards/esp32s3` platformio.ini (`boards_dir = ..`, `-Wno-deprecated-declarations`), pin c3/s3 platform versions | todo | — |
| T14 | Web UI (phase 2): control page served from LittleFS or embedded | todo | T9 |
| T15 | yaeger parity on hardware: flat keys (`BurnerVal`, `Mode`, `Setpoint`, `Target`), `setPreferences`/`getPreferences`, `Amb`/`wifiStrength` in data, setpoint 0→>0 PID reset (`docs/control-parity.md`) | todo | T8 |
| T16 | Test that parses the `.aset` slider/button commands and runs each through `controlproto`, so the file and the firmware can't drift | todo | — |
| T17 | Artisan setting check against source + docs (`docs/artisan-settings.md`) | done | — |
| T18 | Board renamed `esp32s3-roaster` → `esp32s3-flex` (folder, envs, board id, `.aset`) | done | — |
| T19 | Decide D10 (flex vs Croaster's esp32s3 ini: standalone / extends / shared base ini) | blocked | Shawn |
| T20 | Flex screen: Croaster's shared SSD1306 display + `setHeaderText()`; drop `RoasterOledDisplay`/U8g2 | done | — |
| T21 | Rename roaster-named code to flex/control (map in `docs/design.md`) | done | — |
| T22 | Croaster's OTA only (ArduinoOTA and `-ota` env removed), `default_16MB.csv`, flex `lib_deps` trimmed | done | — |
| T23 | Watchdog settings: `FLEX_WATCHDOG_ENABLED`/`FLEX_WATCHDOG_TIMEOUT_S` defaults, `{"command":{"watchdog":...}}` saved in NVS, shown in `getControl` | done | — |
| T24 | Two-probe path documented end to end (`docs/how.md`); per-probe fault filter moved to pure `ProbeChannel`; host tests for fault bits and independent channels | done | — |
| T25 | Config wizard `tools/flex-config.py` (design D14): questions or an answers file in, `src/config.h` out, same checks as `ConfigCheck.h`; README section | todo | Shawn (D14) |
| T26 | Library builds without warnings from Croaster or our code: `-Wreorder` in `CroasterBleManager`, `-Wformat` in `CroasterDeviceIdentity`; WiFiManager's own warning remains | done | — |
| T27 | °F by default (design D16): `FLEX_TEMP_UNIT`, OLED and telemetry in °F, `.aset` in °F mode, header says "Fan" instead of "F". Bench check: OLED in °F, Artisan reads BT/ET/SV in °F, a 383 °F setpoint comes back as SV 383 | done (bench check todo) | T2 |
| T28 | Bench findings 2026-10-26: Heater/Fan LCDs swapped and SV LCD empty (`.aset` `channel_nodes`); `Amb` 32 °F with no BT chip; leaving Autotune carried the relay's 100 % into Manual/PID; SV changes during Autotune ignored. Bench check: Heater/Fan/SV LCDs match the OLED, `Amb` near room temperature, Manual during Autotune → heater 0 | done (bench check todo) | — |
| T29 | Visible autotune status (design D17) | todo | Shawn (D17) |
| T30 | Roast rules: Artisan alarms first, firmware rules later if needed (design D20) | todo | Shawn (D20) |
