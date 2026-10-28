# dpCREATOR PnP — Beta, phase 1 (no camera)

> **Beta.** This is test hardware and test firmware settings, not a finished product.
> You are testing it yourself, on your own machine. Read the whole page once before
> you power anything, and please send back the test log (section 10) — that is how
> this gets finished.

| | |
|---|---|
| Version | PnP beta 0.1 — 2026-10-28 |
| Firmware | dpCREATOR FluidNC 4.1.1 (W5500 build, release `w5500-latest`) |
| Board | dpCREATOR R3 (v4.0.3) |
| Config | `example_configs/dpcreator_r3_pnp.yaml` |
| Test files | `examples/pnp/pnp_01_bench_test.nc`, `examples/pnp/pnp_02_single_part.nc` |

**Contents**

1. [What phase 1 is](#1-what-phase-1-is)
2. [Safety](#2-safety)
3. [What you need](#3-what-you-need)
4. [Wiring](#4-wiring)
5. [Install the config](#5-install-the-config)
6. [First power-up](#6-first-power-up)
7. [Bench test](#7-bench-test)
8. [Your first part](#8-your-first-part)
9. [G-code reference for PnP](#9-g-code-reference-for-pnp)
10. [Test log — please send this back](#10-test-log)
11. [Troubleshooting](#11-troubleshooting)
12. [Known limits and what comes next](#12-known-limits-and-what-comes-next)

---

## 1. What phase 1 is

A working pick-and-place head on a standard R3 board, with **no new firmware and
no camera**:

| Machine part | R3 connection | Controlled by |
|---|---|---|
| X / Y gantry | X, Y axes | normal moves |
| Nozzle up/down | Z axis | normal moves |
| Nozzle rotation | A axis (in degrees) | `A` moves |
| Vacuum pump | Relay | `M62 P0` on / `M63 P0` off |
| Vacuum valve | PWM output (through a MOSFET or SSR) | `M62 P1` on / `M63 P1` off |
| Vacuum switch (optional) | LIM_A input | `M66 P0 L0` → `#5399` |

Positions (feeder pockets, board pads) are taught by jogging the nozzle and
copying machine coordinates. Jobs are plain g-code that runs from the SD card,
so a job does not depend on the browser staying open.

Phase 1 is good for parts that a fixed position is accurate enough for:
0805 and larger passives, SOT-23, SOIC. Fine-pitch parts need the camera
phases (section 12).

## 2. Safety

- **Reset drops the part.** Stop, Reset and any alarm switch the pump and valve
  off at once. That is on purpose.
- **E-stop stops the motors only.** The R3 E-stop disables STEP/DIR. The relay
  and valve outputs stay as they were until the board resets. If you want E-stop
  to kill the pump too, wire the pump supply through the E-stop circuit.
- **Never drive a solenoid straight from the PWM terminal.** Use a MOSFET module
  or SSR, with a flyback diode across the valve coil.
- **The relay is rated for its label, not more.** A small diaphragm pump is fine;
  check the pump current against the relay rating on your board.
- **Keep fingers away from the nozzle** while a job runs. Z moves fast.

## 3. What you need

| Part | Notes |
|---|---|
| dpCREATOR R3 + firmware `w5500-latest` | Same firmware as the CNC build |
| Gantry with X, Y, Z | Belts for X/Y are fine |
| Nozzle rotation motor | Small stepper, hollow shaft is easiest (NEMA 8 or 11) |
| Rotary vacuum union | If the motor shaft is not hollow |
| PnP nozzles | Juki-style or similar, sized for your parts |
| Vacuum pump | 12 V / 24 V diaphragm pump, runs for the whole job |
| 3/2-way solenoid valve | Normally closed. Powered: nozzle → pump. Off: nozzle → open air |
| MOSFET module or SSR | To switch the valve from the PWM terminal |
| Vacuum switch (optional) | Adjustable, normally open, closes on vacuum. Detects missed picks |
| Strip feeders or a tray | Anything that holds parts at a repeatable spot |

Pneumatics, in order:

```
pump  →  3/2 valve  →  (tee: vacuum switch)  →  rotary union / hollow shaft  →  nozzle
                ↘ vent to open air when the valve is off
```

The pump runs for the whole job; only the valve switches. That gives a fast
pick and a fast, clean release.

## 4. Wiring

| Function | R3 terminal | GPIO | Config line | Check |
|---|---|---|---|---|
| X / Y / Z motors | X, Y, Z | 4/5, 6/7, 15/16 | `axes:` | as for CNC |
| Nozzle rotation | A | 17/18 | `axes: a:` | |
| X / Y / Z endstops | LIM_X, LIM_Y, LIM_Z | 47, 48, 45 | `limit_all_pin` | Z homes **up** |
| Vacuum pump | Relay (NO / COM) | 39 | `user_outputs: digital0_pin` | |
| Vacuum valve | PWM | 21 | `user_outputs: digital1_pin` | **VERIFY** level and polarity |
| Vacuum switch | LIM_A ↔ LIMIT COM | 38 | `user_inputs: digital0_pin` | **VERIFY** polarity |
| Pendant | GX12 (UART1) | 1/2 | `uart1:` | unchanged |

Notes:

- **Valve:** PWM terminal → MOSFET module signal input; MOSFET module switches the
  valve's 12/24 V. If the valve is ON when it should be OFF, add `:low` to
  `digital1_pin` (`gpio.21:low`).
- **Vacuum switch:** wire it exactly like an endstop (LIM_A to LIMIT COM). The
  limit inputs are isolated, so use only a dry contact, no external voltage. If the
  bench test reads `1` with the nozzle open, remove `:low` from `digital0_pin`.
- **No vacuum switch?** Fine for phase 1. Leave the input unwired and delete the
  "check the part" block in `pnp_02_single_part.nc` (it is marked).

## 5. Install the config

1. **Back up your current `config.yaml`** (WebUI → Files → flash → download it).
2. Upload `dpcreator_r3_pnp.yaml` to the board's flash (WebUI → Files → flash).
3. Make it the active config and restart:

   ```
   $Config/Filename=dpcreator_r3_pnp.yaml
   $bye
   ```

4. After the restart check the startup messages:

   ```
   $Startup/Show
   ```

   You should see no `config` errors. Any line with `error` — copy it into the
   test log and stop here.

5. Set your own numbers: every line marked `<-- SET` (steps per mm, speeds,
   travel). Start with the slow speeds in the file and raise them only after the
   bench test passes.

To go back to the router: `$Config/Filename=config.yaml` then `$bye`.

## 6. First power-up

1. Power the board with **pump and valve power off**.
2. Turn the nozzle straight by hand (this angle becomes A = 0 — the A axis is
   not homed).
3. Home: `$H`. Z goes up first, then X and Y. The config requires homing before
   any other move (`must_home: true`), because every position you teach is a
   machine position.
4. Jog each axis a little and check the direction. A positive `A` should turn
   the nozzle counter-clockwise seen from above (flip `direction_pin` with `:low`
   if not).
5. Test the outputs by hand with the macro buttons:

   | Macro | Command | Expect |
   |---|---|---|
   | Macro0 | `M64P0` | pump relay clicks |
   | Macro2 | `M64P1` | valve output on (LED on MOSFET module) |
   | Macro3 | `M65P1` | valve output off |
   | Macro1 | `M65P0&M65P1` | everything off |

   Turn on the I/O fields in the status report once (it is saved):

   ```
   $Report/UserIO=On
   ```

   The status report then carries `|UO:` for the outputs and `|UI:` for the
   vacuum switch, one character each, output/input 0 first: `|UO:10` = pump on,
   valve off. `|UI:1` = vacuum present. Cover and uncover the nozzle tip (pump and
   valve on) and watch `|UI:` change — the quickest vacuum-switch check.

6. Now switch on pump and valve power.

## 7. Bench test

Copy `pnp_01_bench_test.nc` to the SD card, home, and run it. It stops after
every step (`M0`) with a message; check the result, write it in the test log,
press **Resume**.

| Step | What happens | Pass when |
|---|---|---|
| 1 | Pump on | pump runs |
| 2 | Valve on | valve clicks, suction at the nozzle tip |
| 3 | You cover the tip | message shows `Vacuum ... = 1` |
| 4 | You uncover the tip | message shows `Vacuum ... = 0` |
| 5 | Valve off | no suction, nozzle vents |
| 6 | Nozzle +90 / 0 / −90 / 0 | returns to the same angle every time |
| 7 | 10 turns and back | mark on the nozzle ends where it started |

Steps 3–4 need the vacuum switch; without it, write "no switch" and carry on.
If step 7 fails, A is losing steps: lower A `max_rate_mm_per_min` /
`acceleration_mm_per_sec2` or check the motor current.

## 8. Your first part

Use `pnp_02_single_part.nc`. It picks one part and places it, with a missed-pick
check.

1. Home. Put one part in the feeder and a board (or a piece of tape, sticky side
   up) on the bed.
2. **Teach the pick spot:** jog the nozzle over the centre of the part, lower Z
   until the tip just touches it. Copy the **MPos** X, Y, Z into `pick_x`,
   `pick_y`, `pick_z`.
3. **Teach the place spot:** jog to the centre of the pads, lower until the tip is
   at part height above the board. Copy into `place_x`, `place_y`, `place_z`.
   Set `place_a` to the rotation the part needs.
4. Set `safe_z` so the tip clears the feeder and the tallest part on the board.
5. Run it. Watch the first run with a hand near Stop.

What it does: travel at `safe_z` → down to 1 mm above the part → slow touch →
valve on → lift → check vacuum (retry once, then ask you) → move and rotate →
down → valve off → lift → nozzle back to 0 → pump off.

For many parts, copy the PICK / CHECK / PLACE block once per part with that
part's numbers. The PnP app (next step) will generate this file from a KiCad
position file instead.

## 9. G-code reference for PnP

| Code | Meaning | Use in jobs? |
|---|---|---|
| `M62 P0` / `M63 P0` | Pump on / off, **after** the current moves finish | yes |
| `M62 P1` / `M63 P1` | Valve on / off, **after** the current moves finish | yes |
| `M64 Pn` / `M65 Pn` | Same, but **at once**, even in the middle of a move | by hand only |
| `G4 P0.1` | Wait for moves to finish, then pause 0.1 s | before every `M66` |
| `M66 P0 L0` | Read the vacuum switch into `#5399` (1 = vacuum, 0 = none) | yes |
| `O100 IF [#5399 EQ 0]` … `O100 ENDIF` | Do something only on a missed pick (write it in capitals) | yes |
| `(MSG, text)` | Show a message in the WebUI / pendant | yes |
| `(PRINT, x = #5399)` | Show a value | for testing |
| `M0` | Pause until Resume | yes |
| `#<name> = 12.5` | Named number, use as `#<name>` | yes |
| `G53` | This line uses machine coordinates | yes |

Why `G4` before `M66`: `M66` reads the input the moment the line is read, while the
machine may still be moving to the previous position. `G4` makes it wait.

Only `L0` (read now) works for `M66` in this firmware; the wait-for-input modes
(`L1`–`L4`) are not implemented.

## 10. Test log

Please send this back (support ticket on dpcreator.eu, or the R&D group).

```
PnP beta 0.1 test log
Date:
Firmware (version.txt / $Build/Info):
Board: R3 v4.0.3   serial/chip ID:
Mechanics (belts/screws, motors, nozzle motor):
Pump / valve / vacuum switch models:

Config loaded without errors?  yes / no   (paste error lines)
Homing OK?                     yes / no
Outputs by hand (macros)  pump: ok/fail   valve: ok/fail   |UO:/|UI: shown: yes/no

Bench test   1:   2:   3:   4:   5:   6:   7:
Single part  picked: yes/no   placed: yes/no   offset seen (mm, deg):
Missed-pick check worked?      yes / no / no switch

Speeds that worked (X/Y, Z, A max rate and acceleration):
Anything strange:
Photos/video link:
```

## 11. Troubleshooting

| Problem | Likely cause | Fix |
|---|---|---|
| "error" after `$Config/Filename` | typo in the file, or missing section | `$Startup/Show`, check the line it names |
| Every move gives an alarm / "homing required" | `must_home: true` | home first (`$H`) |
| Valve on when it should be off | output polarity | `digital1_pin: gpio.21:low` |
| Vacuum reads 1 with the tip open | input polarity | `digital0_pin: gpio.38` (remove `:low`) |
| Vacuum always 0 | switch threshold too high, leak, or wiring | cover the tip, adjust the switch, check LIM_A ↔ LIMIT COM |
| Part drops during travel | weak vacuum, leak at the union, nozzle too small | check hoses, use a bigger nozzle |
| Part sticks to the nozzle on place | valve does not vent | 3/2 valve vent port must be open; raise `G4 P0.1` after `M63 P1` |
| Nozzle angle drifts | A losing steps | lower A rate/acceleration, raise motor current |
| `M66` error | input not defined in config | check `user_inputs:` |
| Part placed off by the same amount every time | taught position, or nozzle not centred | re-teach; check nozzle runout by rotating 360° |

## 12. Known limits and what comes next

Phase 1 limits:

- No camera: accuracy is whatever your mechanics and teaching give you.
- Every part is taught by hand (or copied from a feeder pitch by hand).
- A is not homed: set the nozzle straight before homing.
- `M66` can only read the input now (`L0`), not wait for it.

Coming next, in this order:

1. **PnP app (web)** on the Motion base: import a KiCad position file, align the
   board on 2–3 fiducials, set up strip feeders by pitch, and generate the job file.
2. **Top camera** (USB): find fiducials and feeder pockets automatically.
3. **Bottom camera:** measure each part on the nozzle and correct offset and rotation.

## Changelog

- **0.1 (2026-10-28)** — first beta: config, bench test, single-part template, this guide.
