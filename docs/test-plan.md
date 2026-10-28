# Functional test list — flex control board

A running list of behaviours to build a proper test plan around. One line per
check: what to do, then the expected outcome after the arrow. Add to it as
features and bench sessions happen.

Status tags:
- **host**: covered by a host test (`pio test -e native` in
  `boards/esp32s3-flex`); the test name follows.
- **host (fork)**: covered by a host test in the `MAX6675_Thermocouple` fork
  (`test/host`, run steps in its README); the test name follows.
- **build 2026-10-26**: checked by a clean build on that date.
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
- Manual, heater on, both probes unplugged → `trip:"sensor"` after 2 s, heater 0; heater 0 → no trip; one probe left → no trip (D23). **host** `test_sensor_fault_rules`; bench untested

## MAX6675 library fork (D21)
Stock MAX6675 boards (`esp32s3`, `esp32c3`, `esp8266`) read probes through
`SmoothThermocouple`; neither flex build does (since 2026-10-27 the monitor
build uses `app/MonitorProbes`, D11).
- First reading seeds the average; 0 °C keeps the average. **host (fork)** `testFirstSampleSeedsAverage`, `testZeroKeepsAverage`, `testZeroSeed`
- 100, 100, NAN, 100 → 100, 100, NAN, 100 (recovers). **host (fork)** `testRecoversAfterNan`
- MAX6675 frames decode as before, with a wait before every pin change; 33 us of waits per read. **host (fork)** `testMax6675Decode`, `testMax6675ReadTime`
- MAX6675 board, logic analyser on SCK and CS → CS low for well under 1 ms per probe (was about 33 ms), SCK period 2 us or more, reads 250 ms apart. **untested**
- MAX6675 board at room temperature and in boiling water → same readings as on the old library (about 100 °C / 212 °F in water). **untested**
- MAX6675 board, unplug one probe → that probe `N/A` on the screen and absent from `getArtisanData`, the other unaffected. **untested**
- Plug it back in at a different temperature → the new temperature on the next sample, no slow ramp from the old value (before: `N/A` until reboot). **untested**
- `esp32s3-debug` (stock) links without `SmoothThermocoupleFix.cpp`. **build 2026-10-26** (upstream board files, in a copy pinned to `espressif32 @ 6.13.0`; see T13)
- Flex after the library change: ET at room temperature as before; unplug ET → `N/A`, replug → reading returns. **untested**

## Flex monitor build (`FLEX_HEATER_NONE`, D11)
- Builds with `FLEX_TC_MAX31855` and with `FLEX_TC_MAX6675` (`FLEX_FAN_NONE`). **build 2026-10-27** (temporary config copies)
- MAX31855 monitor: BT and ET at room temperature on the OLED and in `getArtisanData`; boiling water ≈ 100 °C / 212 °F. **untested**
- MAX6675 monitor: same checks on MAX6675 modules. **untested**
- Unplug one probe → that probe `N/A` after the third bad read (under 1 s), the other unaffected; plug back → reading returns. **untested**
- Heater and fan pins stay idle; control commands such as `getControl` get no control reply (the plugin is not built). **untested**

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
- Fresh NVS, no `pidInput` sent → `Target` reads BT (D18). **host** (default only, `test_protocol_basic_and_data`); bench untested
- `{"command":{"pidInput":"ET"}}` → `Target` reads ET and survives a reboot; the BT default doesn't override it. **untested**
- PID input BT with no BT probe → PID mode trips `sensor` after 2 s; autotune refused. **untested**
- Autotune on BT on the real heater → gains that hold a BT setpoint. **untested** (needs BT probe and AC)

## Autotune entry, exit and gates
- Autotune with SV below 212 °F → refused, mode stays Manual. **bench 2026-10-26**
- Autotune with SV at or above 212 °F → mode Tuning, BurnerVal 100. **bench 2026-10-26**
- Autotune with fan below 30 % → fan set to 60 %. **untested**
- Relay identifies a simulated roaster and the gains hold its setpoint. **host** `test_relay_autotune_identifies_and_controls`
- Autotune → PID or Manual → starts from 0 %, not the relay's 100 % (fix in 067e316). **host** `test_leaving_autotune_starts_from_zero`; bench todo
- Autotune → PID or Manual → `autotune` reads "aborted"; Stop → "aborted: stop". **untested**
- Autotune with SV below 212 °F → `autotune` reads "need setpoint >= 212 F" in every sample; OLED shows "AT: SV<212F" for 10 s, again on each retry (D17). **host** (text, `test_autotune_status_text`); bench untested
- Autotune with °C selected → "need setpoint >= 100 C". **host** `test_autotune_status_text`
- Autotune with no probe on the PID input → "need a valid probe", OLED "AT: no probe". **untested**
- Trip during autotune → `autotune` "aborted: <trip>"; OLED keeps the TRIP line. **untested**
- Autotune fails → "failed: <reason>"; OLED "AT fail: ..." within 20 characters. **host** (text); bench untested
- Autotune with no heat (no AC) → fails "timeout" after 30 min, heater 0. **untested**
- Autotune completes on the real heater → new gains saved to NVS, Manual, heater 0, fan 30 %. **untested** (needs AC)
- `{"command":{"pidInput":"ET"}}` during autotune on BT → `autotune` "aborted: input changed", OLED "AT abort: input", heater 0 (D23). **host** (`test_pv_step_stops_autotune_and_resets_pid`, text in `test_autotune_status_text`); bench untested
- `pidInput` change in PID → heater output continues without a jump; the same input again → nothing happens (D23). **host** `test_pv_step_stops_autotune_and_resets_pid`; bench untested
- Autotune on the real heater → note the swing around SV and the time taken; compare with the README (simulated ±10 to ±45 °C, 4 to 14 min). **untested** (needs AC)

## Watchdog and trips
- Artisan Off, then On → `trip:"comms"`, heater 0, fan at the trip speed; stays tripped after reconnect. **bench 2026-10-26** (when it tripped is unconfirmed; it should only trip while heating)
- Artisan Off while heating → trip after the 10 s timeout, not before. **untested**
- Clear trip, or any heater, mode, autotune or profile command → trip clears. **untested**
- Fan or setpoint command while tripped → applied, trip stays. **untested**
- Watchdog timeout and enabled flag parse from JSON. **host** `test_protocol_watchdog`
- `{"command":{"watchdog":{"enabled":false}}}` → no comms trip, survives reboot. **untested**
- Probe above `FLEX_OVER_TEMP_C` (set low to test) → `trip:"overtemp"`, heater 0, fan 100 %. **untested**
- OTA starts → `trip:"ota"`, heater 0. **untested**

## Stop and cooldown
- Stop → heater 0, fan 100 % at once, SV kept, mode Manual. **bench 2026-10-26** (before D19; the stop fan is now 65 %)
- Stop with defaults → fan 65 % (D19). **untested**
- Any trip (comms, sensor, nomains) → fan to 100 % once when it starts; a fan command afterwards still applies (D19). **untested**
- `cooldownFan` as a number or `{"stop","trip"}` object, range 0..100, both in `getControl`. **host** `test_protocol_cooldown_fan`
- `{"command":{"cooldownFan":{"trip":80}}}` then a comms trip → fan 80 %, survives reboot. **untested**
- `{"command":{"cooldownFan":65}}` then Stop → fan 65 %, survives reboot. **untested**

## Mains detection
- `FLEX_REQUIRE_MAINS 1`, no AC → heat refused, `trip:"nomains"`. **bench 2026-10-26** (before the grace period; bench todo: trip now comes about 2 s after heat is set)
- Grace period (`FLEX_MAINS_GRACE_MS` 2000): a dropout under 2 s while heating does not trip; over 2 s trips; heater 0 resets the count. **host** `test_mains_dropout_under_grace_does_not_trip`, `test_mains_absent_past_grace_trips`, `test_mains_heater_off_resets_grace`; bench todo: pull the dimmer's Z-C wire for 1 s while heating (no trip), then for 3 s (trip)
- AC connected → `getControl` `mainsOk` true, heat allowed. **untested**
- `FLEX_REQUIRE_MAINS 1`, AC connected, heater on for an hour → no `nomains` trip; a crossing stored after the control task read the timer counts as recent (D23). **host** `test_zero_cross_age`; bench untested

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

## Correction offsets (D22)
- Correction offset → PID input shifts by it; ET, BT and MAX inputs; `Amb` unchanged. **host** `test_correction_shifts_pid_input`
- `correctionBt` +18 in °F → PID input +10 °C and matches the displayed BT. **host** `test_correction_in_fahrenheit` (the core getter's ÷ 1.8 is checked on the bench only)
- Faulted probe plus an offset → still NaN; no over-temperature trip from it. **host** `test_correction_keeps_nan`
- Negative offset hiding a hot probe → over-temperature trips on the raw reading. **host** `test_overtemp_uses_raw_and_corrected`
- Snapshot BT/ET → uncorrected, so the offset is added once. **host** `test_snapshot_stays_uncorrected`
- °F mode, `{"command":{"correctionBt":10}}`, PID on BT, SV 300 °F → displayed BT settles at 300 °F, not 310 °F. **untested** (needs AC)
- `{"command":{"correctionBt":-50}}` with `FLEX_OVER_TEMP_C` set low → `trip:"overtemp"` when the raw BT passes the limit, while the display still reads 50 °F lower. **untested**
- Correction set in °F, then `{"command":{"tempUnit":"C"}}` → the same number now applies in °C on the display and in PID; PID holds the displayed SV. **untested**
- Correction change during autotune → `autotune` "aborted: correction changed", OLED "AT abort: offset", heater 0. **host** (text, `test_autotune_status_text`); bench untested
- Correction change in PID → heater output continues without a jump. **untested**
- Stock boards (`esp32c3`, `esp8266`) → same RAM and flash use and the same warnings with the getters added. **build 2026-10-26**
