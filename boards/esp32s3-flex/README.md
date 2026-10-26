# Croaster — ESP32-S3 Flex board (heater + fan control)

Board `esp32s3-flex` ("flex": named by Shawn, since the whole project is a
roaster). Standalone **PlatformIO** project, like every `boards/<board>/` folder. It is a
Croaster board that also **controls** the roaster: PID heater through an SSR,
AC-dimmer (or PWM) fan, roast profiles, relay autotune, safety trips, and
Artisan control over Croaster's WebSocket. It consumes:

| lib_dep | What |
|:---|:---|
| `Croaster=symlink://../../croaster` | Croaster core: WiFiManager, WebSocket :81, BLE, OTA, RoR, roast timer |
| `CroasterDisplaySSD1306=symlink://../common` | Croaster's shared OLED screen; its header line shows heater %, fan % and mode (or the trip) |
| `ControlCore=symlink://../../control-core` | Pure control logic: PID, autotune, profiles, MAX31855/MAX6675 decode, command protocol (host-tested) |
| `ControlEsp32=symlink://../../control-esp32` | ESP32 side: control task, SSR, dimmer/PWM fan, probes, NVS, `ControlPlugin` |

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

`FLEX_HEATER_NONE` + `FLEX_FAN_NONE` builds a plain Croaster temperature
monitor (no control libs used) with either converter.

---

## 🔧 Build, upload, test

```bash
cd boards/esp32s3-flex
pio run -e esp32s3-flex                    # release (what ../../build_all.sh builds)
pio run -e esp32s3-flex -t upload          # flash over USB
../../upload_monitor.sh esp32s3-flex       # debug build + upload + monitor
pio test -e native                            # host unit tests (control-core)
```

| Env | Platform | Use |
|:---|:---|:---|
| `esp32s3-flex` | `platformio/espressif32 @ 8.0.0` (Arduino 3.3.12 / IDF 5.5) | release |
| `esp32s3-flex-debug` | same | `CORE_DEBUG_LEVEL=3` |
| `esp32s3-flex-pioarduino` | pioarduino 55.03.312 (same Arduino core) | alternative platform |
| `esp32s3-flex-pioarduino-sdkconfig` | pioarduino + `custom_sdkconfig` | rebuilds IDF with `esp32s3-sdkconfig.defaults` (slow first build; not needed: no TLS) |

Serial logs: native USB-C port (`ARDUINO_USB_CDC_ON_BOOT=1`). Using the other
(USB-UART) port? Set `ARDUINO_USB_CDC_ON_BOOT=0` in `platformio.ini`.

---

## 📡 Commands (WebSocket :81 and BLE, Croaster JSON)

Croaster's own commands work unchanged (`getArtisanData`, `getDeviceInfo`,
`tempUnit`, OTA...). The roaster adds (temperatures in Croaster's current unit):

| JSON | Effect |
|:---|:---|
| `{"command":{"burner":0..100}}` | heater %, manual mode |
| `{"command":{"fan":0..100}}` | fan % |
| `{"command":{"setpoint":T}}` | PID setpoint |
| `{"command":{"mode":"manual"\|"pid"}}` | mode |
| `{"command":{"pidInput":"ET"\|"BT"\|"MAX"}}` | PID input (saved) |
| `{"command":{"pid":{"kp":..,"ki":..,"kd":..}}}` | gains (saved) |
| `{"command":{"cooldownFan":0..100}}` | fan after `stop` (saved) |
| `{"command":{"watchdog":{"enabled":bool,"timeout":s}}}` | comms watchdog on/off and timeout, 2..600 s (saved; either field alone works) |
| `{"command":{"profile":[{"duration":s,"setpoint":T,"interpolation":"ease-out","fanValue":%}]}}` | load profile |
| `{"command":"startProfile"}` / `"stop"` / `"autotune"` / `"clearTrip"` / `"getControl"` | actions / query |

yaeger's formats also work (same parser, see `docs/control-parity.md`):

| JSON | Effect |
|:---|:---|
| `{"BurnerVal":v}` `{"FanVal":v}` `{"Setpoint":T}` `{"Mode":"Manual"\|"PID"}` `{"Target":"ET"\|"BT"\|"MAX"}` | as above (no `"command"` key) |
| `{"command":"setBurner","value":v}` / `setFan` | heater / fan % |
| `{"command":"setPreferences","pidKp":..,"pidKi":..,"pidKd":..,"cooldownFanSpeed":..}` | gains + cooldown fan (saved); replies with preferences |
| `{"command":"getPreferences","id":N}` | `{"id":N,"data":{"type":"preferences","pidKp",...,"cooldownFanSpeed"}}` |

Data broadcasts gain `BurnerVal`, `FanVal`, `SV`, `Mode`, `trip`, `Amb`,
`Target`, `pidKp`/`pidKi`/`pidKd`, `wifiStrength`.

The PID runs on the ESP32. Clients (Artisan, ICRM, a browser) only send targets
and commands.

Safety: over-temperature, probe fault, no mains (dimmer sees no zero-cross),
OTA in progress, and comms lost (below) all turn the heater off and latch a
trip until `clearTrip`.

### Comms watchdog

While the heater is on (Manual, PID or Autotune; profile runs are exempt), the
firmware expects a client message at least every timeout. Any message counts,
over WebSocket or BLE, including Artisan's once-a-second `getArtisanData`. If
none arrives, it trips `comms`: heater off, fan to the cooldown speed.

| Setting | Default | Change at build time | Change at runtime (saved in NVS) |
|:---|:---|:---|:---|
| On/off | on | `FLEX_WATCHDOG_ENABLED` (1/0) | `{"command":{"watchdog":{"enabled":false}}}` |
| Timeout | 10 s | `FLEX_WATCHDOG_TIMEOUT_S` (2..600) | `{"command":{"watchdog":{"timeout":20}}}` |

`config.h` values apply until a runtime command saves new ones; after that NVS
wins. `{"command":"getControl"}` shows the current values under `watchdog`.
Turn it off only when something else watches the roast: with it off, a crashed
laptop leaves the heater running until the over-temperature trip.

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
