# One limit-switch input for several axes

Some machines wire the end switches of several axes in series to one input: Stepcraft (X, Y and Z on
DB25 pin 12), many Mach3 and WinPC-NC machines. FluidNC claims each GPIO once ("Pin is already used"), so
until now only one axis could have that switch. `limit_shared_with` lets the other axes use it too.

```yaml
axes:
  z:
    homing:
      cycle: 1
      positive_direction: true
    motor0:
      limit_all_pin: gpio.46      # the one input
      hard_limits: true
      pulloff_mm: 1.5
  x:
    homing:
      cycle: 2
      positive_direction: false
    motor0:
      limit_shared_with: z        # same switch input as Z
      pulloff_mm: 1.5
  y:
    homing:
      cycle: 3
      positive_direction: true
    motor0:
      limit_shared_with: z
      pulloff_mm: 1.5
```

How it works: the owner axis's `limit_all_pin` sets the limit bit and stops the motor of every axis that
shares it. The axes home **one after the other** (different `cycle` numbers), so a touch during homing
belongs to the one axis that is moving; it pulls off, the switch opens, and the next cycle starts. Outside
homing any touch stops all of them and raises a hard-limit alarm (`hard_limits` of the owner axis).

Rules, checked at start-up (`Axes::link_shared_limits`); a broken rule logs an error and leaves that axis
without a switch:
- `limit_shared_with` names another axis, whose motor0 has a `limit_all_pin` and doesn't share itself;
- axes on the same switch must not home in the same cycle (they would all stop at the first touch).

A shared switch can't tell which axis touched it, so after a hard-limit alarm the axis has to be found by
eye, as on the original controller.
