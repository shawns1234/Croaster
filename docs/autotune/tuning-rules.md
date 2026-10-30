# Method and tuning rules

Why the autotune works the way it does, with sources and the simulation
numbers behind the defaults. Earlier research (single-point relay, 2026-10-26)
is in `docs/research/autotune.md`.

## Relay feedback

Åström and Hägglund's relay test replaces the PID with an on/off switch
around the target. Most thermal processes then settle into a steady cycle at
their ultimate period Tu, the frequency where the loop's phase lag is 180°.
With a relay of amplitude d and hysteresis h, and a measured swing a (half
peak to peak), the describing function gives the ultimate gain

    Ku = 4d / (π √(a² − h²))

Sources: K. J. Åström, T. Hägglund, "Automatic tuning of simple regulators
with specifications on phase and amplitude margins", *Automatica* 20(5),
1984, 645–651; C. C. Hang, K. J. Åström, Q. G. Wang, "Relay feedback
auto-tuning of process controllers — a tutorial review", *J. Process Control*
12, 2002, 143–162.

The describing function assumes a sinusoidal swing. A roaster's swing is
closer to a triangle, and on a lag-dominant plant the formula reads Ku low:
the host test `test_at_fopdt_ultimate_point` (gain 3 °C/%, τ 240 s, dead time 30 s)
reads it 23 % below the analytic value. A low Ku gives lower gains, so the
error is on the cautious side; the test asserts that it stays below the true
value and within 30 %.

### What 3D-printer firmware does

| Tool | Test | Points | Gains |
|---|---|---|---|
| Marlin `M303` ([temperature.cpp](https://github.com/MarlinFirmware/Marlin/blob/bugfix-2.1.x/Marlin/src/module/temperature.cpp)) | relay at the user's target `S`; bias corrected each cycle by `bias += d·(t_high − t_low)/(t_high + t_low)`; `C` cycles (default 5); stops above target + 30 °C | one | Ziegler–Nichols PID (hotend) |
| Klipper `PID_CALIBRATE` ([pid_calibrate.py](https://github.com/Klipper3d/klipper/blob/master/klippy/extras/pid_calibrate.py)) | full power below `TARGET`, off above, 2.5 °C hysteresis; 12 peaks; median cycle | one | Ziegler–Nichols PID |

Both tune at one temperature and leave other temperatures to the user. The
roaster autotune runs the same relay at three points in one go, keeps
Marlin's bias correction (a full-power/off relay on a roaster gives very
uneven halves: fast heating, slow cooling), and adds a convergence test
instead of a fixed cycle count.

## Model from the relay

With the settled bias (heater % that holds the test point), the steady gain
is `K = (T − T_ambient)/bias` °C per %. Assuming first order plus dead time,
`K e^(−Ls)/(τs + 1)`, and ω = 2π/Tu:

    τ = √((K·Ku)² − 1) / ω
    L = (π − atan(ω τ)) / ω

The model is reported per point (gain, τ, dead time) and feeds the SIMC rule.
It is approximate (ambient is assumed 25 °C) and is shown for diagnosis.

## Rules compared

From Ku and Tu (parallel form, `Ki = Kp/Ti`, `Kd = Kp·Td`):

| Rule | Kp | Ti | Td | Source |
|---|---|---|---|---|
| Ziegler–Nichols PI (default) | 0.45 Ku | Tu/1.2 | 0 | J. G. Ziegler, N. B. Nichols, "Optimum settings for automatic controllers", *Trans. ASME* 64, 1942, 759–768 |
| Tyreus–Luyben PI | Ku/3.22 | 2.2 Tu | 0 | B. D. Tyreus, W. L. Luyben, "Tuning PI controllers for integrator/dead time processes", *Ind. Eng. Chem. Res.* 31, 1992, 2625–2628 |
| Tyreus–Luyben PID | Ku/2.2 | 2.2 Tu | Tu/6.3 | as above |
| SIMC PI (τc = L) | τ/(2KL) | min(τ, 8L) | 0 | S. Skogestad, "Simple analytic rules for model reduction and PID controller tuning", *J. Process Control* 13, 2003, 291–309 |
| Ziegler–Nichols PID | 0.6 Ku | Tu/2 | Tu/8 | Ziegler, Nichols 1942 |

`CPID_AUTOTUNE_RULE` picks one (0 ZN PI, 1 TL PI, 2 TL PID, 3 SIMC PI).

### Simulation

Plant: the host test's `Drum`, a nonlinear Behmor-like model: heater element
lag 20 s, probe lag 8 s, 10 s transport delay, heat loss rising with
temperature (time constant about 250 s at 150 °C, 100 % holds 300 °C) and
0.25 °C probe noise, read in 0.25 °C steps (MAX31855 resolution). A
three-point run identified:

| Point | Ku (%/°C) | Tu | Bias | K (°C/%) |
|---|---|---|---|---|
| 149 °C | 4.05 | 136 s | 28.9 % | 4.29 |
| 177 °C | 4.48 | 130 s | 44 % | 3.44 |
| 204 °C | 4.43 | 129 s | 53 % | 3.37 |

Each rule's recommended set, run in closed loop: step from 25 to 177 °C,
ramp 6 °C/min to 210 °C, hold. Output TV is total variation of the heater
request (how much it moves), per minute.

| Rule | Overshoot | Ramp error (mean) | Hold error | Output TV |
|---|---|---|---|---|
| ZN PI | 1.0 °C | 3.0 °C | 0.20 °C | 122 %/min |
| TL PI | 0 | 7.8 °C | 0.79 °C | 84 %/min |
| SIMC PI | 0 | 7.5 °C | — | — |
| TL PID | 0 | 6.2 °C | — | ~3100 %/min |
| ZN PID | 5.5 °C | 1.9 °C | — | ~3300 %/min |
| Default gains 1/0.1/0.01 | 26.5 °C | 15.6 °C | 14.9 °C | — |

Robustness of the ZN PI set when the drum differs from the test: time
constant ×1.6 (a loaded drum) → overshoot 1.2 °C; dead time ×2 → 9.8 °C;
gain ×1.4 → 6.5 °C.

### Choice

- **Ziegler–Nichols PI** by default. It follows a ramp best of the quiet rules
  (3 °C mean lag on a 6 °C/min ramp) with 1 °C overshoot. Derivative on a
  thermocouple with 0.25 °C steps moves the heater 25 times as much for a
  small gain in tracking, so the default has none. Taking the most cautious
  gains over the points (lowest Kp, longest Ti) adds margin for the load.
- **Tyreus–Luyben PI** is the alternative for a drum that overshoots: no
  overshoot and a calmer heater, but twice the ramp lag.

## Gain scheduling

Per-point gains differ by about 10 % on the simulated drum. Switching gains
by temperature (gain scheduling, Åström and Hägglund, *Advanced PID Control*,
ISA 2006, ch. 12) improved the ramp error from 3.01 to 2.72 °C (IAE −8 %).
That is too small to justify a second control path, so it is not
implemented; the per-point gains are reported so a schedule can be added
later if a real drum shows a larger spread.
