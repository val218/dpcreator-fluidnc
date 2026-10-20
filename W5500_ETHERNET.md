# W5500 Ethernet on FluidNC 4.1.1 (ESP32-S3)

This branch (`fluidnc-4.1.1-w5500`) is stock FluidNC v4.1.1 plus fixes so that
everything works when the network is Ethernet instead of WiFi:

- WebUI / ESP800 / ESP111 report the Ethernet IP (was `0.0.0.0`)
- hostname (`fluidnc`) is used for DHCP, mDNS, OTA and ESP800
- mDNS (`http://fluidnc.local`), OTA and notifications start on Ethernet
- `$Ethernet/Setup=IP=... MSK=... GW=...` and `$Sta/Setup=...` parse correctly

plus dpCREATOR additions:

- stock FluidNC WebUI in the filesystem image; the dpCREATOR WebUI (3D toolpath
  view, job recovery, Z adjust, history) is a separate product installed on the
  board from dpcreator.eu. The commands below work from any sender or console.
- `$Board/ID`: the board's chip ID and the level of the dpCREATOR commands
  (`[MSG:BoardID:<12 hex>:api=2]`)
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
- **Laser jobs in the previews**: the laser power travels with the toolpath,
  so the pendant and the WebUI show the burn (section 8)
- **Homing state** for the WebUI and the pendant (`$Homed`, section 15)
- **Router and laser on one machine**: a laser (or knife) selected by its own
  tool number, with its offset from the router applied automatically
  (section 16)

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
[**w5500-latest**](https://github.com/val218/dpcreator-fluidnc/releases/tag/w5500-latest)
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

1. Download [`tools/windows/dpcreator-sd-drive.cmd`](https://raw.githubusercontent.com/val218/dpcreator-fluidnc/fluidnc-4.1.1-w5500/tools/windows/dpcreator-sd-drive.cmd)
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
- If the file is uploaded again while its preview is being sent to the
  pendant, the new preview is built and sent when it is ready (the pendant
  used to wait for the rest of the old one).
- While a preview the pendant asked for is being built, the pendant shows the
  build progress (it no longer gives up with *preview stream broken*).
- The pendant's copy is kept on the SD card (`/sd/.dpc/pv/`): the next
  Prepare of the same file starts sending at once. It is rebuilt with the
  `.viz`.
- Pendant firmware from cd36a8e on asks for compact points (API level 3):
  about a third of the bytes of the text points, so previews arrive about 3×
  faster.

### Laser jobs

A file is taken as a **laser job** when, with the spindle/laser on, it uses
**M4**, or all its cuts are at one Z height and it uses more than two
different S values (a grey-scale engraving). For a laser job every preview
point also carries the **power** of the move, in % of the file's highest S:

- the pendant's PART view shows the burn (plywood darkening) instead of a
  carved block, and its override pill reads **LASER**;
- the WebUI shows the burn in the Part view and the laser power in the run
  bar (section 15), also when the job is loaded from its `.viz`;
- grey-scale rasters change power on almost every move: their preview may
  hold 16000 points (8000 for other jobs), and the pendant gets moves merged
  with their average power, like a picture scaled down.

A laser file with only one power level (a plain vector cut) is not detected
and shows as a normal toolpath. Previews from older firmware are rebuilt once
by themselves (format `v6`).

M4 (dynamic laser power) needs a `Laser:` spindle in config.yaml (or a
spindle with a direction pin). On a machine with only a router spindle M4
stops the job with *error 20 (Unsupported GCode command)*: use M3, or set up
the laser as in section 16.

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

**Calibrate axes…** (in Tools) corrects steps per mm:

1. Pick the axis and a distance (1 / 10 / 100 mm or your own; A in degrees)
   and the feed. Mark the start, press **Move +** or **Move −** (a jog, only
   when Idle). **Back to start** returns.
2. Enter the distance you measured: the page shows the error (mm and %) and the
   new value = current × commanded ÷ measured, also per 10 / 100 mm and per
   motor turn when you give the mm per turn.
3. **Apply (try)** sets it now (`$/axes/<axis>/steps_per_mm=...`, not saved);
   move and measure again — it should now come out right. **Revert** puts back
   the value from when the window opened. Changing it shifts the machine
   position: home again before cutting.
4. **Save to config.yaml** changes only the `steps_per_mm:` line of that axis
   in the active config file on the flash (comments and the rest stay); the old
   file is kept as `config.yaml.bak`.

*From the hardware* gives the starting value: motor steps per turn ×
microstepping ÷ mm per motor turn (screw lead, or pulley teeth × belt pitch);
the page warns when the measured value is more than 10 % away from it (wrong
microstepping, pulley or lead). Use the longest distance you can measure well
and approach both marks from the same direction (backlash).

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

## 15. WebUI: position, 3D view, laser

**Position panel**

- Two columns: **Work** (the job's coordinates) and **Machine** (from the
  homing position). The chip above them shows **Not homed · Home** while an
  axis that has homing has not homed since power-up or a reset (click it to
  home). Machine positions only mean "where on the bed" after homing; work
  positions are always right. Unlock (`$X`) does not count as homing.
  `$Homed` answers `[MSG:HomedAxes:<homed>:of=<axes with homing>]`.
- **Zero an axis by holding** its row (or X0 / Y0 / Z0) for about 0.7 s; a tap
  does nothing, so a stray click cannot move the work zero. **Zero all** works
  the same way.
- **Probe Z0** (under the position): touch plate or 3D probe. Put the probe a
  few mm above the part and test it (push the stylus or touch the plate to the
  bit: the light turns green), enter the plate thickness, then **Probe**: the
  tool touches, Z0 is set at the plate's top surface (+ your Z offset) and Z lifts 5 mm.

**3D view while a job runs**

- The spindle (or the laser module) moves over the bed; the finished cuts are
  coloured by depth; a progress card shows the file, time left, % and line.
  **Follow** keeps the tool in view; **Auto-fit** keeps the job in view when
  it loads or the work zero moves; **Spindle view** shows the spindle body or
  only the bit; **Stock** shows the stock block (its size: **Stock** above).
- The machine bed and travel box, and the warning that a job leaves the
  travel, are only drawn when X and Y have homing and `soft_limits: true` in
  config.yaml and the machine is homed: then the numbers are real. Otherwise
  the view shows the plain grid at the bottom of the work.
- **Part** (next to the view buttons) shows the part the job makes: the
  toolpath carved out of the block with the bit in the tool column (flat,
  ball, V…), carved live while the job runs. The **Path** switch then draws
  the toolpath faintly over the part. Laser jobs show the burn instead, with
  the beam on while it fires.
- **Run bar** under the view: Cycle start / Pause / Stop, mist and flood, and
  feed and spindle overrides as sliders. For a laser job it reads **Laser**
  and shows the power as % of the top S in config.yaml, with the S value
  (e.g. *3% S600* when the spindle's top is 18000): the laser is detected from
  the selected spindle (section 16), or from the loaded job when config.yaml
  has no laser.
- **Tool column** (left of the view): the tool table; click a tool to change
  to it (M6). A laser on its own tool number (section 16) shows as
  *T2 laser*.

**Studio and the WebUI in the browser**

The **Studio** button opens dpCREATOR Studio in its own tab; from Studio,
sending a job to the machine switches back to the WebUI tab instead of
opening another one, and a second click on Studio goes to the tab that is
already open.

## 16. Router and laser on one machine

A laser module (or a drag knife, a pen…) next to the router can be its own
**spindle**, selected with its own tool number, with no tool changer. The
board switches the output, stops the other one and applies the laser's
offset from the router, so **one work zero serves both**.

### config.yaml

Router on an RS485 inverter, laser on a PWM output (the cleanest set-up):

```yaml
uart1:
  txd_pin: gpio.xx
  rxd_pin: gpio.yy
  rts_pin: gpio.zz          # RS485 driver enable
  baud: 9600
  mode: 8N1

Huanyang:                   # or H2A, YL620, ModbusVFD ...
  uart_num: 1
  modbus_id: 1
  tool_num: 0               # every tool not listed by another spindle
  speed_map: 0=0% 24000=100%

Laser:
  pwm_hz: 5000
  output_pin: gpio.ww       # laser PWM / TTL input
  enable_pin: NO_PIN
  tools: 2                  # M6 T2 selects the laser
  offset_mm: 42.5 18 0      # laser spot from the router bit: X Y Z
  speed_map: 0=0% 1000=100%
```

Router on a 0-10V inverter (the R3's 0-10V output, the relay switching the
inverter's FWD/run input):

```yaml
10V:
  output_pin: gpio.xx       # into the 0-10V converter
  forward_pin: gpio.yy      # relay -> inverter FWD / run
  tool_num: 0
  spinup_ms: 3000
  spindown_ms: 3000
  speed_map: 0=0% 24000=100%
```

plus a `Laser:` section as above **on its own GPIO**. Two spindles cannot use
the same output pin (FluidNC stops with *pin already used*), so on a board
whose PWM and 0-10V share one GPIO the laser needs another free output.
Routing one signal to either the inverter or the laser through a relay is not
supported: on the R3 the single relay is needed for the inverter's run input.

**On the dpCREATOR R3** there is no spare GPIO. Two set-ups work:

1. **Recommended: router on an RS485 inverter, laser on the PWM output.**
   The PWM output (`gpio.21`) and the relay (`gpio.39`) are free for the
   laser: PWM = power, relay = laser enable (or air assist). Any A axis stays.

   ```yaml
   Huanyang:                 # uart1 / RS485 wiring: see the Spindle and VFD page
     uart_num: 1
     modbus_id: 1
     tool_num: 0
     speed_map: 0=0% 24000=100%
   Laser:
     pwm_hz: 5000
     output_pin: gpio.21
     enable_pin: gpio.39
     tools: 2
     offset_mm: 42.5 18 0
     speed_map: 0=0% 1000=100%
   ```

2. **Possible: router on a 0-10V inverter, laser on the A-axis STEP output.**
   Only without an A axis. The 0-10V output uses `gpio.21`, the relay
   (`gpio.39`) switches the inverter's FWD/run input, and the laser takes the
   A-axis STEP output (`gpio.17`, 5 V logic, which suits the TTL/PWM input of
   most diode lasers). Remove the `a:` axis from config.yaml.

   ```yaml
   10V:
     output_pin: gpio.21
     forward_pin: gpio.39
     tool_num: 0
     spinup_ms: 3000
     spindown_ms: 3000
     speed_map: 0=0% 24000=100%
   Laser:
     pwm_hz: 5000
     output_pin: gpio.17     # A-axis STEP output (no A axis)
     tools: 2
     offset_mm: 42.5 18 0
     speed_map: 0=0% 1000=100%
   ```

   The STEP output goes through the board's 3.3 V → 5 V level shifter. If that
   stage inverts the signal, use `gpio.17:low` so S0 means laser off, and
   check with a meter or a scope before connecting the laser.

With an A axis (e.g. a rotary) and a 0-10V inverter there is no output left
for the laser: use set-up 1.

- `tools:` - the exact tool numbers that select this spindle (`2`, or `2 9`).
  A spindle with a `tools:` list is left out of the `tool_num` ranges, so T1,
  T3, T4... stay on the router. `tool_num` ranges work as in stock FluidNC.
- `offset_mm:` - where this spindle's tool point is relative to the router's
  bit, X Y Z in mm: `42.5 18 0` = 42.5 mm to the right (+X) and 18 mm behind
  (+Y). Z: how far the laser's focus point is above the bit's tip (negative
  when it is below).

### Measuring the offset

1. With the router: set a work zero, then cut or draw a small cross at a
   known spot, e.g. a 0.5 mm deep cross at X50 Y50.
2. Note the machine position (Machine column) with the bit exactly over the
   cross centre.
3. Switch to the laser, jog until the laser's spot (low power or the
   pointer) is exactly on the cross centre, and note the machine position.
4. `offset_mm` X = bit X − laser X, Y = bit Y − laser Y (the bit's position
   minus the laser's). Z the same way: offset Z = (machine Z with the bit
   touching the work surface) − (machine Z with the laser focused on it).
5. Put it in config.yaml, restart, switch to T2 and check: X50 Y50 now puts
   the laser spot on the cross.

### Using it

- **Switch**: `M6 T2` in a file or the console, the *T2 laser* button in the
  WebUI's tool column, or the WebUI tool table. The router stops, the laser is
  selected and the work position jumps by the offset (it now shows where the
  laser spot is); nothing is measured. `M6 T1` (or any router tool) switches
  back; with a tool setter (`atc_manual`) the router tool is measured as usual.
- **Jobs**: put `M6 T2` at the start of a laser file (most laser programs let
  you add a header), `M6 T1` at the start of a router file.
- The offset is kept in the tool offset (`G43.1`): a measured tool length
  stays, `G49` clears the tool length but keeps the laser's offset, and after
  a reset the selected spindle's offset is put back. An explicit `G43.1` value
  is kept as given.
- `$Spindle` answers `[MSG:Spindle:<name>:laser|spindle:<x>:<y>:<z>:<tools>]`
  and the same line is sent to everyone on every switch; the WebUI and the
  pendant use it (LASER on the pendant's override pill).
- `Laser:` spindles are in laser mode: M4 power follows the speed, as in stock
  FluidNC.

## 17. Troubleshooting

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
| Job stops at once with *error 20 (Unsupported GCode command)* at the `M4` line | M4 needs a laser spindle: use M3, or add a `Laser:` spindle (section 16). |
| Job stops with *error 14 (Line too long)* | A line (often a long comment) is longer than the controller accepts: shorten it. |
| Pendant keeps showing *loading* for a file just uploaded | Fixed in builds from 4acd654b on: a preview rebuilt while it was being sent is sent again. |
| Board stops answering (WebUI, pendant, serial) after the pendant was plugged in / rebooted | Fixed: XON/XOFF is off on the pendant UART — a noise byte that looked like XOFF paused the board's transmitter and blocked it. |
| Hold / door / reset when the pendant is plugged in or rebooted | Fixed: line noise on the pendant UART is dropped. `$Pendant/Debug` shows how many bytes were dropped. |
| WebUI goes offline / online or jerks while jogging | Fixed in builds from 5e5b8f2 on (with the pendant from 43f1a20 on): the pendant streams the handwheel as smooth jogs, state-change reports are spaced to at most one per 50 ms, and a busy WebSocket waits instead of being closed. If it still happens, the network link itself is slow: try a lower Ethernet SPI frequency in `config.yaml`. |
| WebUI shows *"Controller restarted after a crash"* | Press **Copy details** and send the text: the reason (panic, task / interrupt watchdog), the crash backtrace and the last commands sent before it (`$Crash` prints them too). After a **task watchdog** reset (reason `task-watchdog`: a task stopped responding for 5 s) the details also name the task that missed the watchdog, what each watched task was doing then (polling task, protocol task, network task, with the HTTP request in progress) and the call stack of the task that was waiting - `$Crash` prints these as `[MSG:CrashWdt:...]`, the boot log as `Watchdog: ...`. To check that this works on your board, send `$Crash/TestWatchdog` while Idle: the board waits 8 s on purpose, restarts, and the notice then names that command. **Dismiss** (or `$Crash/Clear`) tells the board it was seen: the notice goes away in every open WebUI and stays away after reloads, until the next crash. One crash restarts with your `config.yaml`; only 3 crash restarts in a row fall back to the default config (WiFi AP `FluidNC`, no Ethernet) - reset the board to try again. |
| Boot shows *"Showing startup log from previous panic"* | Capture serial output at 115200 including `Guru Meditation` and `Backtrace:` lines and decode via **Actions → Run workflow** with the backtrace input. |

Windows has no telnet client by default; test the port with PowerShell:

```powershell
Test-NetConnection 192.168.10.102 -Port 23
```

or use PuTTY (connection type **Raw**), type `?` + Enter → `<Idle|MPos:...>`.

### Useful commands

```
$Homed                  axes homed since power-up: [MSG:HomedAxes:...]
$Spindle                selected spindle (router / laser) and its offset
$Ethernet/Status        link, IP, mask, gateway, MAC
$Ethernet/Setup         show static IP settings
$Ethernet/Init          bring up the W5500 manually (test)
$network/type           WiFi | Ethernet
$Telnet/Port            Telnet (gSender) port
[ESP800]json=yes        WebSocketIP should be the Ethernet IP
```
