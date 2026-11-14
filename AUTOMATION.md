# Automations

Rules that run on the board itself, with no browser open: an output that
follows the spindle, an M-code that switches the vacuum table, a pedal on an
input, lubrication every 30 minutes of motion, a feed hold when the vacuum
drops. The WebUI writes them to `automation.json` on the local filesystem
(`/localfs/automation.json`); the board reads it at startup and on
`$Automation/Reload`.

Nothing here moves the machine except a `gcode` action, which runs only when
the machine is Idle with no job. Output actions run at any time, also during a
job. An alarm stops every running sequence and switches every `follow` output
off.

## Commands

| Command | Short | |
|---|---|---|
| `$Automation/List` | `$AUL` | one line per rule: `[AUTO:<n>,<type>,<state>[,<detail>],<name>]` |
| `$Automation/Reload` | `$AUR` | read `automation.json` again; errors name the rule and action |
| `$Automation/Run=<n>` | `$AUX=<n>` | start rule n (input, event, timer or manual rules) |

`<state>`: `on` / `off` (follow), `running` / `ready` (the others), `disabled`.
`<detail>`: `M110` for an M-code rule, `<counted s>/<every s>` for a timer.
Examples:

```
[AUTO:0,follow,on,Dust extraction]
[AUTO:1,mcode,ready,M110,Vacuum on]
[AUTO:3,timer,ready,1260/1800,Lubrication]
```

The board logs `[MSG:INFO: Automation: 7 rule(s) from automation.json]` at
startup, or `[MSG:ERR: Automation: automation.json rule 3 (Lube): action 1: pulse needs "time" > 0]`.
Messages from `msg` actions: `[MSG:INFO: Automation: <text>]`; from `hold`:
`[MSG:WARN: Automation: <text>]`.

## automation.json

```json
{
  "version": 1,
  "rules": [
    {"name": "Dust extraction", "type": "follow", "when": "spindle", "out": 2, "off_delay": 5},
    {"name": "Vacuum on",  "type": "mcode", "code": 110, "actions": [{"do": "out", "n": 3, "value": 1}]},
    {"name": "Vacuum off", "type": "mcode", "code": 111, "actions": [{"do": "out", "n": 3, "value": 0}]},
    {"name": "Vacuum lost", "type": "input", "input": {"exp": 2, "in": 5}, "edge": "fall", "only": "job",
     "debounce": 0.2, "actions": [{"do": "hold", "text": "Vacuum lost"}]},
    {"name": "Vacuum pedal", "type": "input", "input": {"user": 6}, "edge": "rise", "only": "idle",
     "actions": [{"do": "out", "n": 3, "value": "toggle"}]},
    {"name": "Lubrication", "type": "timer", "every": 1800, "count": "motion",
     "actions": [{"do": "pulse", "n": 5, "time": 3}]},
    {"name": "Park when idle", "type": "event", "event": "idle", "after": 600, "enabled": false,
     "actions": [{"do": "gcode", "text": "G53 G0 Z-1"}]}
  ]
}
```

All times are in seconds (decimals allowed). At most 32 rules, 32 actions each,
32 KB.

### Common rule fields

| Field | |
|---|---|
| `name` | shown in `$AUL` and messages |
| `type` | `follow`, `input`, `mcode`, `event`, `timer`, `manual` |
| `enabled` | default `true` |
| `only` | `always` (default), `job` (a file job, or G-code running or held), `idle` (no job): when the trigger counts. Not for `follow` and `mcode`. |
| `actions` | list of actions (all types except `follow`) |

### Types

- **follow** – output `out` (user output 0–15) is on while the condition holds.
  `when`: `spindle`, `job`, `motion` (Cycle, Jog, Homing), `idle`, `input` (with `input`).
  `invert`, `on_delay`, `off_delay`. Switching back during a delay cancels it.
- **input** – `input` changes; `edge`: `rise` (default), `fall`, `change`;
  `debounce` (default 0.03). An expander that goes offline is not an edge.
- **mcode** – `code` 100–199 becomes a G-code: `M110`. Runs after the moves
  before it, inside a job too. No `gcode` actions; `wait_input` needs a `timeout`.
  Unknown M1xx codes stay errors.
- **event** – `event`: `spindle_on`, `spindle_off`, `job_start`, `job_end`,
  `alarm`, `idle` (with `after`: Idle for that long, once per idle period).
- **timer** – every `every` seconds (≥ 1) of `count`: `always` (default),
  `motion`, `spindle`, `job`. The count starts at 0 at power-up.
- **manual** – only `$Automation/Run=<n>`.

A rule that is still running ignores new triggers.

### Inputs

`{"user": 3}` = user input `digital3_pin` (as FluidNC sees it, after `:low`).
`{"exp": 2, "in": 5}` = IN5 of the IO expander on uart_channel2, as the
expander reports it (1 = 24 V present) – it needs no config.yaml entry.

### Actions

| `do` | Fields | |
|---|---|---|
| `out` | `n`, `value` 0/1/`"toggle"` | user output `digital<n>_pin` |
| `pulse` | `n`, `time` | on, wait, off |
| `pwm` | `n` (0–3), `value` 0–100 | analog output (`analog<n>_pin` or a Modbus one) |
| `wait` | `time` | |
| `wait_input` | `input`, `value` 0/1, `timeout`, `on_timeout`, `text` | `on_timeout`: `stop` (default; the rule ends with a message), `continue`, `hold` (feed hold) |
| `msg` | `text` | |
| `hold` | `text` | feed hold, resumable with cycle start |
| `gcode` | `text` | Idle only; `&`-separated lines or `$SD/Run=/file.nc`, like a FluidNC macro |

Outputs must be in `user_outputs:` (digital0_pin–digital15_pin) in config.yaml.
