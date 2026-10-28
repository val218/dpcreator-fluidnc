# dpCREATOR FluidNC

FluidNC 4.1.1 for ESP32-S3 controllers, extended into a complete machine
system for the **dpCREATOR WebUI** and the **TabUI pendant**: job recovery,
live Z adjust, handwheel rewind, pendant previews, macros, job history and
crash diagnostics, kept by the board itself. W5500 Ethernet is included too,
but it is no longer the main point (upstream FluidNC is adding it).

**Pick and place (beta):** [PNP_BETA.md](PNP_BETA.md) — R3 as a PnP controller, no camera yet

Full guide: **[W5500_ETHERNET.md](W5500_ETHERNET.md)** · Download:
[latest release](https://github.com/val218/dpcreator-fluidnc/releases/tag/w5500-latest)

> - **[Quick start: turn on Ethernet](W5500_ETHERNET.md#quick-start-turn-on-ethernet)**
> - [Get the firmware](W5500_ETHERNET.md#1-get-the-firmware) · [Wiring](W5500_ETHERNET.md#2-wiring) · [config.yaml](W5500_ETHERNET.md#3-configyaml) · [Ethernet setup](W5500_ETHERNET.md#4-switch-fluidnc-to-ethernet)
> - [Connect gSender over Ethernet](W5500_ETHERNET.md#5-connect-gsender-over-ethernet)
> - [SD card as a network drive + automatic pendant .viz](W5500_ETHERNET.md#6-sd-card-as-a-network-drive-windows)
> - [Pendant connection status](W5500_ETHERNET.md#7-pendant-connection-status)
> - [Job preview on the pendant: Prepare → Run](W5500_ETHERNET.md#8-job-preview-on-the-tabui-pendant) · [Job recovery](W5500_ETHERNET.md#9-job-recovery) · [Live Z adjust](W5500_ETHERNET.md#10-live-z-adjust) · [Handwheel rewind](W5500_ETHERNET.md#11-handwheel-rewind) · [Macros](W5500_ETHERNET.md#12-macros) · [Offsets & tools](W5500_ETHERNET.md#13-work-offsets-and-tools) · [G-code view & history](W5500_ETHERNET.md#14-g-code-view-run-from-line-job-history) · [Network security & login](W5500_ETHERNET.md#17-network-security-login-wi-fi-updates) · [Inputs, outputs & Modbus I/O](W5500_ETHERNET.md#18-inputs-outputs-and-modbus-io-dpcreator-motion) · [Troubleshooting](W5500_ETHERNET.md#19-troubleshooting)
> - The **dpCREATOR WebUI** (3D view, job recovery, Z adjust, history) is a separate product: install it on the board from [dpcreator.eu](https://dpcreator.eu/). This firmware ships with the stock FluidNC WebUI and works with any FluidNC sender. `$Board/ID` reports the board's chip ID.

## What this firmware adds to FluidNC 4.1.1

The dpCREATOR WebUI and the TabUI pendant use these board commands; with stock
FluidNC the pendant only jogs and the dpCREATOR WebUI does not work. All of
them also work from any sender or the console.

| Feature | What it does | Commands | Guide |
| --- | --- | --- | --- |
| Job recovery | An interrupted job (alarm, E-stop, Stop, reset, power loss) is kept by the board and offered for restart from a safe line, with a dry run; any file can also be started from a chosen line | `$Job/Recovery`, `$Job/RunFrom=` | [9](W5500_ETHERNET.md#9-job-recovery) |
| Live Z adjust | Raise or lower the tool while the job cuts, applied within milliseconds; ends with the job, can be added to the work zero | `$Job/ZAdjust` | [10](W5500_ETHERNET.md#10-live-z-adjust) |
| Handwheel rewind | In Hold, run back along the cut path, change Z, then Continue re-cuts to the hold point | `$Rewind/...` | [11](W5500_ETHERNET.md#11-handwheel-rewind) |
| Pendant integration | Prepare a file and see it on the pendant, then Run or Cancel from either side; toolpath previews built and sent by the board; link status | `$Job/Prepare=`, `$Viz/...`, `$Pendant/Status` | [7](W5500_ETHERNET.md#7-pendant-connection-status), [8](W5500_ETHERNET.md#8-job-preview-on-the-tabui-pendant) |
| Macro library | Macros on the SD card, shared by the WebUI and the pendant | `$MacroLib/...` | [12](W5500_ETHERNET.md#12-macros) |
| Job history and hour counters | Every job with its result and time; run, spindle and service hours | `$Stats` | [14](W5500_ETHERNET.md#14-g-code-view-run-from-line-job-history) |
| SD card as a network drive | Map the SD card as a drive on your PC; pendant previews are built for every G-code file copied onto it | WebDAV at `/sd` | [6](W5500_ETHERNET.md#6-sd-card-as-a-network-drive-windows) |
| Crash diagnostics | The crash backtrace survives the restart; a watchdog reset names the stuck task and its call stack; the last commands are kept; one crash no longer drops your `config.yaml` (only 3 in a row do) | `$Crash`, `$Crash/Clear`, `$Crash/TestWatchdog` | [15](W5500_ETHERNET.md#19-troubleshooting) |
| Octal PSRAM | The 8 MB octal PSRAM of N8R8 / N16R8 modules is used; stock S3 builds leave it off | – | – |
| W5500 Ethernet | Wired network, with the IP, hostname, mDNS and OTA working on Ethernet | `$Ethernet/Setup=` | [Quick start](W5500_ETHERNET.md#quick-start-turn-on-ethernet) |
| Laser previews | Laser jobs carry their power in the toolpath preview: the pendant and the WebUI show the burn and the laser power | – | [8](W5500_ETHERNET.md#laser-jobs) |
| Router and laser | A laser (or knife) next to the router selected by its own tool number (`tools: 2`), its offset from the router applied while it is selected (`offset_mm:`), so one work zero serves both | `M6 T<n>`, `$Spindle` | [16](W5500_ETHERNET.md#16-router-and-laser-on-one-machine) |
| Homing state | Which axes homed since power-up, for the WebUI's *Not homed* chip and machine-position checks | `$Homed` | [15](W5500_ETHERNET.md#15-webui-position-3d-view-laser) |
| Board ID | Chip ID and the level of the dpCREATOR commands | `$Board/ID` | – |

It also fixes problems found on the real machine: a deadlock between the
network task and the polling task when a browser connected (watchdog
restarts), UART writes that blocked every channel while the pendant was busy,
status-report floods while jogging that disconnected the WebUI, and preview
transfers that overran the pendant.

## Compatible boards

The firmware needs an **ESP32-S3 with 8 MB or more of flash** and, for the
pendant, a free UART (TX/RX) plus one input for its E-stop line. WiFi is
enough; Ethernet (W5500) is optional. Each board needs its own `config.yaml`
with its pins. Only the dpCREATOR boards are tested; the rest is from the
makers' published specs.

| Board | Module | Runs this build | Notes |
| --- | --- | --- | --- |
| dpCREATOR R2 / R3 | ESP32-S3-WROOM-1-N16R8 | Yes, tested | Reference board: CNC I/O, W5500, SD, pendant port |
| ESP32-S3-DevKitC-1 N8R8 / N16R8 and clones | N8R8 / N16R8 | Yes | Bare dev board: add drivers, inputs, SD. GPIO 35-37 belong to the PSRAM |
| [PiBot V5.77](https://www.tindie.com/products/pibot/pibot-v577-carrier-esp32-s3-62-axis-cnc/) / [V5.88](https://www.tindie.com/products/pibot/pibot-v588-ultra-esp32-s3-61-axis-cnc/) | plug-in ESP32-S3 module | Yes, with an N8R8 / N16R8 module | 6-8 axes for external drivers, microSD |
| [Waveshare ESP32-S3-ETH](https://www.waveshare.com/wiki/ESP32-S3-ETH) | ESP32-S3R8, 16 MB flash | Yes, over WiFi | Its SD card and W5500 use separate SPI pins; this firmware expects them on one bus, so Ethernet needs a change |
| [LilyGO T-ETH-Lite S3](https://wiki.lilygo.cc/products/t-eth-series/t-eth-lite-s3/) | 16 MB flash, 8 MB octal PSRAM | Yes, over WiFi | Check its SD / W5500 pins for the same reason |
| [Doberman](https://www.elecrow.com/doberman-esp32-s3-8-axis-cnc-controller.html) | ESP32-S3-WROOM-1U-N8R8 | Not yet | Its I/O uses pins the octal PSRAM needs: needs a build without octal PSRAM (planned) |
| ESP32-S3 with quad PSRAM (N8R2, N16R2) or none (N8, N16) | | Runs, without PSRAM | Works on internal RAM only, with less headroom; a quad-PSRAM build is planned |
| ESP32-S3 with 4 MB flash (N4, N4R2) | | No | The firmware and file system need 8 MB |
| Original ESP32 boards (6x CNC Controller, 6-Pack, Jackpot, MKS DLC32) | ESP32-WROOM-32 | No | Not an ESP32-S3 |

<img src="https://github.com/bdring/FluidNC/wiki/images/logos/FluidNC.svg" width="600">

## Introduction

**FluidNC** is a CNC firmware optimized for the ESP32 controller. It is the next generation of firmware from the creators of Grbl_ESP32. It includes a web based UI and the flexibility to operate a wide variety of machine types. This includes the ability to control machines with multiple tool types such as laser plus spindle or a tool changer.  

## Firmware Architecture

- Object-Oriented hierarchical design
- Hardware abstraction for machine features like spindles, motors, and stepper drivers
- Extensible - Adding new features is much easier for the firmware as well as gcode senders.

## Machine Definition Method

There is no need to compile the firmware. You use an installation script to upload the latest release of the firmware and then create [config file](http://wiki.fluidnc.com/en/config/overview) text file that describes your machine.  That file is uploaded to the FLASH on the ESP32 using the USB/Serial port or WIFI.

You can have multiple config files stored on the ESP32. The default is config.yaml, but you can change that with [**$Config/Filename=<myOtherConfig.yaml>**](http://wiki.fluidnc.com/en/features/commands_and_settings#config_filename)

### AI Config File Helpers

Writing a config file by hand (or with the help of an AI assistant) can be error-prone given how many sections and fields FluidNC supports. The [`tools/`](tools/) directory has a formal spec, a JSON Schema, a command-line validator, and an MCP server that an AI assistant can call directly to check its own work. See [AI Config File Helpers](http://wiki.fluidnc.com/en/config/overview#ai-config-file-helpers) on the wiki for details.

## Basic Grbl Compatibility

The intent is to maintain as much Grbl compatibility as possible. It is 100% compatible with the day to day operations of running gcode with a sender, so there is no change to the Grbl gcode send/response protocol, and all Grbl gcode are supported. Most of the $ settings have been replaced with easily readable items in the config file.


## WebUI

FluidNC includes a built-in browser-based Web UI (Esp32_WebUI) so you control the machine from a PC, phone, or tablet on the same Wifi network.

## Wiki

[Check out the wiki](http://wiki.fluidnc.com) if you want the learn more about the feature or how to use it.

## Credits

The original [Grbl](https://github.com/gnea/grbl) is an awesome project by Sungeon (Sonny) Jeon. I have known him for many years and he is always very helpful. I have used Grbl on many projects.

The Wifi and WebUI is based on [this project.](https://github.com/luc-github/ESP3D-WEBUI)  

## Discussion

<img src="http://wiki.fluidnc.com/discord-logo_trans.png" width="180">

We have a Discord server for the development this project. Ask for an invite


## Donations

This project requires a lot of work and often expensive items for testing. Please consider a safe, secure and highly appreciated donation via the PayPal link below or via the GitHub sponsor link at the top of the page.

[![](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/donate/?hosted_button_id=8DYLB6ZYYDG7Y)
