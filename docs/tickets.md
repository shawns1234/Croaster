# Tickets — roaster control on Croaster

Tracer-bullet tickets: each one is a thin end-to-end slice that can be checked
on hardware. Status: `done`, `todo`, `blocked`. "Blocked by" lists edges.

| # | Ticket | Status | Blocked by |
|:---|:---|:---|:---|
| T1 | Skeleton: board project, config.h + ConfigCheck, roaster-core/-control libs, builds on espressif32 8.0.0 + pioarduino, 15 host tests | done | — |
| T2 | Bench bring-up, no mains: flash, WiFiManager portal, OLED shows IP + BT/ET from both MAX31855; `getData` in a browser WS client | todo | T1 |
| T3 | Probe check: both probes read room temp; boiling water ≈ 100 °C; grounded-probe fault shows as `trip:"sensor"` | todo | T2 |
| T4 | Fan on mains: dimmer Z-C detected (`mainsDetected`), fan 30/60/100 % audibly different; unplug mains → `trip:"nomains"` | todo | T2 |
| T5 | Heater on bench lamp/load via SSR: `burner` 0/50/100 % gives correct window duty; fan interlock (heat forces fan ≥ 30 %) | todo | T4 |
| T6 | Safety drills: over-temp (set `ROASTER_OVER_TEMP_C` low), close Artisan while heating → `trip:"comms"` after 30 s, OTA start → heater off | todo | T5 |
| T7 | Artisan: load `artisan/croaster-roaster.aset`, ET/BT curves, burner/fan sliders, BurnerVal/FanVal logged | todo | T5 |
| T8 | PID: setpoint hold on the roaster empty; autotune run; gains saved to NVS survive reboot | todo | T6 |
| T9 | Profile: load the README example profile, `startProfile`, setpoint follows ramps, `stop` → cooldown fan | todo | T8 |
| T10 | ICRM app over BLE + WebSocket with board id `esp32s3-roaster` (design D5) | todo | T2 |
| T11 | Decide D1/D3/D5/D6 with Shawn; update AGENTS.md layout if D1 accepted | blocked | Shawn |
| T12 | Optional core hook `onAnyCommand` for the comms watchdog (D6 option 2) | todo | T11 |
| T13 | Existing boards: fix `boards/esp32s3` platformio.ini (`boards_dir = ..`, `-Wno-deprecated-declarations`), pin c3/s3 platform versions | todo | — |
| T14 | Web UI (phase 2): roaster page served from LittleFS or embedded | todo | T9 |
