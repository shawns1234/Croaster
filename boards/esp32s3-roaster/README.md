# Croaster — ESP32-S3 Roaster (heater + fan control)

Standalone **PlatformIO** project, like every `boards/<board>/` folder. It is a
Croaster board that also **controls** the roaster: PID heater through an SSR,
AC-dimmer (or PWM) fan, roast profiles, relay autotune, safety trips, and
Artisan control over Croaster's WebSocket. It consumes:

| lib_dep | What |
|:---|:---|
| `Croaster=symlink://../../croaster` | Croaster core: WiFiManager, WebSocket :81, BLE, OTA, RoR, roast timer |
| `CroasterDisplaySSD1306=symlink://../common` | Croaster's stock OLED screen (optional) |
| `RoasterCore=symlink://../../roaster-core` | Pure control logic: PID, autotune, profiles, MAX31855/MAX6675 decode, command protocol (host-tested) |
| `RoasterControl=symlink://../../roaster-control` | ESP32 side: control task, SSR, dimmer/PWM fan, probes, NVS, `RoasterPlugin` |

Design notes and decisions: [`docs/design.md`](../../docs/design.md).

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
| Display | 0.96" 128×64 I2C OLED, SSD1306 (**confirmed**), yellow/blue | address 0x3C (0x3D also probed); top 16 rows are yellow: the IP header sits there |

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
features (BLE, ArduinoOTA), PID defaults, safety limits, control core. It is
force-included into every translation unit (`-include src/config.h`) so it can
also switch Croaster library features (`CROASTER_HAS_BLE`).

Invalid choices fail the build with a message naming the setting:

```
src/ConfigCheck.h: error: static assertion failed: config.h: two ROASTER_PIN_* settings use the same GPIO
src/ConfigCheck.h: error: #error "config.h: ROASTER_REQUIRE_MAINS needs ROASTER_FAN_AC_DIMMER ..."
```

`ROASTER_HEATER_NONE` + `ROASTER_FAN_NONE` builds a plain Croaster temperature
monitor (no control libs used) with either converter.

---

## 🔧 Build, upload, test

```bash
cd boards/esp32s3-roaster
pio run -e esp32s3-roaster                    # release (what ../../build_all.sh builds)
pio run -e esp32s3-roaster -t upload          # flash over USB
../../upload_monitor.sh esp32s3-roaster       # debug build + upload + monitor
pio run -e esp32s3-roaster-ota -t upload      # after the first flash: espota to croaster-roaster.local
pio test -e native                            # host unit tests (roaster-core)
```

| Env | Platform | Use |
|:---|:---|:---|
| `esp32s3-roaster` | `platformio/espressif32 @ 8.0.0` (Arduino 3.3.12 / IDF 5.5) | release |
| `esp32s3-roaster-debug` | same | `CORE_DEBUG_LEVEL=3` |
| `esp32s3-roaster-ota` | same | espota upload |
| `esp32s3-roaster-pioarduino` | pioarduino 55.03.312 (same Arduino core) | alternative platform |
| `esp32s3-roaster-pioarduino-sdkconfig` | pioarduino + `custom_sdkconfig` | rebuilds IDF with `esp32s3-sdkconfig.defaults` (slow first build; not needed: no TLS) |

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
| `{"command":{"profile":[{"duration":s,"setpoint":T,"interpolation":"ease-out","fanValue":%}]}}` | load profile |
| `{"command":"startProfile"}` / `"stop"` / `"autotune"` / `"clearTrip"` / `"getRoaster"` | actions / query |
| `{"command":"getData","id":N}` | same as `getArtisanData`, and counts as client activity for the comms watchdog |

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
OTA in progress, and **comms lost** (heating, not running a profile, and no
client traffic for `ROASTER_COMMS_TIMEOUT_S`, default 10 s; also sets the
cooldown fan) all turn the heater off and latch a trip until `clearTrip`.

## ☕ Artisan

Load `artisan/croaster-roaster.aset` (Help → Load Settings). Host
`croaster-roaster.local` (or the IP on the OLED), port 81, path `WebSocket`,
one `getData` per second. Curves: BT, ET, Heater, Fan, SV. Sliders: Fan,
Setpoint, Heater. Buttons: heater 50–70 %/Off, PID, Manual, Autotune, Stop,
Clear trip. Artisan's own PID ("Control") is off, because ours runs on the
ESP32. Every setting and its source: [`docs/artisan-settings.md`](../../docs/artisan-settings.md).

## ⚠️ Check before first power-up
- IPEX antenna attached (transmitting without it can damage the RF stage).
- ET probe isolation: MAX31855 reports a short-to-GND/VCC fault on grounded
  probes or a probe touching the chassis. Ungrounded probes only.
- Dimmer VCC on 3V3, mains 60 Hz (`ROASTER_MAINS_HZ`), fan motor suits triac
  phase control (universal / shaded-pole; not capacitor-run or EC fans).
