# Functional test list — flex control board

A running list of behaviours to build a proper test plan around. One line per
check: what to do, then the expected outcome after the arrow. Add to it as
features and bench sessions happen.

Status tags:
- **host**: covered by a host test (`pio test -e native` in
  `boards/esp32s3-flex`); the test name follows.
- **bench 2026-10-26**: seen on the bench on that date (ET probe, OLED and SSR
  DC side connected; no BT probe, no AC, `FLEX_REQUIRE_MAINS 0` except where
  noted).
- **untested**: not checked yet. "Bench todo" marks items queued for the next
  session.

## Probes and ambient
- ET probe at room temperature → ET about 74 °F in telemetry and on the OLED. **bench 2026-10-26**
- MAX31855 frame decode (temperature, cold junction, sign) → correct °C values. **host** `test_max31855_decode`
- Open, short-to-GND and short-to-VCC faults → reading rejected, cold junction still valid. **host** `test_max31855_fault_bits`
- MAX6675 frame decode and open-probe flag → correct °C or rejected. **host** `test_max6675_decode`
- A fault on one probe → the other probe keeps reporting. **host** `test_probe_channels_independent_faults`
- Moving average → seeded with the first sample, no ramp from 0. **host** `test_moving_average_seeds_with_first_sample`
- No BT chip fitted → `Amb` is the ET chip's board temperature, not 32 °F. **host** `test_board_temperature_skips_missing_chip`; bench todo
- BT chip fitted without a probe → `Amb` from the BT chip, BT absent from telemetry. **host** `test_board_temperature_skips_missing_chip`; bench untested
- Unplug the PID input probe in PID mode → `trip:"sensor"` after 2 s, heater 0. **untested**

## OLED
- Temperatures shown in °F. **bench 2026-10-26**
- Mode changes (Manual, PID, Tuning) show in the header. **bench 2026-10-26**
- Header reads "H<n> Fan<n> <mode>", or "TRIP <cause> Fan<n>" when tripped. **untested**

## Artisan mapping
- Input 1 = BT, Input 2 = ET on the curves and LCDs. **untested** (no BT probe yet)
- Heater and Fan LCDs match the OLED's H and Fan values (fix in 067e316). **bench todo**
- SV LCD shows the firmware SV in °F (fix in 067e316). **bench todo**
- Fan, Setpoint and Heater sliders send `fan`, `setpoint` and `burner` commands. **bench 2026-10-26**
- Event buttons (heater %, PID, Manual, Autotune, Stop, Clear trip) send their commands. **bench 2026-10-26**
- yaeger's `setBurner`/`setFan` → heater/fan change; flat `{"BurnerVal":60}` → ignored (D8). **host** `test_protocol_yaeger_compat`; bench untested

## Manual
- Heater slider → BurnerVal follows; SSR duty is BurnerVal × 90 %. **bench 2026-10-26** (BurnerVal only; duty unmeasured)
- SV change in Manual → no effect on heater or fan. **bench 2026-10-26**
- Heater > 0 with fan below 30 % → fan raised to 30 %. **untested**

## PID
- SV below ET in PID → heater output decays toward 0 (integral unwinding). **bench 2026-10-26**
- Integral doesn't wind up while saturated; output leaves 100 % soon after PV passes SV. **host** `test_pid_integrator_does_not_wind_up`
- Manual → PID keeps the current heater output (bumpless). **host** `test_pid_bumpless_reset`
- PID holds SV on the real heater without sustained oscillation. **untested** (needs AC)
- `{"command":{"pidInput":"BT"}}` → `Target` reads BT and survives a reboot. **untested**

## Autotune entry, exit and gates
- Autotune with SV below 212 °F → refused, mode stays Manual. **bench 2026-10-26**
- Autotune with SV at or above 212 °F → mode Tuning, BurnerVal 100. **bench 2026-10-26**
- Autotune with fan below 30 % → fan set to 60 %. **untested**
- Relay identifies a simulated roaster and the gains hold its setpoint. **host** `test_relay_autotune_identifies_and_controls`
- Autotune → PID or Manual → starts from 0 %, not the relay's 100 % (fix in 067e316). **host** `test_leaving_autotune_starts_from_zero`; bench todo
- Autotune → PID or Manual → `getControl` `autotune` reads "aborted". **untested**
- Autotune with no heat (no AC) → fails "timeout" after 30 min, heater 0. **untested**
- Autotune completes on the real heater → new gains saved to NVS, Manual, heater 0, fan 30 %. **untested** (needs AC)

## Watchdog and trips
- Artisan Off, then On → `trip:"comms"`, heater 0, fan at cooldown; stays tripped after reconnect. **bench 2026-10-26** (when it tripped is unconfirmed; it should only trip while heating)
- Artisan Off while heating → trip after the 10 s timeout, not before. **untested**
- Clear trip, or any heater, mode, autotune or profile command → trip clears. **untested**
- Fan or setpoint command while tripped → applied, trip stays. **untested**
- Watchdog timeout and enabled flag parse from JSON. **host** `test_protocol_watchdog`
- `{"command":{"watchdog":{"enabled":false}}}` → no comms trip, survives reboot. **untested**
- Probe above `FLEX_OVER_TEMP_C` (set low to test) → `trip:"overtemp"`, heater 0, fan 100 %. **untested**
- OTA starts → `trip:"ota"`, heater 0. **untested**

## Stop and cooldown
- Stop → heater 0, fan 100 % at once, SV kept, mode Manual. **bench 2026-10-26**
- `{"command":{"cooldownFan":65}}` then Stop → fan 65 %, survives reboot. **untested**

## Mains detection
- `FLEX_REQUIRE_MAINS 1`, no AC → heat refused, `trip:"nomains"`. **bench 2026-10-26**
- AC connected → `getControl` `mainsOk` true, heat allowed. **untested**

## °F conversion
- °F/°C/K conversions both ways. **host** `test_unit_conversion`
- Nested setpoints in °F convert to °C; SV written back in °F. **host** `test_protocol_values`, `test_protocol_yaeger_compat`, `test_protocol_basic_and_data`
- Profile steps in °F convert to °C. **host** `test_profile_in_fahrenheit`
- Artisan setpoint 383 °F → SV 383 in telemetry and Artisan's SV LCD. **host** (conversion only, `test_unit_conversion`); bench todo
- `{"command":{"tempUnit":"C"}}` → OLED and telemetry in °C until reboot. **untested**

## Setpoint caps
- Setpoint above 482 °F (250 °C) → SV capped at 482 °F. **untested** (cap is in the controller, not host-tested)
- Negative setpoint → SV 0 °C (32 °F). **untested**

## Profiles
- README example profile → setpoints follow the ramps. **host** `test_profile_matches_readme_example`
- Profile load and `startProfile` over WebSocket → mode Profile, SV follows, watchdog exempt. **untested**
