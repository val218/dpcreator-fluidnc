# Charge pump for parallel-port breakouts

Many Mach3 breakout boards (TB6560 boards, optocoupler 5-axis boards, Gecko G540 with its switch on) keep the
stepper drivers disabled unless one DB25 input sees a square wave. `charge_pump:` makes it:

```yaml
charge_pump:
  pin: gpio.37      # any output pin (on the dpCREATOR Link: the GPIO behind the DB25 pin); ":low" inverts the idle level
  hz: 12500         # Mach3's value; 5000 to 25000
  stall_ms: 100     # stop it if the main loop hasn't run for this long (20 to 2000)
```

LEDC makes the 50 % square wave, so it costs no CPU. A 10 ms timer decides whether it may run:

| Off | On |
|---|---|
| before the config is loaded, Starting, ConfigAlarm, Critical, Sleep | Idle, Run, Hold, Jog, Homing, Door, Check mode |
| during a reset (`sys.abort`) | Alarm for "not homed" (after power-up) and other soft alarms: homing must be able to move |
| E-stop or fault control pin active | |
| Alarm after a hard limit, hard stop, probe hard limit, active control pin at start, expander reset | |
| main loop stalled longer than `stall_ms` (LEDC would keep running on a hung firmware) | |

A crash ends in a chip reset, which leaves the pin at its idle level. Every change is logged
(`[MSG:INFO: Charge pump on]`, `[MSG:INFO: Charge pump off: alarm]`); `$CP` (`$ChargePump`) prints
`ChargePump:on`, `ChargePump:off:<reason>` or `ChargePump:none`.

Before relying on it: check 12.5 kHz on the pin with a scope, that it stops within 100 ms on E-stop, a hard
limit and a forced stall, that nothing comes out while booting, and that a real charge-pump breakout enables
its drivers with it and drops them on E-stop.
