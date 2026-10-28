# Relay autotune: how it works, and how ours compares

Researched 2026-10-26 for the flex board's `{"command":"autotune"}`. Code:
`croaster-control/src/core/relay_autotune.{h,cpp}`. Simulation numbers are
from a first-order-plus-dead-time (FOPDT) plant like the one in the host test
`test_relay_autotune_identifies_and_controls`, run on the host the same day.

## The method (Åström–Hägglund relay feedback)
- Swap the PID for an on/off relay around the setpoint: heater high below
  SP − h, low above SP + h. Most processes then settle into a steady
  oscillation at their ultimate period Pu, where the phase lag is 180°.
  Source: Åström & Hägglund, "Automatic tuning of simple regulators with
  specifications on phase and amplitude margins", *Automatica* 20(5):645–651,
  1984, doi:10.1016/0005-1098(84)90014-1.
- The describing function of a relay of amplitude d gives the ultimate gain
  from the PV oscillation amplitude a: **Ku = 4d / (πa)**. With hysteresis ε,
  the relay's −1/N(a) has magnitude πa/(4d) and phase −π + arcsin(ε/a).
  Ku = 4d/(πa) is the magnitude. Using only the real-axis part gives
  4d/(π√(a² − ε²)). Source: VŠB Ostrava, "Relay feedback identification",
  transactions.fs.vsb.cz/2005-2/155.pdf.
- Noise: the hysteresis band 2ε should be wider than twice the noise
  amplitude, and the oscillation at least about 3× the noise, or the relay
  chatters (same source; br3ttb's `noiseBand` has the same purpose).
- One test gives one point (Ku, Pu). Gains then come from a tuning rule.

## Tuning rules (from Ku, Pu)
| Rule | Kp | Ti | Td | Character |
|:---|:---|:---|:---|:---|
| Ziegler–Nichols classic PID (Trans. ASME 64:759–768, 1942) | 0.6 Ku | Pu/2 | Pu/8 | quarter-decay, aggressive, overshoots |
| ZN "no overshoot" (as Marlin's bed/chamber) | 0.2 Ku | Pu/2 | Pu/3 | soft |
| Tyreus–Luyben PI (Ind. Eng. Chem. Res. 31(11):2625–2628, 1992, doi:10.1021/ie00011a029) | Ku/3.22 | 2.2 Pu | — | for slow, lag-dominated processes |
| Tyreus–Luyben PID (same) | Ku/2.2 | 2.2 Pu | Pu/6.3 | gentler than ZN, less overshoot |

Parallel form used here: Ki = Kp/Ti, Kd = Kp·Td.

## Established implementations
| Implementation | Method | Ku | Pu | Checks |
|:---|:---|:---|:---|:---|
| br3ttb PID_AutoTune_v0 ([repo](https://github.com/br3ttb/Arduino-PID-AutoTune-Library)) | relay ±`oStep` (default 30) around the current output, `noiseBand` 0.5 | 4·(2·oStep)/(π·(max−min)) | time between peaks | the last 3 peaks within 5 % of the swing (convergence); ZN PI (0.4 Ku) or PID (0.6 Ku) |
| Marlin `M303` ([temperature.cpp](https://github.com/MarlinFirmware/Marlin/blob/bugfix-2.1.x/Marlin/src/module/temperature.cpp), checked at 5a946ab) | relay at the target with a moving bias: `bias += d·(t_high − t_low)/(t_high + t_low)` evens out the heat/cool halves | 4d/(π·(max−min)/2) | t_high + t_low | ≥ 3 cycles, overshoot limit (`MAX_OVERSHOOT_PID_AUTOTUNE` 30 °C), 20 min cycle limit; ZN classic for hotends, 0.2 Ku / Tu/3 for beds |
| sTune ([repo](https://github.com/Dlloydev/sTune)) | open-loop step test, inflection point of the S-curve | — (fits a FOPDT model) | — | not a relay test |
| Artisan ([repo](https://github.com/artisan-roaster-scope/artisan), checked at master 5be39f6) | its software PID (`pid.py`) has **no autotune** | | | The Autotune buttons drive the Fuji PXR/PXG controller's own autotune over Modbus (`pid_dialogs.py`) |

## Pitfalls for a roaster
- **Lag-dominated, slow.** BT sits in the bean mass and lags the heater by tens
  of seconds; ZN on BT overshoots. TL (or a PI rule) is the usual choice.
- **0/100 % relay = big swings.** On the simulated plant (gain 2.6 °C/%,
  τ 90 s) the swing around SP was about ±10 °C with 6 s dead time and ±47 °C
  with 40 s. A smaller step around an operating duty (br3ttb's `oStep`,
  Marlin's bias) shrinks it.
- **Asymmetric heating and cooling.** The relay is centred on 50 % duty, but
  holding SP may need 30 % or 70 %. The halves then differ in length. The
  describing-function estimate degrades, and the mean PV drifts off SP.
  Marlin corrects this with its bias.
- **Heat soak.** Chamber walls and drum keep warming for minutes, so early
  cycles drift. A convergence check (br3ttb) or skipping early cycles
  (Marlin, ours) handles this.
- **Plant must not change mid-test.** Fan speed, beans and setpoint all change
  the process. Tune at the fan and load you will roast with.
- **Describing-function error.** For FOPDT plants the relay estimate of Ku is
  typically low. In our simulation it came out 17–28 % under the analytic
  Ku, with Pu within 1–16 %. Low Ku means softer gains, the safe direction.

## yaeger's autotune (what it does)
- yaeger is by Vid Tadel ([tadelv/yaeger](https://github.com/tadelv/yaeger), no
  licence file). PID and autotune came in [PR #21](https://github.com/tadelv/yaeger/pull/21)
  by RobTS (merged 2026-04-23, commit c6f4ca7), built on WIP PR #18.
- It vendors [lily-osp/AutoTunePID](https://github.com/lily-osp/AutoTunePID)
  (MIT). The PR says autotune "exists purely for research purposes" and that
  the library's "data pulsing approach is not particularly good".
- The vendored `performAutoTune` flips the output every 2 s whatever the PV
  does, and takes the oscillation amplitude equal to the relay amplitude. So
  Ku = 4/π and Tu ≈ 2 s on every plant: the gains don't depend on the
  roaster (host test `test_legacy_autotune_math_is_plant_independent`).
- lily-osp later replaced it upstream (PR #6, 2026-05-09) with a real relay
  with hysteresis and peak/valley tracking. yaeger still vendors the old copy.

## Ours (`RelayAutotune`)
A new Åström–Hägglund relay written for this project (first in commit
22d53fc on shawns1234/yaeger, then moved here). It is not yaeger's or
lily-osp's code.
- Relay 100/0 % (heater request; physical duty × `FLEX_HEATER_MAX_PERCENT`),
  hysteresis 1 °C, sample 0.25 s.
- Skips the heat-up and the first full cycle, then averages 4 cycles:
  amplitude (max − min)/2 and period between low switches.
- Ku = 4d/(π√(a² − h²)), the real-axis form above (≈15 % above 4d/(πa) at
  a = 2h, < 2 % once a ≥ 5h). Tyreus–Luyben PID gains.
- Fails on: probe NaN (after the 3-fault filter), PV > 275 °C (corrected),
  30 min timeout, swing ≤ 1.05 h. The controller also aborts it on stop, mode,
  setpoint, PID input or correction change, and on every trip.
- No convergence check and no bias correction (see design.md proposals).
