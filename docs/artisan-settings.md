# Artisan settings — `boards/esp32s3-flex/artisan/croaster-flex.aset`

What each setting in the `.aset` does, why it has that value, and where that was
checked. Artisan sources are cited at commit `5be39f6` of
`github.com/artisan-roaster-scope/artisan` (`src/artisanlib/...`), plus the
WebSocket device docs at <https://artisan-scope.org/devices/websockets/>.

## How the file was first made

The first version was **not researched**. It was a mechanical edit of yaeger's
`.aset`: port changed to 81, `getData` polling added, and the slider and button
commands rewritten to Croaster's nested JSON. Nothing else was checked against
Artisan's source. It inherited these faults from yaeger:

- **BT and ET were swapped.** `channel_nodes=ET, BT` puts ET on Input 1. Artisan
  treats Input 1 as BT (see table), so the curves were labelled the wrong way round.
- The Control and PID buttons were turned on. Those start **Artisan's own
  software PID**, which can't reach our firmware (see below).
- There were no curves for heater %, fan % or setpoint.
- The sampling interval was 2 s.
- The WebSocket `path` was empty.

yaeger's own firmware couldn't serve this file anyway. It never answers
`getData` with the request `id`. Artisan drops any reply without a matching id,
so it read nothing. yaeger users depended on the `id`-less 100 ms broadcast,
which current Artisan ignores.

This version was checked line by line against the Artisan source and our
firmware (`control-core/src/control/control_protocol.cpp`,
`control-esp32/src/ControlPlugin.cpp`).

## How Artisan talks to the roaster

```mermaid
sequenceDiagram
    participant A as Artisan (PC)
    participant F as ESP32 firmware
    loop every Delay (1 s)
        A->>F: {"command":"getArtisanData","id":N,"roasterID":0}
        F-->>A: {"id":N,"data":{"BT":..,"ET":..,"BurnerVal":..,"FanVal":..,"SV":..,...}}
    end
    Note over A: Inputs 1-6 filled from one reply
    A->>F: slider / button: {"command":{"burner":60}}
    Note over F: firmware applies it; no reply needed
```

## Settings

| Section / key | Value | Why | Source |
|:---|:---|:---|:---|
| `[Device] id` | `111` | 111 = the "WebSocket" device | `device_registry.py:152` |
| `[WebSocket] host` | `192.168.4.1` | Croaster's own access point address (README, "Option 1"). **On your home WiFi, put in the IP shown on the OLED.** Croaster advertises no mDNS name, so `.local` does not resolve (design D13). | `README.md` "How to Connect Croaster with Artisan" |
| `[WebSocket] port` | `81` | Croaster's WebSocket server port | `croaster/src/CroasterWebSocketManager.*` |
| `[WebSocket] path` | `WebSocket` | Artisan connects to `ws://host:port/path`. Croaster's server answers on any path; `WebSocket` is what Croaster's own setup screenshot uses. | `wsport.py` (URL build); `images/Connect-Artisan-Same-Network.png` |
| `[WebSocket] request_data_command` | `getArtisanData` | Croaster's own data command. One request fills **all** inputs; Croaster answers with the same `id`. Like any message, it also keeps the comms watchdog satisfied (`CroasterCommandHandler::lastCommandMillis()`). | `comm.py:1944-1955`; `croaster/src/CroasterCommandHandler.cpp` |
| `[WebSocket] channel_requests` | all empty | Only used when `request_data_command` is empty. Leaving them empty avoids 10 extra requests per sample. | `comm.py:1958-1968` |
| `[WebSocket] channel_nodes` | `BT, ET, BurnerVal, FanVal, SV, , , , ,` | **Fixed twice.** First (was `ET, BT`): Input 1 is BT and Input 2 is ET. Then (2026-10-26 bench test, was `FanVal, BurnerVal, , SV`): the Heater and Fan LCDs showed each other's values and the SV LCD read the empty Input 5. `WSread` returns `(tx, readings[2m+1], readings[2m])` and the sampler unpacks extra devices as `extratx, extrat2, extrat1`, so extra curve 1 is the odd input (3, 5) and curve 2 the even input (4, 6). Field names are the keys our data hook adds to `getArtisanData`. | `comm.py:1973`; `canvas.py:19458`; `control_protocol.cpp` `writeDataFields` |
| `[WebSocket] channel_modes` | `2, 2, 0, 0, 2, 0, 0, 0, 0, 0` | **Changed (was 1 = °C).** 2 = a temperature sent in °F, matching the firmware's default `FLEX_TEMP_UNIT "F"`; 0 = a plain number (heater %, fan %). SV (Input 5) is a temperature. Artisan converts a channel only when its mode differs from Artisan's own mode, so readings stay right even if Artisan is switched to °C. If you set the firmware to `"C"`, set these to 1. | `comm.py` `WSextractData`; Artisan WebSocket docs ("mode") |
| `[WebSocket] id_node` / `machine_node` | `id` / `roasterID` | Artisan adds both to every request. We echo `id`. | `wsport.py:96-97` |
| `[WebSocket] request_timeout` / `connect_timeout` / `reconnect_interval` | `0.5` / `5` / `3` s | Artisan default request timeout; our reply takes a few ms. Croaster's own screenshot uses 0.5 / 4 / 2; either is fine. | `wsport.py:89`; `images/Connect-Artisan-Same-Network.png` |
| `[WebSocket] compression` | `true` | Offers deflate. The links2004 server ignores it, so it does no harm. | `wsport.py:73,286` |
| `[WebSocket] charge_message` / `drop_message` | `startRoasting` / `endRoasting` | Inert. They are messages the *roaster* could push to Artisan, and ours never pushes them. | Artisan WebSocket docs |
| `[ExtraDev] extradevices` | `112, 113` | "+WebSocket 34" (Inputs 3/4) and "+WebSocket 56" (Inputs 5/6). They reuse the readings from the one `getArtisanData`. | `device_registry.py:153-154`; `comm.py:1944` (only mode 0 polls) |
| `[ExtraDev] extraname1/extraname2` | `Heater, SV` / `Fan, Unused` | curve1 = the odd input (3, 5) and curve2 = the even input (4, 6), so WS34 gives Heater (BurnerVal) and Fan (FanVal), and WS56 gives SV. | `comm.py:1973`; `canvas.py:19458` (`extratx, extrat2, extrat1`) |
| `[ExtraDev] extraCurveVisibility2 / extraLCDvisibility*` | WS56 curve 2 hidden; LCDs shown for Heater, Fan, SV | Input 5 is unused | — |
| `[ExtraComm] extra*` | 2 entries each | Each list must be as long as `extradevices`. Artisan pads short lists, but explicit lists match its own Probat example. The serial values are ignored for WebSocket devices. | `main.py:15085`; `includes/Machines/Probat/Sample_Roaster.aset` |
| `[Device] Controlbuttonflag` | `false` | **Fixed (was true).** For a WebSocket device `externalPIDControl()` returns 0, so this button runs **Artisan's software PID** on the PC. That PID can only move a slider; it can't set our firmware's setpoint. Our PID runs on the ESP32 (see below). | `pid_control.py:1294-1304` |
| `[Device] PIDbuttonflag` | `false` | Only means something for the TC4 (device 19) | `pid_control.py:1300` |
| `[ArduinoPID] *` (pidKp, pidOnCHARGE...) | unchanged, unused | Settings for Artisan's software PID, which is now off. Our gains live in the ESP32's NVS and are set by autotune or `setPreferences`. | — |
| `[General] Delay` | `1000` ms | **Fixed (was 2000).** Artisan's default sampling interval. Our firmware reads probes every 250 ms, so 1 s is easy. The same interval feeds the comms watchdog (10 s), giving it 10 samples of margin. | `canvas.py:623-625` |
| `[General] Mode` | `F` | **Changed (was C).** Artisan in °F, like the firmware (`FLEX_TEMP_UNIT`). Artisan sends slider and button temperatures (setpoint) in its own mode, and the firmware reads them in its unit, so **Artisan's mode and the firmware unit must match**. The firmware converts to °C and caps the setpoint at `FLEX_MAX_SETPOINT_C` (250 °C = 482 °F). | `control_protocol.cpp` (`toCelsius`); `config.h` `FLEX_TEMP_UNIT` |
| °F values | Phases `194, 266, 365, 446`, `AUCbase` 212, `dropDuplicatesLimit` 0.54, `RoRlimit`/`RoRlimitm` 63/−128, `svSliderMax` 446, `svValue` 356, axes `ymax` 527 (`ygrid` 100), `zmax` 76 (`zgrid` 10) | **Changed.** The old °C values converted with the formulas Artisan's own °C→°F switch uses. `ymax` 527 °F is the 275 °C over-temp trip. `minLimit`/`maxLimit` (0/700) are Artisan's general sanity range and are not converted. | `main.py` `fahrenheitMode()` |
| `[Sliders] slideractions` | `18, 0, 18, 18` | 18 = "WebSocket Command". Slider 2 (Drum) is unused. | `main.py:8319` (`fireslideraction_internal`) |
| `[Sliders] slidercommands` | `send({{"command": {{"fan": {}}}}})`, —, `...setpoint...`, `...burner...` | Artisan always runs `str.format`, so literal braces are doubled and `{}` becomes the slider value | `main.py:8327-8353` (UI action 18 → internal 20, `cmd.format`); `wsport.py:375` `send` |
| `[Sliders] slidermin/slidermax` | fan 0–100, setpoint 0–482 (°F), heater 0–100 | The firmware enforces fan ≥ 30 % while heating, heater ≤ 90 % and setpoint ≤ 250 °C (482 °F), so the sliders don't have to. Fan can go to 0 when cold (yaeger's slider stopped at 30). | `config.h` `FLEX_MIN_FAN_WITH_HEAT`, `FLEX_HEATER_MAX_PERCENT`, `FLEX_MAX_SETPOINT_C` |
| `[Sliders] eventslidertemp` / `ModeTempSliders` | `0, 0, 1, 0` / `F` | Slider 3 is a temperature (setpoint), and its range is stored in °F. If Artisan's mode changes, Artisan converts the range. | Artisan event slider settings; `main.py` `adjustTempSliders` |
| `[Sliders] slidervisibilities` | `1, 0, 1, 1` | Fan, Setpoint, Heater | — |
| `[events] etypes` | `Fan, Drum, Setpoint, Heater, --` | Names the 4 event types after what the sliders drive | `main.py:15456` |
| `[ExtraEventButtons]` 50–70 %, Off | type 3, values `6…8, 0`, action 22 | 22 = "WebSocket Command". Artisan sends `round(v*10)-10`, so 6 → 50 and 8 → 70. Type 3 logs a Heater event. | `main.py:11356-11368`; `canvas.py:2775` |
| `[ExtraEventButtons]` PID, Manual, Autotune, Stop, Clear trip | type 4 (no event), action 22 | **Added.** They drive the firmware's own modes. Type-4 buttons still run `cmd.format(...)`, so the doubled braces work. | `main.py:11435-11439` |
| `[DefaultButtons]` CHARGE/DROP actions | none | Not automated on purpose. Press **Stop** (heater off, cooldown fan) at DROP. | — |

## Artisan's PID vs our firmware PID (plain English)

There are two different PIDs, and only one is in use.

- **Artisan's software PID** runs on the PC. Every sample it works out a
  heater % and moves a slider, which sends the % to the roaster. If WiFi or the
  PC stalls, the heater stays wherever the last message left it. That is the
  "Control" button, and it is now hidden.
- **Our firmware PID** runs on the ESP32, the way yaeger's did: every 250 ms,
  next to the SSR, with the safety trips. Artisan only sends *what to aim for*:
  the setpoint slider, plus PID, Manual, Autotune and Stop. The ESP32 does the
  control itself and keeps doing it if Artisan closes. If client traffic stops
  for the watchdog timeout (10 s by default) while heating, the comms watchdog
  turns the heater off.

## Using it
1. Help → Load Settings… → `croaster-flex.aset`.
2. Config → Port → WebSocket: set the host to the IP on the OLED if `.local`
   doesn't resolve.
3. Press ON. BT, ET, Heater, Fan and SV should plot.
4. To run in PID: set the **Setpoint** slider, then press **PID**. For Manual,
   press **Manual** and use the Heater slider or buttons.
