# Miata NA — Bluetooth / MP3 Head Unit

An ESP32-based replacement head-unit module for a Mazda Miata NA. It keeps the
factory radio and amplifier, and adds Bluetooth audio, local MP3 playback from
an SD card, and an AUX input, switched by relays and shown on a small
14-segment-style TFT display.

The project has three parts, each in its own folder:

| Folder | What's in it |
|--------|--------------|
| [`firmware/`](firmware/) | The two ESP32 firmwares (primary ESP32-S3 + Bluetooth ESP32-WROOM), the TFT_eSPI setup, and standalone test sketches. See [`firmware/README.md`](firmware/README.md) for the full build and wiring guide. |
| [`hardware/`](hardware/) | KiCad schematic (`kicad/`) and an exported schematic PDF (`pdf/`). PCB layout and fabrication are done elsewhere and not kept here. |
| [`mechanical/`](mechanical/) | 3D-printed enclosure: printable `stl/` files and editable CAD `source/`. |

## Overview

- **Two ESP32s.** The primary ESP32-S3 runs the display, keypad, relays and
  local MP3/SD playback. A secondary ESP32-WROOM handles Bluetooth A2DP
  reception, because the S3 has no Classic Bluetooth. They talk over UART.
- **Four inputs**, cycled with the EJECT button: factory Radio, Bluetooth, MP3
  (SD card), and AUX-IN. Relays route the selected source to the amplifier.
- **Display**: ST7789 170×320 TFT, landscape, showing input and scrolling
  artist/title.

A block diagram and the full feature list are in
[`firmware/README.md`](firmware/README.md).

## Status

- Firmware: working (see [`firmware/CHANGELOG.md`](firmware/CHANGELOG.md), latest **v3.8**).
- Hardware: KiCad schematic in `hardware/` (PCB done elsewhere).
- Enclosure: STL / CAD to be added under `mechanical/`.

## Licenses

- **Firmware** (`firmware/`): GPL-3.0-or-later — see [`LICENSE`](LICENSE).
- **Hardware** (`hardware/`) and **enclosure** (`mechanical/`):
  CERN-OHL-P v2 — see [`hardware/LICENSE`](hardware/LICENSE).
- The DSEG14 Classic font bundled with the firmware is by Keshikan, under the
  SIL Open Font License 1.1 (not covered by the GPL).

See [`NOTICE`](NOTICE) for the full breakdown of licenses and third-party
components.

This is a personal project shared as-is. It controls a car's audio and
connects to the factory wiring; build and install it at your own risk.
