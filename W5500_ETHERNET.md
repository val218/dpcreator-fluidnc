# W5500 Ethernet on FluidNC 4.1.1 (ESP32-S3)

This branch (`fluidnc-4.1.1-w5500`) is stock FluidNC v4.1.1 plus fixes so that
everything works when the network is Ethernet instead of WiFi:

- WebUI / ESP800 / ESP111 report the Ethernet IP (was `0.0.0.0`)
- hostname (`fluidnc`) is used for DHCP, mDNS, OTA and ESP800
- mDNS (`http://fluidnc.local`), OTA and notifications start on Ethernet
- `$Ethernet/Setup=IP=... MSK=... GW=...` and `$Sta/Setup=...` parse correctly

plus dpCREATOR additions:

- dpCREATOR WebUI (3D toolpath view, DRO, jog, files, console) built into the firmware
- SD card usable as a **network drive** on your PC, with TabUI pendant `.viz`
  previews built automatically for every G-code file copied onto it
- TabUI pendant connection status (`$Pendant/Status`, badge in the WebUI)
- Job preview on the pendant: **Prepare** a file in the WebUI, it appears on
  the pendant, then **Run** or **Cancel** from either one (see section 8)
- **Job recovery** kept by the board: an interrupted job (alarm, E-stop,
  Stop, reset, power loss) is offered for restart from a safe line, with the
  same screens on the WebUI and the pendant (see section 9)
- **Live Z adjust** while a job cuts or is in Hold, from the WebUI or the
  pendant handwheel: a temporary offset kept by the board that ends with the
  job (see section 10)
- **Handwheel rewind** in Hold: go back along the cut path with the pendant
  wheel or the WebUI, change Z, and re-cut to where it stopped (section 11)
- PathRetrace / VizGenerator commands for the TabUI pendant

Tested on a dpCREATOR R2/R3 (ESP32-S3-WROOM-1-N16R8).

## Quick start: turn on Ethernet

A **fresh install** (`merged-flash.bin`, or after erasing settings) already
starts with Ethernet on and DHCP, using the dpCREATOR config: plug in the
cable and look up the IP in `$Ethernet/Status` or your router. The commands
below are only needed to change that, or on a board that was set to WiFi.

Send these over USB (serial console), one line at a time. `config.yaml` must
contain the `ethernet:` section (see [section 3](#3-configyaml)), or the W5500
won't start.

**Automatic IP (DHCP):**

```
$network/type=Ethernet
$Ethernet/IPMode=DHCP
$bye
```

**Fixed IP** (recommended for gSender and the network drive, the address never
changes; use a free address in your own LAN):

```
$network/type=Ethernet
$Ethernet/IPMode=Static
$Ethernet/IP=192.168.10.50
$Ethernet/Netmask=255.255.255.0
$Ethernet/Gateway=192.168.10.1
$bye
```

**Check it** after the reboot:

```
$Ethernet/Status
$Startup/Show
```

Look for `Ethernet link up` and `Ethernet IP is 192.168.10.x`, then
`ping 192.168.10.x` from the PC and open `http://192.168.10.x` in a browser.

WiFi is off after this. To go back: `$network/type=WiFi`, then `$bye` (over USB).

If `config.yaml` has no `ethernet:` section (for example after flashing a
default config), the board uses WiFi instead even with `$network/type=Ethernet`
(`[MSG:WARN: ... using WiFi so the board stays reachable]`): your saved WiFi
network, or else its own **FluidNC** access point at `http://192.168.0.1`,
where you can upload the right config.yaml. Ethernet comes back by itself once
the config has the `ethernet:` section.

---

**Contents**

0. [Quick start: turn on Ethernet](#quick-start-turn-on-ethernet)
1. [Get the firmware](#1-get-the-firmware)
2. [Wiring](#2-wiring)
3. [config.yaml](#3-configyaml)
4. [Switch FluidNC to Ethernet](#4-switch-fluidnc-to-ethernet)
5. [Connect gSender over Ethernet](#5-connect-gsender-over-ethernet)
6. [SD card as a network drive (Windows)](#6-sd-card-as-a-network-drive-windows)
7. [Pendant connection status](#7-pendant-connection-status)
8. [Job preview on the TabUI pendant (Prepare → Run)](#8-job-preview-on-the-tabui-pendant)
9. [Job recovery](#9-job-recovery)
10. [Live Z adjust](#10-live-z-adjust)
11. [Handwheel rewind](#11-handwheel-rewind)
12. [Macros](#12-macros)
13. [Work offsets and tools](#13-work-offsets-and-tools)
14. [G-code view, run from line, job history](#14-g-code-view-run-from-line-job-history)
15. [Troubleshooting](#15-troubleshooting)

---

## 1. Get the firmware

Firmware is built automatically by GitHub Actions on every push to this branch.

**Easiest:** the release
[**w5500-latest**](https://github.com/val218/fluidNC-with-w5500-support/releases/tag/w5500-latest)
always holds the newest build as plain files (no GitHub login, no zip):
`merged-flash.bin`, `firmware.bin`, … and `version.txt` (which commit).

Or from Actions (needs a GitHub login):

1. Open the **Actions** tab of this repository.
2. Open the latest run of **"Build FluidNC 4.1.1 W5500 (ESP32-S3)"**.
3. Download the **`fluidnc-4.1.1-w5500-s3-…`** artifact zip at the bottom of
   the run page. (The `debug-elf-webui-…` zip is only for decoding crashes.)

| File | Use |
|---|---|
| `merged-flash.bin` | Fresh board. Bootloader + partitions + firmware + filesystem, flash at `0x0`. **Overwrites `config.yaml` with the dpCREATOR R2/R3 example config (`example_configs/dpcreator_r2_r3_w5500.yaml`) - back yours up first if you changed it.** |
| `firmware.bin` | Board already running FluidNC. Flash at `0x10000` or upload via WebUI. Keeps config and settings. |
| `littlefs.bin` | Filesystem only (WebUI + config). |
| `firmware.elf` | In the separate debug zip: symbols to decode a crash backtrace. |

```
esptool.py --chip esp32s3 write_flash 0x0 merged-flash.bin
# or, keep existing config:
esptool.py --chip esp32s3 write_flash 0x10000 firmware.bin
```

To rebuild without changing code: **Actions → Build FluidNC 4.1.1 W5500 → Run workflow**.

---

## 2. Wiring

The W5500 shares the SPI bus with the SD card.

| W5500 module | ESP32-S3 | Notes |
|---|---|---|
| SCLK | GPIO12 | shared with SD card |
| MOSI | GPIO11 | shared with SD card |
| MISO | GPIO13 | shared with SD card |
| CS (SCSn) | GPIO14 | |
| INT | GPIO9 | optional — leave `int_pin: NO_PIN` if not connected |
| RST | — | optional, not used |
| 3V3 / GND | 3V3 / GND | short wires, solid ground |

SD card CS is GPIO10.

> **Important:** if INT is configured but not physically connected, the board gets
> link-up but never receives a packet (DHCP fails, ping times out). Either wire INT
> or set `int_pin: NO_PIN` (polling mode, ~10 ms extra latency, fine for CNC).

---

## 3. config.yaml

Add / adjust these sections (pins as above):

```yaml
spi:
  miso_pin: gpio.13
  mosi_pin: gpio.11
  sck_pin: gpio.12

sdcard:
  cs_pin: gpio.10
  card_detect_pin: NO_PIN
  frequency_hz: 8000000

ethernet:
  cs_pin: gpio.14
  int_pin: NO_PIN        # gpio.9 if the module's INT is wired
  rst_pin: NO_PIN
  phy_type: w5500
  phy_addr: 1
  frequency_hz: 8000000  # 8 MHz is safe on a bus shared with the SD card
```

Upload it (WebUI file manager or `$LocalFS`), then reboot with `$bye`.

---

## 4. Switch FluidNC to Ethernet

Optional first test, while still on WiFi — brings up only the W5500:

```
$Ethernet/Init
$Ethernet/Status
```

Expect `Ethernet PHY init succeeded` and `Link: Up` with a cable plugged in.

Switch the network to Ethernet:

```
$network/type=Ethernet
$bye
```

WiFi (including the AP) is off after this. To go back: `$network/type=WiFi` over USB, then `$bye`.

### DHCP (default)

```
$Ethernet/IPMode=DHCP
$bye
```

Recommended: add a **DHCP reservation** in your router for the board's MAC
(shown in `$Ethernet/Status`) so the IP never changes — gSender needs a fixed IP.

### Static IP

Use an address in **your** LAN subnet (check your PC with `ipconfig`):

```
$Ethernet/IPMode=Static
$Ethernet/IP=192.168.10.50
$Ethernet/Netmask=255.255.255.0
$Ethernet/Gateway=192.168.10.1
$bye
```

or in one line:

```
$Ethernet/Setup=IP=192.168.10.50 MSK=255.255.255.0 GW=192.168.10.1
```

### Check it

`$Startup/Show` should contain:

```
[MSG:INFO: Ethernet link up]
[MSG:INFO: Ethernet IP is 192.168.10.x]
[MSG:INFO: Start mDNS with hostname:http://fluidnc.local/]
[MSG:INFO: HTTP started on port 80]
[MSG:INFO: Telnet started on port 23]
```

From the PC:

```
ping 192.168.10.x
```

Then open `http://192.168.10.x` or `http://fluidnc.local` for the WebUI.

---

## 5. Connect gSender over Ethernet

gSender connects to FluidNC's **Telnet server** (raw TCP). Tested with gSender 1.6.4.

1. Find the board's IP (`$Ethernet/Status` or the boot log) and Telnet port
   (`$Telnet/Port`, default `23`).
2. In gSender open **Config → Ethernet**:
   - **Connect to IP**: the board's IP, e.g. `192.168.10.102`
   - **Ethernet port**: same as `$Telnet/Port` (default `23`)
3. Click **Apply Settings**.
4. Open the connection menu (top left) and click the **Ethernet** entry
   (`192.168.10.x — Ethernet (port N)`), not a COM port.

> **Do not use gSender's "Remote Mode" for this.** Remote Mode is a web server that
> runs on the PC so phones/tablets can control gSender; its IP must be the **PC's own**
> address. Putting the controller IP there gives
> *"There was a problem connecting to the remote address… Remote mode has been disabled"*.

Changing the Telnet port on the board (if needed):

```
$Telnet/Port=2323
$bye
```

…and set the same number in gSender's Ethernet port field.

---

## 6. SD card as a network drive (Windows)

The board serves its SD card over the LAN with WebDAV at `http://<board-ip>/sd`.
Windows can map that to a drive letter, which then shows up in **This PC** and
reconnects at every sign-in. Copy G-code onto it like onto a USB stick.

### Easiest: setup script

**From the WebUI:** Files panel → **PC drive** downloads the script with your
board's IP already filled in; double-click it on the PC (steps 2-3 below, no
IP to type). Or:

1. Download [`tools/windows/dpcreator-sd-drive.cmd`](https://raw.githubusercontent.com/val218/fluidNC-with-w5500-support/fluidnc-4.1.1-w5500/tools/windows/dpcreator-sd-drive.cmd)
   (right-click the link → *Save link as…*).
2. **Double-click it** (do *not* use "Run as administrator"). If Windows says
   *"Windows protected your PC"*, click **More info → Run anyway**.
3. Enter the board's IP (Enter = `192.168.10.104`) and answer **Yes** on the
   one admin prompt.

It turns on the Windows WebDAV client, lifts the 50 MB file limit, maps the
SD card as **S:** named **dpCREATOR SD**, and adds a hidden sign-in task that
reconnects it after every restart. Run it again if the board's IP changes.
Other drive letter: `dpcreator-sd-drive.cmd 192.168.10.104 Z`.
Undo everything: `dpcreator-sd-drive.cmd remove`.

**Show it together with C:, D: …** Windows always lists mapped drives under
*Network locations* in This PC. To have one list: open **This PC**, right-click
an empty area → **Group by → (None)**.

### Manual setup

**One-time setup** (Command Prompt **as administrator**: Start → type `cmd` →
right-click → *Run as administrator*):

```
sc config WebClient start= auto
net start WebClient
reg add HKLM\SYSTEM\CurrentControlSet\Services\WebClient\Parameters /v FileSizeLimitInBytes /t REG_DWORD /d 4294967295 /f
net stop WebClient && net start WebClient
```

The `WebClient` service is the Windows WebDAV client. The registry line lifts
its default 50 MB per-file limit (large G-code files fail to copy without it).

**Map the drive** (a **normal** Command Prompt, not the admin one: a drive
mapped as administrator does not show in your Explorer; use your board's IP):

```
net use S: http://192.168.10.104/sd /persistent:yes
```

or in Explorer: **This PC → Map network drive**, Folder `http://192.168.10.104/sd`,
tick **Reconnect at sign-in**.

Give the board a fixed address (static IP, or a DHCP reservation on the router
for its MAC) so the drive keeps working after a reboot.

**Pendant previews are built automatically.** Whenever a G-code file
(`.nc .gcode .gc .ngc .tap .cnc .g`) is written to the SD card, from this
drive, the web UI or the classic UI, FluidNC queues `<file>.viz` for the TabUI
pendant. The build waits until **nothing moves and no job is running** (Idle, or
Alarm right after power-up; the controller does not take commands while it
reads the file), starts 1.5 s after the last write,
and replaces any old `.viz`. Deleting or renaming a G-code file over the drive
removes its `.viz`. You will see the `.viz` files next to your G-code in
Explorer; leave them there. Progress shows in the web UI (and any console) as
`[MSG:VizAutoBusy/VizAutoReady/VizAutoErr:...]`; these are not sent to the
pendant, so they never replace what the pendant is showing.
`$Viz/Refresh=/sd/file.nc` queues a rebuild by hand (WebUI: select the file →
**Pendant .viz**, with a confirmation). Large files are thinned so the preview
always covers the whole file (max 8000 points).

Tips:
- Clicking the board under Explorer's **Network** folder, or typing
  `\\192.168.10.104`, gives a "problem accessing" error: that is Windows file
  sharing (SMB), which FluidNC does not have. Use the S: drive, or
  `\\192.168.10.104@80\sd` in the address bar.
- Don't copy big files while a job is running from the SD card; the card is
  shared and the job has priority. The `.viz` build waits for the job anyway.
- If Explorer is slow to open the drive, untick **Automatically detect
  settings** in Internet Options → Connections → LAN settings (WebClient
  waits for proxy auto-detection).
- macOS: Finder → Go → Connect to Server → `http://192.168.10.104/sd`.
  Linux: `davfs2` or your file manager's `dav://192.168.10.104/sd`.

## 7. Pendant connection status

`$Pendant/Status` answers `[MSG:Pendant:connected]`, `disconnected` or `none`
(no `uart_channel1` configured), and every channel except the pendant's own
gets that message when it changes. The web UI shows it as a badge in the
header. Detection uses the pendant's link pings: an idle pendant sends `?`
every 500 ms, so an unplugged pendant is noticed within about 3 s while the
machine is idle. During a running job the board streams reports and the
pendant may stay quiet, so the badge keeps its last state until the job ends.

## 8. Job preview on the TabUI pendant

The board builds and sends the pendant the toolpath preview of each G-code
file, so the pendant shows the same part as the WebUI's 3D view.

### Prepare → Run

SD-card G-code files have a **Prepare** button instead of Run — in the WebUI
and in the pendant's Files tab; both show the same prepared file:

1. **Prepare**: the file's path is sent to the pendant, which switches to its
   DRO screen and draws it; the WebUI viewer shows the same file. A
   **Prepared** bar above the viewer shows the pendant's state:
   *loading on the pendant…* → *shown on the pendant* (or *no pendant
   connected*).
2. **Run** from either side starts the job: **▶ Run** in the WebUI bar, or
   **Run** (tap twice) on the pendant, which first raises Z, moves to X0 Y0,
   then runs the file.
3. **Cancel** from either side clears it on both.

Flash files keep a direct **▶ Run**. A job started any other way (console,
classic UI, gSender…) while a pendant is connected waits until the pendant
shows its preview, then starts by itself; it starts anyway after 15 s
without progress or if the pendant disconnects, and Reset cancels it.

### `.viz` previews

- `<file>.viz` sits next to the G-code on the SD card and is built
  **automatically**: after an upload (WebUI, network drive, classic UI), when
  a job starts, or when the pendant asks for a file without one. The build
  runs in the background; the controller keeps taking commands.
- Large files are covered from the first line to the last (two passes: the
  path is measured, then written with one level of detail for the whole file).
- The pendant gets a copy sized for its screen: detail smaller than one
  screen pixel of the part is left out, and it never gets more points than it
  has memory for.
- WebUI file list: green dot = preview ready, red = not built yet (it will be
  when needed), blinking amber = being built. The **.viz** button builds or
  rebuilds it by hand (only needed for older files).
- `.nc .gcode .gc .ngc .tap .cnc .g` files are supported, including Vectric /
  Mach `.tap` headers (`G90.1` / `G91.1`).

### Commands

```
$Job/Prepare=/file.nc     show a file on the pendant (WebUI Prepare)
$Job/Unprepare            cancel it
$Job/Prepared             report: [MSG:Prepared:<loading|ready|nopendant|none>:<file>]
$Viz/Refresh=/file.nc     rebuild a file's .viz in the background
$Viz/Push=/file.nc        send a file's preview to the pendant (the pendant does this)
$Pendant/Trace=on|off     echo the pendant's command lines (PND> ...) to the WebUI terminal
$Pendant/Debug            link counters: bytes, lines, noise / dropped bytes
```

While a preview is being built or sent, the WebUI terminal shows `VizBusy`,
`VizPush: ... N of M points, X mm detail` and `VizReady` / `VizErr` lines.

## 9. Job recovery

The board writes down the line every SD file job is at (every few seconds,
and at once on Hold or a stop) to its flash, so a power cut doesn't lose it.
A job that ends any way other than reaching the end of the file is kept as
**interrupted**. The WebUI (a card under the 3D view; a sheet at the bottom on
a phone) and the pendant (a screen of its own; "Interrupted job – tap to
recover" on the DRO after **Later**) show the same three steps:

1. **Job interrupted** – file, line it stopped at, why. *Resume…*, *Later*,
   *Discard*.
2. **Restart from** – the board scans the file and suggests:
   - **Last retract** – just after the last rapid move that lifted Z (the
     tool was clear of the part): the safest choice, recuts a little.
   - **Start of this tool** – just after the last tool change.
   - **Where it stopped** – may restart in the middle of a cut.
   - **Any line** – WebUI: type it; pendant: turn the handwheel.
   The G-code lines around the choice are shown so you can check it, and the
   WebUI's 3D view marks the path done up to that line.
3. **Before starting** – home the machine, keep the same work / Z zero, put
   the right tool in (there is no automatic tool change). **Dry run** moves
   to safe height and over the restart point only. **Resume** (confirm):

   - lift Z to machine top, set coordinate system, plane, units
   - rapid to X/Y (and A) of the restart point
   - spindle on with the file's direction and speed, 2 s spin-up, coolant
   - rapid to 3 mm above, feed down to the cut depth
   - restore G90/G91, G20/G21, feed and motion mode, then continue the file
     from the chosen line

The machine must be **Idle and homed**. A stop or E-stop while cutting
leaves it in **alarm**, so the recovery screens have their own **Unlock**
(`$X`) and **Home** (`$H`, asks first) buttons: the WebUI card shows them in a
red row with what to do, the pendant's last screen swaps *Dry run / START* for
*Unlock / Home* until the machine is ready. The board reports this as
`[MSG:RecoveryReady:<alarm|unhomed|busy|ok>]` whenever it changes. If the chosen line is in the middle of an arc, a note asks you to
check the first move.

```
$Job/Recovery            report: [MSG:Recovery:<state>:<stop>:<retract>:<tool>:<reason>:<file>]
                         state: none | interrupted | scanning | resuming
$Job/Points              work out the suggested restart lines
$Job/Resume=<line>       continue the interrupted job from <line>
$Job/Resume=<line>:dry   only move above that line's start point
$Job/Discard             forget the interrupted job
```

The pendant no longer keeps its own checkpoint: with an older board
firmware, the pendant shows no recovery at all. Flash both.

## 10. Live Z adjust

Raise or lower the cut while the job runs, e.g. when the first pass shows the
material is thicker or the zero was a little off. It works like a wear
offset on an industrial control or "babystepping" on a 3D printer:

- **Temporary.** The board keeps the offset for the running job only. It is
  not written to the work coordinates or to flash, and it ends with the job
  (finished, stopped, alarm, reset).
- **+ = tool higher = shallower, − = tool lower = deeper.**
- **Limited:** at most 0.5 mm per step and ±1 mm in total by default
  (`$Job/ZAdjust/Limit=<mm>`, 0.05 … 5 mm, kept across restarts).
- **Works while cutting and in Hold**, from the WebUI and the pendant.
- **The DRO keeps showing the file's Z.** The offset is part of the work
  offset (WCO) while it is active, so the WebUI 3D view and the pendant path
  still line up with the file.

### When it reaches the tool

**Live.** Like babystepping on a 3D printer, the board's stepper interrupt
adds single Z steps between the job's own steps (on ticks where the job does
not step Z, at most one step per millisecond), so the tool moves to the new
height within a fraction of a second while it cuts — whatever the moves are.
The screens show `pending` ("moving to it…") until all steps are out, then
`active`.

- The steps only go out while the machine moves. **In Hold** a change waits
  for **Resume** ("applies on Resume").
- MPos is the real tool position. The work offset (WCO) includes the steps
  made, so WPos, the DRO and the 3D views keep showing the file's Z.
- When the job ends (finished, stopped, alarm) the board takes the real
  position over: nothing moves back, and the next move goes where it is
  told. Use **Apply** afterwards if the next run should cut the same.

### Using it

- **WebUI:** while a job runs a **Z ADJUST** bar appears under the 3D view:
  ▼ 0.1 / ▼ 0.01 / ▲ 0.01 / ▲ 0.1 and **0** (reset), with `moving to it…`
  or `active`. The same row is under Control panel → Overrides.
- **Pendant:** while the job runs the bottom bar is **Hold | Z adj | Abort**.
  Tap **Z adj**, hold the enable button and tap −0.1 / −0.01 / +0.01 / +0.1,
  or turn the wheel (**CW raises, CCW lowers**, by the step switch). **Reset 0**
  clears it, **Done** (the middle button) closes the panel. The middle button
  shows the offset (yellow = pending).

If the WebUI has no Z adjust bar, the page is older than the firmware: the page
is in the filesystem image, so a `firmware.bin`-only (OTA) update keeps the
old page. Flash `merged-flash.bin` (or `littlefs.bin`) and reload with
Ctrl+F5. The WebUI now warns about this (it compares its build with the
firmware's, footer shows both).

### After the job

If the job ended with an offset, both screens ask **"Move the work Z zero by
it, so the next run cuts the same?"** – **Apply** adds it to Z of the
coordinate system the job used (G54…G59), **No** forgets it. Starting a new
job also forgets it. A job continued with *Job recovery* starts with no offset;
set it again if needed.

```
$Job/ZAdjust               report: [MSG:ZAdjust:<offset>:<none|pending|active|ended>:<line>]
$Job/ZAdjust=+0.05         add (with sign) - only while a job runs
$Job/ZAdjust=0.2           set (no sign); =0 clears (and declines the end-of-job offer)
$Job/ZAdjust/Apply         move the work Z zero by the offset the last job ended with (Idle)
$Job/ZAdjust/Limit=1.0     largest total offset in mm (0.05 .. 5)
```

Errors come as `[MSG:ZAdjustErr:<text>]` (no job running, at most 0.5 mm per
step, limit reached, …).

> The pendant's old Z nudge sent `G10 L20 P1 Z…` (it changed G54 in flash,
> the opposite way round, and FluidNC 4.1 refuses G-code from a second
> channel during a job). Flash both firmwares.

## 11. Handwheel rewind

While a job is in **Hold**, move the tool back along the path it has cut,
change the height there, and re-cut to where it stopped — *manual handle
retrace* as on industrial controls.

1. **Hold**, then **Rewind** (pendant: REWIND in the Hold strip; WebUI: the
   Rewind bar under the 3D view).
2. **Back / forward** along the path: pendant wheel (enable held) CCW / CW,
   0.1 / 1 / 5 mm per click by the step switch; WebUI ◀◀ 5 · ◀ 1 · ◀ 0.1 and
   0.1 ▶ · 1 ▶ · 5 ▶▶ (hold a button to repeat). Never past the hold point.
3. **Z** −0.1 / −0.01 / +0.01 / +0.1 there.
4. **Continue** (or cycle start / Resume): re-cut at the programmed feed from
   there to the hold point at the new height, then the job resumes; the height
   change becomes the job's live Z adjust (section 10).
   **Cancel**: back to the hold point at the old height, still in Hold.

How it works:

- The board records every move of the running job (end point, feed, file
  line, spindle) when it is planned — up to 8192 moves in PSRAM. In Hold the
  moves still queued are left out, so the path starts exactly where the tool
  stopped.
- The rewind moves are *system motions* — what FluidNC uses to park for the
  safety door — so the held job, its planner and the spindle stay as they are.
  Retracing goes at the programmed feed, at most 1500 mm/min (rapids at
  1000 mm/min), one path segment or 1 mm per move.
- It goes back through the recorded moves and stops at a tool change / spindle
  start or speed change. Not in laser mode.
- **Continue** only resumes the job when the tool is back at the hold point
  (X/Y/A exact). A reset or alarm while rewound ends the job as any other —
  use *Job recovery*.
- Live Z adjust commands are refused while rewinding (use the Rewind Z
  buttons).

```
$Rewind/Start              enter rewind (job in Hold)
$Rewind/Move=-0.5          0.5 mm back along the path (+ = forward)
$Rewind/Z=+0.05            raise (+) / lower (-) the tool there
$Rewind/Continue           re-cut to the hold point and resume (also ~)
$Rewind/Cancel             back to the hold point, stay in Hold
$Rewind                    report
```

Reports: `[MSG:Rewind:<off|active|continuing|returning>:<back_mm>:<z_mm>:<line>:<max_back_mm>]`,
`[MSG:RewindErr:<text>]`. The old `$Retrace/...` motion commands (pendant
*PATH RETRACE*) are removed: they jogged in work coordinates to recorded
machine positions (jogging is refused in Hold) and resumed the held job from
the wrong place.

## 12. Macros

Macros are kept by the board on the SD card, so the WebUI and the pendant use
the same list and it survives firmware updates:

```
/sd/.dpc/macros.txt        one line per macro, in button order: <id>|<name>|<#rrggbb>|<flags>
                           flags: p = on the pendant, c = ask before running, b = button on the Control panel
/sd/.dpc/macros/<id>.nc    the macro itself
```

- **WebUI:** Control panel → **Macros** shows the buttons; **Edit…** opens the
  editor: name, colour, *on the pendant*, *ask before running*, *button on the
  Control panel*, the script, reorder, copy, delete, **Save & run**, and
  **Export / Import** (one JSON file) to back up or share macros.
- **Pendant:** the Macros tab shows the macros marked *on the pendant* (tap twice
  for the ones that ask first). It loads the list when it connects and again
  when it is changed.
- **Scripts:** any number of lines — G-code, `$` commands and FluidNC's
  expressions: `#<name>` variables (`#<_name>` kept between macros), maths in
  `[ ]`, `o100 if / elseif / else / endif`, `while`, `do`, `repeat`,
  `(PRINT, ...)` / `(MSG, ...)`, `#5061..#5063` last probe position, `#5070`
  probe touched, `#<_x> #<_y> #<_z>` position. The editor has a help box.
- A macro runs like a file job (Hold / Stop work, the machine must be Idle to
  start) but is not offered for job recovery and has no preview.
- The first time, a starter set is created (Park, Zero X / Y / Z / all,
  coolant, spindle, a spindle warm-up).

```
$MacroLib/List         [MSG:MacroItem:<id>:<#rrggbb>:<flags>:<name>] ... [MSG:MacroEnd:<count>]
$MacroLib/Run=<id>     run it
$MacroLib/Reload       tell every screen the list changed
```

## 13. Work offsets and tools

**Offsets…** (next to the coordinate system selector) shows G54–G59 as a table
(machine position of each work zero, from `$#`): edit a value and press Enter
(`G10 L2`), **Zero XY here / Zero Z here** (`G10 L20`, the tool position becomes
0 in that system), **Use** to switch to it. Below: G92 (with **Clear G92**),
the tool length offset and G28 / G30.

**Tools…** is the tool table and tool change with a **fixed tool setter**:

- The list (number, name, diameter, notes) is kept on the SD card
  (`/sd/.dpc/tools.txt`).
- **Change** runs `M6 T<n>` with FluidNC's `atc_manual` tool changer: Z to safe
  height; the first time, the tool in the spindle is measured on the setter as
  the reference; then the machine goes to the change position and waits. A bar
  *"Install tool T<n> … Resume"* appears (the pendant shows Hold / Resume);
  after **Resume** the new tool is measured and the tool length offset (G43.1)
  is set from the difference. Set the work Z zero with the first tool, then
  change tools with M6.
- **Reset (M6 T0)** forgets the reference and sets the tool length to 0 — for a
  new job / new work zero.
- **Set up the tool setter** builds the `atc_manual:` section for config.yaml
  from positions you jog to (**Here** buttons): change position, setter X/Y,
  lowest Z (fails below it: no tool), rapid-down Z, safe Z, probe speeds. Add
  `atc: atc_manual` under your spindle and check the probe pin. If it is set
  up, the dialog shows the setter and change positions from the running config.

## 14. G-code view, run from line, job history

- **G-code** (3D view bar) shows the file as text under the view: the line being
  cut is highlighted and followed (untick *follow* to look around). Click a line
  and **Run from line**: the board offers it like an interrupted job
  (`$Job/RunFrom=<line>:<file>`), so the job recovery screens on the WebUI and
  the pendant take over — restart point (that line, last retract before it,
  start of the tool), Unlock / Home if needed, Dry run, Resume.
- **History…** (Machine panel): the jobs the board ran — date, file, time,
  done / stopped / alarm and the line — newest first, from
  `/sd/.dpc/jobs.log`; stopped jobs have **Run from line**. Above it the hour
  counters: cutting time, spindle time, spindle time since the last service
  (**Reset service counter**) and the number of jobs (`$Stats`,
  `/sd/.dpc/stats.txt`). The board has no clock: the WebUI tells it the time
  when it connects, so jobs run with no page open have no date.

## 15. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `Ethernet PHY init failed` | Wrong SPI/CS pins, wiring, or no `spi:` section. |
| Link up, IP `0.0.0.0` (DHCP), ping fails | INT configured but not wired → set `int_pin: NO_PIN`. Or no DHCP server on that port. |
| Static IP set, ping says *Destination host unreachable* | Board and PC in different subnets, or board now has a different IP (DHCP). |
| `Test-NetConnection <ip> -Port <port>` → `TcpTestSucceeded : False` but ping OK | Telnet port differs from what you test; check `$Telnet/Port`, `$Telnet/Enable=ON`. |
| gSender: *"Remote mode has been disabled"* | Controller IP was entered in Remote Mode. Use **Config → Ethernet** instead. |
| gSender: *Unable to connect* | IP/port in Config → Ethernet don't match the board; click **Apply Settings**. |
| Pendant shows no path / a wrong path | Check the WebUI terminal for `VizErr`; press **.viz** on the file to rebuild it. On the pendant's terminal tab, `RX overflow` or `garbled msgs dropped` mean bytes are lost on the pendant cable — use a shorter / shielded cable or a lower baud rate (same on both ends). |
| Browser / Windows calls the firmware zip *dangerous* | False alarm on an unknown download; the firmware zip now holds only the `.bin` files. If it is still blocked: keep / "Download anyway", or check the file in Windows Security → Protection history and allow it. |
| Z adjust (or any `$` command) from the WebUI does nothing while a job runs that was started from the WebUI | Fixed: `$SD/Run` started from a WebUI / pendant / terminal now answers `ok` at once. Before, the reply waited for the end of the job and the board took no further command lines from that page until then (only Hold / Resume / overrides). |
| Pendant keeps the *E-stop released* menu after the job was unlocked / resumed from the WebUI | Fixed (pendant firmware): the menu closes by itself when the alarm is cleared anywhere. |
| Board stops answering (WebUI, pendant, serial) after the pendant was plugged in / rebooted | Fixed: XON/XOFF is off on the pendant UART — a noise byte that looked like XOFF paused the board's transmitter and blocked it. |
| Hold / door / reset when the pendant is plugged in or rebooted | Fixed: line noise on the pendant UART is dropped. `$Pendant/Debug` shows how many bytes were dropped. |
| WebUI shows *"The controller restarted after a crash"* | Press **Copy details** and send the text: the reason (panic, task / interrupt watchdog), the crash backtrace and the last commands sent before it (`$Crash` prints them too). One crash restarts with your `config.yaml`; only 3 crash restarts in a row fall back to the default config (WiFi AP `FluidNC`, no Ethernet) - reset the board to try again. |
| Boot shows *"Showing startup log from previous panic"* | Capture serial output at 115200 including `Guru Meditation` and `Backtrace:` lines and decode via **Actions → Run workflow** with the backtrace input. |

Windows has no telnet client by default; test the port with PowerShell:

```powershell
Test-NetConnection 192.168.10.102 -Port 23
```

or use PuTTY (connection type **Raw**), type `?` + Enter → `<Idle|MPos:...>`.

### Useful commands

```
$Ethernet/Status        link, IP, mask, gateway, MAC
$Ethernet/Setup         show static IP settings
$Ethernet/Init          bring up the W5500 manually (test)
$network/type           WiFi | Ethernet
$Telnet/Port            Telnet (gSender) port
[ESP800]json=yes        WebSocketIP should be the Ethernet IP
```
