# Croaster — ESP32-S3 Flex board (heater + fan control)

Board `esp32s3-flex` ("flex": named by Shawn, since the whole project is a
roaster). Standalone **PlatformIO** project, like every `boards/<board>/` folder. It is a
Croaster board that also **controls** the roaster: PID heater through an SSR,
AC-dimmer (or PWM) fan, roast profiles, relay autotune, safety trips, and
Artisan control over Croaster's WebSocket. It consumes:

| lib_dep | What |
|:---|:---|
| `Croaster=symlink://../../croaster` | Croaster core: WiFiManager, WebSocket :81, BLE, OTA, RoR, roast timer |
| `CroasterDisplaySSD1306=symlink://../common` | Croaster's shared OLED screen; its header line shows heater %, fan % and mode (or the trip, or an autotune status for 10 s) |
| `CroasterControl=symlink://../../croaster-control` | Control add-on. `src/core`: PID, autotune, profiles, MAX31855/MAX6675 decode, command protocol (pure, host-tested). `src/hw`: probe SPI reads, SSR, dimmer/PWM fan. `src/app`: control task, NVS, `ControlPlugin` |

Design notes and decisions: [`docs/design.md`](../../docs/design.md).
Whether this ini should share chip-level settings with `boards/esp32s3` (standalone,
`extends`, or a shared base ini) is open decision D10 there; today it is standalone.

---

## 📋 Hardware (defaults in `src/config.h`)

| Part | Item | Notes |
|:---|:---|:---|
| MCU | Lonely Binary ESP32-S3 **Gold Edition N16R8**, IPEX | 16 MB QIO flash, 8 MB **octal** PSRAM (`qio_opi`). **Attach the IPEX antenna before powering up.** |
| ET / BT | 2 × MAX31855 breakout | shared SCK/MISO, one CS each, 3V3 |
| BT probe | PerfectPrime TL1918SL (K, 3.2 × 300 mm, ungrounded) | ungrounded = right for MAX31855 |
| ET probe | K probe from the Inkbird kit | **check grounded vs ungrounded** (see below) |
| Heater | SSR-40DA from the Inkbird PID kit (kit PID unused) | input 3–32 VDC, on > 2.4 V: 3.3 V GPIO drives it directly |
| Fan | RobotDyn 1-ch AC dimmer (BTA16 triac, Z-C output) | VCC from **3V3** so Z-C swings 0–3.3 V |
| Display | 0.96" 128×64 I2C OLED, SSD1306 (**confirmed**), yellow/blue | address 0x3C; top 16 rows are yellow: the IP header sits there |

All modules run from the board's 3V3 pin (AMS1117, ~500 mA budget: OLED ~20 mA,
2 × MAX31855 < 3 mA, dimmer logic < 10 mA, SSR input ~7.5 mA).

## 🔌 Wiring (default pins)

| Signal | GPIO | Notes |
|:---|:---:|:---|
| Thermocouple SCK | 12 | HSPI SCK |
| Thermocouple MISO (DO) | 13 | HSPI MISO |
| ET CS | 10 | |
| BT CS | 11 | |
| SSR + | 4 | SSR − to GND |
| Dimmer PSM/DIM | 5 | gate; must be < 32 |
| Dimmer Z-C | 6 | |
| OLED SDA | 8 | Arduino default I2C |
| OLED SCL | 9 | |
| Status LED | 48 | on-board WS2812 (`LED_BUILTIN`) |

Never use: 0/3/45/46 (strapping), 19/20 (native USB), 26–34 (flash, not on the
header), **35–37 (octal PSRAM — they are on the header)**, 43/44 (UART0 → USB-UART
port), 48 (RGB LED). `src/ConfigCheck.h` rejects these at compile time.

Yaeger's original devkit map (heater 3, fan 8, Z-C 7, TC 5/6/15/16, I2C 41/42)
also passes the check except heater on GPIO3 (strapping): move it if you reuse
that wiring.

---

## ⚙️ Configure: `src/config.h`

The one file to edit: thermocouple chip, heater, fan driver, display, pins,
features (BLE), PID defaults, safety limits, control core. It is
force-included into every translation unit (`-include src/config.h`) so it can
also switch Croaster library features (`CROASTER_HAS_BLE`).

Invalid choices fail the build with a message naming the setting:

```
src/ConfigCheck.h: error: static assertion failed: config.h: two FLEX_PIN_* settings use the same GPIO
src/ConfigCheck.h: error: #error "config.h: FLEX_REQUIRE_MAINS needs FLEX_FAN_AC_DIMMER ..."
```

`FLEX_HEATER_NONE` + `FLEX_FAN_NONE` builds a temperature monitor with either
converter: Croaster's screen, RoR, WebSocket and BLE, with no heater, fan or
control task. The probes are read by the same code as the control build
(`croaster-control` `hw/thermocouples` and `app/MonitorProbes`).

---

## 🔧 Build, upload, test

```bash
cd boards/esp32s3-flex
pio run -e esp32s3-flex                    # release (what ../../build_all.sh builds)
pio run -e esp32s3-flex -t upload          # flash over USB
../../upload_monitor.sh esp32s3-flex       # debug build + upload + monitor
pio test -e native                            # host unit tests (croaster-control/src/core)
```

| Env | Platform | Use |
|:---|:---|:---|
| `esp32s3-flex` | `platformio/espressif32 @ 8.0.0` (Arduino 3.3.12 / IDF 5.5) | release |
| `esp32s3-flex-debug` | same | `CORE_DEBUG_LEVEL=3` |
| `esp32s3-flex-pioarduino` | pioarduino 55.03.312 (same Arduino core) | alternative platform |

Every env sets `board_build.sdkconfig_path = ../esp32s3/esp32s3-sdkconfig.defaults`,
Croaster's own file in `boards/esp32s3` (flex keeps no copy). Arduino builds on these platforms compile
against prebuilt IDF libraries, so the file documents the intended Kconfig
rather than changing the build (see `docs/design.md`, "Build platform and
sdkconfig").

Serial logs: native USB-C port (`ARDUINO_USB_CDC_ON_BOOT=1`). Using the other
(USB-UART) port? Set `ARDUINO_USB_CDC_ON_BOOT=0` in `platformio.ini`.

---

## 📡 Commands (WebSocket :81 and BLE, Croaster JSON)

Croaster's own commands work unchanged (`getArtisanData`, `getDeviceInfo`,
`tempUnit`, OTA...). Temperatures are in Croaster's current unit: °F at boot
(`FLEX_TEMP_UNIT` in `config.h`); `{"command":{"tempUnit":"C"}}` switches until
the next reboot. Limits and the PID still work in °C. The flex board adds:

| JSON | Effect |
|:---|:---|
| `{"command":{"burner":0..100}}` | heater %, manual mode |
| `{"command":{"fan":0..100}}` | fan % |
| `{"command":{"setpoint":T}}` | PID setpoint |
| `{"command":{"mode":"manual"\|"pid"}}` | mode |
| `{"command":{"pidInput":"ET"\|"BT"\|"MAX"}}` | PID input (saved; default BT, `FLEX_PID_INPUT`; a saved value wins over the default) |
| `{"command":{"pid":{"kp":..,"ki":..,"kd":..}}}` | gains (saved) |
| `{"command":{"cooldownFan":{"stop":0..100,"trip":0..100}}}` | fan after `stop` (default 65) and when any safety trip starts (default 100); either field alone works; a plain number sets `stop` (saved) |
| `{"command":{"watchdog":{"enabled":bool,"timeout":s}}}` | comms watchdog on/off and timeout, 2..600 s (saved; either field alone works) |
| `{"command":{"profile":[{"duration":s,"setpoint":T,"interpolation":"ease-out","fanValue":%}]}}` | load profile |
| `{"command":"startProfile"}` / `"stop"` / `"autotune"` / `"clearTrip"` / `"getControl"` | actions / query |

yaeger's named commands also work (same parser, see `docs/control-parity.md`).
yaeger's flat keys (`{"BurnerVal":v}` without `"command"`) are not accepted.

| JSON | Effect |
|:---|:---|
| `{"command":"setBurner","value":v}` / `setFan` | heater / fan % |
| `{"command":"setPreferences","pidKp":..,"pidKi":..,"pidKd":..,"cooldownFanSpeed":..}` | gains + cooldown fan (saved); replies with preferences |
| `{"command":"getPreferences","id":N}` | `{"id":N,"data":{"type":"preferences","pidKp",...,"cooldownFanSpeed"}}` |

Data broadcasts gain `BurnerVal`, `FanVal`, `SV`, `Mode`, `trip`, `autotune`,
`Amb`, `Target`, `pidKp`/`pidKi`/`pidKd`, `wifiStrength`. `autotune` holds the
last autotune status in the display unit: `idle`, `running`, `done`,
`failed: timeout`, `aborted: stop`, `need setpoint >= 212 F`,
`need a valid probe`. For 10 s after it changes, the OLED header shows a short
form instead ("AT running", "AT: SV<212F").

The PID runs on the ESP32. Clients (Artisan, ICRM, a browser) only send targets
and commands.

Safety: over-temperature, probe fault, no mains (with `FLEX_REQUIRE_MAINS 1`,
the dimmer sees no zero-cross for `FLEX_MAINS_GRACE_MS`, 2 s, while heating),
OTA in progress, and comms lost (below) all turn the heater off and latch a
trip. `clearTrip`, or any heater, mode, autotune or profile command, clears it;
the firmware trips again at once if the cause is still there. Fan and setpoint
changes don't clear a trip.

### Comms watchdog

While the heater is on (Manual, PID or Autotune; profile runs are exempt), the
firmware expects a client message at least every timeout. Any message counts,
over WebSocket or BLE, including Artisan's once-a-second `getArtisanData`. If
none arrives, it trips `comms`: heater off, fan to the trip speed (100 % by
default, `cooldownFan.trip`).

| Setting | Default | Change at build time | Change at runtime (saved in NVS) |
|:---|:---|:---|:---|
| On/off | on | `FLEX_WATCHDOG_ENABLED` (1/0) | `{"command":{"watchdog":{"enabled":false}}}` |
| Timeout | 10 s | `FLEX_WATCHDOG_TIMEOUT_S` (2..600) | `{"command":{"watchdog":{"timeout":20}}}` |

`config.h` values apply until a runtime command saves new ones; after that NVS
wins. `{"command":"getControl"}` shows the current values under `watchdog`.
Turn it off only when something else watches the roast: with it off, a crashed
laptop leaves the heater running until the over-temperature trip.

### Autotune

Autotune measures the roaster and picks PID gains for it. It replaces the
default gains (`FLEX_PID_KP/KI/KD`, yaeger's 1 / 0.1 / 0.01, never tuned for
this hardware). Run it once the heater, fan and the PID input probe (BT by
default) are installed, and again after changing any of them. Run it empty, or
with the load you roast with, at the fan speed you roast with.

How it works: the firmware switches the heater between 100 % and 0 % around
the setpoint (a relay test). Below SV − 1 °C the heater is on; above
SV + 1 °C it is off. The temperature then swings around SV at the roaster's
natural period. From the swing and the period it computes the ultimate gain
Ku and period Pu, then gains by the Tyreus–Luyben rule. Tyreus–Luyben is
gentler than Ziegler–Nichols and suits slow, lagging processes like BT.
Background and sources: [`docs/research/autotune.md`](../../docs/research/autotune.md).

**Start**
1. Set the fan to your roasting speed. Below 30 % the firmware raises it to
   60 %.
2. Set SV to the temperature to tune at, at least 212 °F (100 °C). Pick one
   in your roasting range, and leave room for the swing (below).
3. Send `{"command":"autotune"}`, or press Artisan's **Autotune** button.
   Keep Artisan sampling (ON): the comms watchdog stays active during
   autotune.

It refuses to start, and says why, below 212 °F (`need setpoint >= 212 F`,
OLED `AT: SV<212F`) or without a reading on the PID input probe
(`need a valid probe`, OLED `AT: no probe`).

**While it runs**
- `Mode` reads `Tuning` and `autotune` reads `running`. The OLED header shows
  `AT running` for 10 s, then `H100 Fan60 Tuning` (H follows the relay
  between 100 and 0).
- Don't touch the controls: any of them ends it (below).
- Duration: heat-up to SV, one discarded cycle, then 4 measured cycles. On a
  simulated roaster that took 4 to 14 minutes, depending on its lag (not yet
  measured on this hardware). The limit is 30 minutes.
- The temperature overshoots SV on every cycle, by about ±10 °C (18 °F) on
  a fast roaster and ±45 °C (80 °F) or more on a slow one (same simulation).
  Don't tune with beans you care about.

**Stop**: `{"command":"stop"}` (Artisan **Stop**) ends it with the heater off
and the stop cooldown fan. Any other control change also ends it with the
heater off, since each would spoil the measurement: switching mode (`mode`,
`startProfile`), or changing SV, `pidInput`, a correction offset, the fan
(`aborted: fan changed`, OLED `AT abort: fan`) or the heater (any heater
command or Artisan heater button, `aborted: heater changed`, OLED
`AT abort: heater`). The heater command's value is not applied; a fan command
is. Sending the fan speed it already runs at changes nothing, and the start
rule (below 30 % → 60 %) doesn't count as a change.

**Safety**
- Fails with the heater off on: the PID input temperature above 527 °F
  (275 °C, `FLEX_OVER_TEMP_C`, on the corrected reading), the probe fault
  (three bad reads in a row), no result after 30 minutes, and a swing too
  small to measure.
- Every trip ends it as well (over-temperature on any probe, raw or corrected;
  sensor; comms; no mains; OTA). The status names the trip.
- Nothing is saved unless it completes. Sending `autotune` again while it
  runs starts over.

**Result**
- Done: `autotune` reads `done`, the OLED shows `AT done`. The new gains
  are live and saved in NVS (`kp`/`ki`/`kd`, same as the `pid` command), and
  `getControl` and `pidKp`/`pidKi`/`pidKd` show them. The firmware returns to
  Manual with the heater at 0 % and the fan at 30 %. Select PID to use them;
  it does not resume on its own.
- Failed or aborted: `failed: <reason>` or `aborted: <reason>` (OLED
  `AT fail: ...`, `AT abort: ...`), Manual, heater 0 %. The old gains stay.
- The gains are in heater % per °C (`kp`), per °C·s (`ki`) and %·s per °C
  (`kd`). To go back, send the old values with
  `{"command":{"pid":{"kp":..,"ki":..,"kd":..}}}`.

**Credit**
- Method: K. J. Åström and T. Hägglund, relay feedback autotuning (Automatica,
  1984). Tuning rule: B. D. Tyreus and W. L. Luyben (Ind. Eng. Chem. Res., 1992).
- The command, the fan rule (below 30 % → 60 %) and the fan at 30 % with the
  heater off when done follow [yaeger](https://github.com/tadelv/yaeger) by
  Vid Tadel, whose
  PID and autotune were added by RobTS in
  [PR #21](https://github.com/tadelv/yaeger/pull/21) on top of
  [lily-osp/AutoTunePID](https://github.com/lily-osp/AutoTunePID). That
  autotune toggles the heater every 2 s regardless of temperature, so its
  gains don't depend on the roaster.
- The relay test here (`croaster-control/src/core/relay_autotune.cpp`) is new
  code written for this project, not taken from yaeger or AutoTunePID. So
  are the 212 °F gate, the aborts and the status text. It follows the
  published method; [br3ttb's Arduino PID AutoTune
  library](https://github.com/br3ttb/Arduino-PID-AutoTune-Library) and
  Marlin's [`M303`](https://marlinfw.org/docs/gcode/M303.html) are the
  reference implementations it was checked against.

## ☕ Artisan

Load `artisan/croaster-flex.aset` (Help → Load Settings). Host
`192.168.4.1` on Croaster's access point (or the IP on the OLED), port 81, path `WebSocket`,
one `getArtisanData` per second. Curves: BT, ET, Heater, Fan, SV. Sliders: Fan,
Setpoint, Heater. Buttons: heater 50–70 %/Off, PID, Manual, Autotune, Stop,
Clear trip. Artisan's own PID ("Control") is off, because ours runs on the
ESP32. Every setting and its source: [`docs/artisan-settings.md`](../../docs/artisan-settings.md).

## ⚠️ Check before first power-up
- IPEX antenna attached (transmitting without it can damage the RF stage).
- ET probe isolation: MAX31855 reports a short-to-GND/VCC fault on grounded
  probes or a probe touching the chassis. Ungrounded probes only.
- Dimmer VCC on 3V3, mains 60 Hz (`FLEX_MAINS_HZ`), fan motor suits triac
  phase control (universal / shaded-pole; not capacitor-run or EC fans).
