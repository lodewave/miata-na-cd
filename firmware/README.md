# Miata NA — Bluetooth Music Receiver / Input Switcher

ESP32-based head unit replacement for a Mazda Miata NA. Routes audio between
four sources (factory radio, Bluetooth A2DP, AUX-IN, and SD card MP3) via
relay switching, and displays track metadata on a 14-segment-style TFT screen.

Uses a **two-ESP32 architecture**:
- **Primary ESP32-S3**: Handles display, keypad, relays, and local MP3/SD card playback.
- **Secondary ESP32-WROOM-32E**: Handles Bluetooth A2DP audio reception only (Classic BT
  is not supported on ESP32-S3).

The two ESP32s communicate over UART.

```
┌─────────────────────────────────────────────────────────────┐
│                PRIMARY ESP32-S3                              │
│                                                             │
│  I2S ──────► PCM5102A DAC ──────► Relay ──► Amp ──► Spkrs   │
│  (MP3 playback from local SD card)                          │
│                                                             │
│  SPI (HSPI)► SD Card (MP3 files, FAT32)                     │
│                                                             │
│  SPI (FSPI)► ST7789 TFT Display (320×170, 118px visible)    │
│                                                             │
│  I2C ──────► MCP23017 GPIO Expander                         │
│              ├── Keypad Matrix (2×4, 7 buttons)             │
│              ├── Direct Buttons (PIN, EJECT)                 │
│              └── Relay Outputs (Radio_RELAY, DAC_RELAY)      │
│                                                             │
│  UART1 ────► BT ESP32 (commands + metadata)                 │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│               SECONDARY ESP32-WROOM (BT only)               │
│                                                             │
│  BT A2DP ◄── Phone / Audio Source ("Miata NA")              │
│  I2S ──────► PCM5102A DAC ──────► Relay ──► Amp ──► Spkrs   │
│  UART2 ◄──► Primary ESP32-S3 (commands + metadata)          │
└─────────────────────────────────────────────────────────────┘

Audio Routing:
  Radio ─────────────┐
                     Radio_RELAY ──────► Output (to amp)
  DAC ────┐          │
          DAC_RELAY ─┘
  AUX-IN ─┘

Note: Both BT and MP3 share the same DAC path (Radio_RELAY=HIGH, DAC_RELAY=LOW).
The BT ESP32's DAC and the Primary's MP3 DAC outputs are wired to the
same relay input. Only one is active at a time (managed by input state).
```

---

## Input States

Press **EJECT** to cycle: Radio → BT → MP3 → AUX → Radio… (BT, MP3 and AUX
are skipped when disabled in the settings menu).

Device starts in **Radio** mode on power-up.

| State | Display | Radio_RELAY (GPB1) | DAC_RELAY (GPB0) | I2S_SWITCH (GPIO8) | MODULE_ACTIVE (GPB3) |
|-------|---------|:------------------:|:----------------:|:------------------:|:--------------------:|
| Radio | `RADIO` / `INPUT` | LOW | LOW | LOW | LOW |
| Bluetooth | Splash → Artist / Title (scrolling) | HIGH | LOW | HIGH | HIGH |
| MP3 | Splash → Artist / Title (scrolling) | HIGH | LOW | LOW | HIGH |
| AUX-IN | `AUXILIARY` / `INPUT` | HIGH | HIGH | LOW | HIGH |

**Relay Logic** (`switchRelaysMuted()` in `actions.ino`):
- Radio_RELAY selects Radio (LOW) vs the output of DAC_RELAY (HIGH)
- DAC_RELAY selects DAC (LOW) vs AUX-IN (HIGH)
- Both BT and MP3 use Radio_RELAY=HIGH, DAC_RELAY=LOW (DAC path)
- When a relay changes: AMP_MUTE (GPB2) HIGH and MODULE_ACTIVE (GPB3) to its
  new level in the same Port B write → 100 ms → switch relay(s),
  20 ms apart if both change (Radio_RELAY first) → 200 ms → AMP_MUTE LOW.
  No muting when no relay changes (BT ↔ MP3). Blocks for 300 ms (one relay)
  or 320 ms (both)

| Switch | Relays changed |
|--------|----------------|
| Radio → BT, Radio → MP3 | Radio_RELAY |
| BT / MP3 → AUX | DAC_RELAY |
| AUX → Radio | both (20 ms apart) |
| BT ↔ MP3 | none (I2S switch only) |

**I2S source** (`setI2SSourceMuted()`): when I2S_SWITCH changes (into or out
of Bluetooth), DAC_MUTE (GPIO5) LOW → 5 ms → toggle I2S_SWITCH → 5 ms →
DAC_MUTE HIGH.

**RADIO_SENSE** (GPB5): read LOW continuously for 100 ms → MODULE_ACTIVE LOW
→ wait 100 ms → switch to Radio (so MODULE_ACTIVE doesn't keep the radio off).
Fires once per LOW period; it re-arms when RADIO_SENSE goes HIGH, so EJECT
still works while it stays LOW. Leaving Radio sets MODULE_ACTIVE HIGH, and
the headunit then releases RADIO_SENSE (HIGH), which re-arms it.

**Splash Screen:**
- Switching to BT shows "BLUETOOTH" / "INPUT" for 3 seconds
- Switching to MP3 shows "MP3" / "PLAYER" for 3 seconds
- After splash, shows active display (artist/title)

**Playback Behaviour:**
- BT: Pauses when switching away, resumes when switching back (if connected)
- MP3: Pauses when switching away, resumes from pause point when switching back
- MP3: Starts playing immediately when input is first selected (shuffle mode)
- MP3: ID3v2 (2.2–2.4) / ID3v1 tags parsed for artist/title (falls back to
  file name). Plays `.mp3` files (any case) in the SD root **and in folders**
  up to 4 levels deep (e.g. `/Artist/Album/CD1/01 Song.mp3`), up to 500
  files, all shuffled together. Hidden `._` files left by macOS, hidden
  folders and `System Volume Information` are skipped. Limits are in
  `mp3_files.h`

---

## Keypad Controls

Matrix buttons are active in **Bluetooth** and **MP3** modes:

| Position | Button | BT Action | MP3 Action |
|----------|--------|-----------|------------|
| (0,1) | TrackUp | Next track | Next track (shuffle) |
| (1,0) | Play | Play/Pause toggle | Play/Pause toggle |
| (1,1) | TrackDown | Previous track | Restart; if <3s, previous track |
| (2,0) | Scan | TODO | TODO |
| (2,1) | FF | TODO | TODO |
| (3,0) | RPT | TODO | TODO |
| (3,1) | REV | TODO | TODO |

Direct buttons (active in all modes):

| Pin | Button | Action |
|-----|--------|--------|
| GPA7 | PIN | Hold 1 s: open settings menu |
| GPA4 | EJECT | Cycle input state (skips AUX / MP3 if disabled) |

---

## Settings Menu

Hold **PIN** for 1 s to open. **TrackUp / TrackDown** move, **FF / REV**
change the selected value, **PIN** saves and exits. Other buttons are ignored
while the menu is open; audio keeps playing.

| Item | Default | Step (FF / REV) | Range |
|------|---------|-----------------|-------|
| MP3 Volume | −12.0 dBFS (8231) | 0.5 dB | −60 … 0 dBFS; peak sample / 32767 in parentheses |
| Button Backlight | 70% | 1% | 0–100% |
| LCD Backlight - Day | 90% | 1% | 5–100% |
| LCD Backlight - ILLUM | 50% | 1% | 5–100% |
| BT Enable | ON | toggle | OFF also turns Bluetooth off (module hidden from phones) |
| BT Auto-Reconnect | ON | toggle | BT ESP32 connects to the last phone at power-up |
| MP3 Enable | ON | toggle | — |
| Aux Enable | ON | toggle | — |
| Scroll Speed | 1.5 s | 0.1 s | 0.2–5.0 s per character |
| Function Test | — | FF or REV starts it | hold PIN 1 s to exit (restarts) |

Settings are stored in NVS flash (Preferences, namespace `miata`) and survive
power loss and firmware uploads. Only changed values are written.

**Bluetooth settings** are also stored on the BT ESP32 (namespace `miata_bt`),
so they apply from its power-up without waiting for the primary. The primary
sends them with `BT_CONFIG` at boot, on `BT_READY` and when the menu closes.

- **BT Enable** OFF: EJECT skips BT and the BT ESP32 doesn't start Bluetooth.
  Changing it restarts the BT ESP32 (about 1 s).
- **BT Auto-Reconnect** ON: at power-up the BT ESP32 connects to the last
  phone itself, so you don't have to pick "Miata NA" on the phone. The phone
  is remembered each time it connects, and when the setting is turned on
  while it's connected. If that phone isn't around, other paired phones can
  still connect after two failed attempts. Turning it off stops any retries
  straight away.

**Backlights** (`lighting.ino`) follow ILLUM SENSE (GPB4) and HU LIGHTSWITCH
(GPB6). Levels below are as **read by the ESP32**; the headunit sends the
opposite (reverse logic, see the MCP23017 pin table):

| ILLUM SENSE (GPB4) | HU LIGHTSWITCH (GPB6) | Button backlight (GPIO4) | Display backlight (GPIO9) |
|:------------------:|:---------------------:|--------------------------|---------------------------|
| HIGH | HIGH | OFF | ON at LCD Backlight - Day |
| HIGH | LOW | OFF | ON at LCD Backlight - Day |
| LOW | HIGH | ON at Button Backlight | ON at LCD Backlight - ILLUM |
| LOW | LOW | OFF | OFF |

While the menu is open, the backlight being edited shows its value, and the LCD never
goes fully off.

---

## Wiring

### Primary ESP32-S3 Pin Assignments (ESP32-S3-WROOM-1-N8)

**I2S — MP3 Audio Output (to PCM5102A DAC)**

| GPIO | Function | Connected To |
|------|----------|--------------|
| 6 | I2S_BCK | PCM5102A BCK (via I2S switch) |
| 15 | I2S_WS | PCM5102A LRCK (via I2S switch) |
| 7 | I2S_DATA_OUT | PCM5102A DIN (via I2S switch) |

No MCLK; the PCM5102A generates its own from BCK. 44.1 kHz, 16-bit stereo.

**UART1 — Communication with BT ESP32**

| GPIO | Function | Connected To |
|------|----------|--------------|
| 18 | BT_UART_TX | BT ESP32 RX (GPIO 16) |
| 17 | BT_UART_RX | BT ESP32 TX (GPIO 17) |

**UART0 — Programming / Serial Monitor**

| GPIO | Function | Connected To |
|------|----------|--------------|
| 43 | PROG_TXD (U0TXD) | USB-UART adapter RX |
| 44 | PROG_RXD (U0RXD) | USB-UART adapter TX |

**USB (native USB-OTG)**

| GPIO | Function | Connected To |
|------|----------|--------------|
| 19 | USB_DN | USB D- |
| 20 | USB_DP | USB D+ |

**Boot / Reset Buttons**

| Pin | Function | Notes |
|-----|----------|-------|
| EN (CHIP_PU) | Reset button | Active LOW, momentary to GND |
| GPIO 0 | Boot button | Hold LOW during reset to enter download mode |

**TFT SPI Display (ST7789 170×320) — FSPI / SPI2**

| GPIO | Net | Function | Connected To |
|------|-----|----------|--------------|
| 14 | SPI1_MOSI | TFT_MOSI | Display MOSI |
| 13 | SPI1_SCLK | TFT_SCLK | Display Clock |
| 12 | SPI1_CS | TFT_CS | Display Chip Select |
| 11 | SPI1_DC | TFT_DC | Display Data/Command |
| 10 | SPI1_RST | TFT_RST | Display Reset |
| 9 | SPI1_BL | LCD_BL_PIN | Display Backlight (PWM, driven by the firmware, not TFT_eSPI) |

**SD Card SPI — SPI3 / HSPI**

| GPIO | Function | Connected To |
|------|----------|--------------|
| 40 | SD_MOSI | SD Card MOSI |
| 41 | SD_MISO | SD Card MISO |
| 39 | SD_SCLK | SD Card CLK |
| 42 | SD_CS | SD Card CS |

**I2C1 (for MCP23017 GPIO Expander)**

| GPIO | Net | Function | Connected To |
|------|-----|----------|--------------|
| 1 | I2C1SDA | I2C_SDA | MCP23017 pin 13 (SDA) (+ 4.7kΩ pullup to 3.3V) |
| 2 | I2C1SCL | I2C_SCL | MCP23017 pin 12 (SCL) (+ 4.7kΩ pullup to 3.3V) |
| 48 | I2C1INTA | MCP_INTA | MCP23017 pin 20 (INTA) — active LOW, Port A interrupt |
| 21 | I2C1INTB | MCP_INTB | MCP23017 pin 19 (INTB) — active LOW, Port B interrupt |

INTA/INTB are wired but not used by the firmware yet. The keypad and direct
buttons are polled every 10 ms with whole-port Port A reads/writes at 400 kHz
I2C (9 transfers, ~1.2 ms per scan).

**LED & Control Outputs**

| GPIO | Function | Logic | Connected To |
|------|----------|-------|--------------|
| 4 | LED1_PWM | PWM (LEDC) | LED (via current-limiting resistor) — not yet verified on new PCBA |
| 5 | DAC_MUTE | HIGH = unmute, LOW = mute | PCM5102A XSMT — HIGH, pulsed LOW 10 ms around I2S source changes |
| 8 | I2S_SWITCH | LOW = I2S1 (ESP32-S3, MP3); HIGH = I2S2 (BT ESP32) | TMUX1574 select. HIGH only while the Bluetooth input is selected |

GPIO33 is reserved as the TFT_eSPI dummy MISO (not brought out on the N8).

**Free GPIOs:** 16, 38, 47

### I2S Naming and Source Switching

The schematic names the two I2S buses:

- **I2S1** — MP3 player output from the primary ESP32-S3 (GPIO6 BCK, GPIO7 DATA, GPIO15 LRCK)
- **I2S2** — Bluetooth output from the BT ESP32 (GPIO32 BCK, GPIO14 DATA, GPIO15 LRCK)

Both go into a TMUX1574 (2:1 switch). Its common side feeds the single
PCM5102A. The primary's GPIO8 (`I2S_SWITCH`) selects the source: HIGH = I2S2
(Bluetooth input), LOW = I2S1 (MP3 player, and the idle state for Radio/AUX).
Neither ESP32 needs to release its I2S pins; the unselected side is simply
disconnected by the TMUX1574.

### Secondary ESP32-WROOM-32E (BT) Pin Assignments

**I2S2 — Bluetooth Audio Output (to PCM5102A DAC via TMUX1574)**

| GPIO | Function | Connected To |
|------|----------|--------------|
| 32 | I2S_BCK | TMUX1574 → PCM5102A BCK |
| 15 | I2S_WS | TMUX1574 → PCM5102A LRCK |
| 14 | I2S_DATA_OUT | TMUX1574 → PCM5102A DIN |

**UART2 — Communication with Primary ESP32-S3**

| GPIO | Function | Connected To |
|------|----------|--------------|
| 17 | COMM_TX | Primary ESP32-S3 RX (GPIO 17) |
| 16 | COMM_RX | Primary ESP32-S3 TX (GPIO 18) |

**UART0 — Programming / Serial Monitor**

| GPIO | Function | Connected To |
|------|----------|--------------|
| 1 | TXD0 | USB-UART adapter RX |
| 3 | RXD0 | USB-UART adapter TX |

**Boot / Reset Buttons**

| Pin | Function | Notes |
|-----|----------|-------|
| EN (CHIP_PU) | Reset button | Active LOW, momentary to GND |
| GPIO 0 | Boot button | Hold LOW during reset to enter download mode |

### Inter-ESP32 UART Connection

| Primary ESP32-S3 | BT ESP32 (WROOM) | Signal |
|-----------------|-------------------|--------|
| GPIO 18 (TX) | GPIO 16 (RX) | Commands (BT_PLAY/PAUSE/NEXT/PREV/STATUS) |
| GPIO 17 (RX) | GPIO 17 (TX) | Responses (BT_ARTIST/TITLE/CONNECTED/etc.) |
| GND | GND | Common ground |

### MCP23017 (I2C address 0x20, A0=A1=A2=GND)

| Pin | Mode | Function |
|-----|------|----------|
| GPA0 | Output | Keypad Row 3 |
| GPA1 | Output | Keypad Row 2 |
| GPA2 | Output | Keypad Row 1 |
| GPA3 | Output | Keypad Row 0 |
| GPA4 | Input pullup | BTN_EJECT (direct, wired to GND) |
| GPA5 | Input pullup | Keypad Column 1 |
| GPA6 | Input pullup | Keypad Column 0 |
| GPA7 | Input pullup | BTN_PIN (direct, wired to GND) — see note |
| GPB0 | Output | DAC_RELAY (DAC vs AUX-IN) |
| GPB1 | Output | Radio_RELAY (Radio vs DAC/AUX) |
| GPB2 | Output | AMP MUTE (HIGH = muted) — HIGH during relay switching |
| GPB3 | Output | MODULE ACTIVE — LOW on Radio, HIGH on other inputs |
| GPB4 | Input pullup | ILLUM SENSE (reverse logic, see note) |
| GPB5 | Input pullup | RADIO SENSE — LOW for 100 ms switches to Radio (reverse logic) |
| GPB6 | Input pullup | HU LIGHTSWITCH (reverse logic) |
| GPB7 | *unassigned* | — |

**Reverse logic on the headunit sense inputs (GPB4–GPB6):** the PCBA input
stage inverts ILLUM SENSE, RADIO SENSE and HU LIGHTSWITCH. When the ESP32
reads HIGH, the headunit is actually sending LOW, and a LOW read is a HIGH
from the headunit. Every HIGH/LOW in the firmware, its comments, the README
tables and the test screens is the level **read by the ESP32**.

| ESP32 reads | Headunit signal |
|:-----------:|:---------------:|
| HIGH | LOW |
| LOW | HIGH |

Note: MCP23017 datasheet Rev D (2022) lists GPA7/GPB7 as output-only. PIN is
used as an input on GPA7 per the current PCBA; if I2C glitches show up, move it
to another GPA pin on the next board revision.

All pin assignments are in `primary_esp32s3/board.h`.

### MCP23017 Support Pins

| Pin | Connection |
|-----|------------|
| VDD | 3.3V |
| VSS | GND |
| RESET | VDD (tied high) |
| A0, A1, A2 | GND |
| INTA (pin 20), INTB (pin 19) | ESP32-S3 GPIO 48 (INTA), GPIO 21 (INTB) — active LOW, not used yet |
| SDA (pin 13) | ESP32-S3 GPIO 1 + 4.7kΩ pull-up to VDD |
| SCL (pin 12) | ESP32-S3 GPIO 2 + 4.7kΩ pull-up to VDD |

### Keypad Matrix

Row scanning with diodes. Anodes on columns prevent current backflow.

```
         Col 0 (GPA6)    Col 1 (GPA5)
Row 0 (GPA3):  [unmapped]    [TrackUp]
Row 1 (GPA2):  [Play]        [TrackDown]
Row 2 (GPA1):  [Scan]        [FF]
Row 3 (GPA0):  [RPT]         [REV]
```

---

## Display

- **Panel**: ST7789, 1.9", 170×320 pixels
- **Visible window**: 118×320 pixels (26px vertical offset from physical top)
- **Orientation**: Landscape, rotation 3
- **Font**: DSEG14 Classic Regular 18pt (14-segment style)
- **Color**: `#A8C64E` yellow-green (0x54E4) on black
- **SPI bus**: FSPI (SPI2) on ESP32-S3

**Bluetooth/MP3 mode** (after splash):
- Top half: "ARTIST" label + scrolling artist name
- Divider line
- Bottom half: "TITLE" label + scrolling title

**Radio/AUX modes**: Two centered lines (e.g. "RADIO" / "INPUT").

**Rendering:** every screen is drawn into a 320×118 16-bit `TFT_eSprite`
(75.5 KB RAM) covering the visible window, then pushed to the panel, so text
and screen changes never show a blank frame. Scroll steps push only the 40 px
band that moved (~5 ms at 40 MHz); full screens take ~15 ms. If the sprite
can't be allocated at boot, the firmware falls back to drawing directly and
prints a warning.

Text scrolls at 1.5 seconds per character with a 2-second pause at the start.
Spaces are rendered as real blank spaces, one full character cell wide. The
stock DSEG14 space glyph is only 7 px wide, so its xAdvance was widened to
29 px (the width of every other character) in
`primary_esp32s3/DSEG14Classic_Regular_18.h`. Keep that edit if the font file
is ever regenerated.

---

## UART Protocol (Primary ESP32-S3 ↔ BT ESP32)

**Commands (Primary → BT ESP32):**
| Command | Action |
|---------|--------|
| `BT_PLAY\n` | AVRCP play |
| `BT_PAUSE\n` | AVRCP pause |
| `BT_PLAYPAUSE\n` | AVRCP toggle play/pause |
| `BT_NEXT\n` | AVRCP next track |
| `BT_PREV\n` | AVRCP previous track |
| `BT_STATUS\n` | Request connection status + current metadata |
| `BT_CONFIG:e,a\n` | BT Enable (`e`) and BT Auto-Reconnect (`a`), `0`/`1`. Ignored if unchanged; an `e` change restarts the BT ESP32 |

**Responses (BT ESP32 → Primary):**
| Response | Meaning |
|----------|---------|
| `BT_ARTIST:name\n` | Current artist (from AVRC metadata) |
| `BT_TITLE:name\n` | Current title (from AVRC metadata) |
| `BT_CONNECTED\n` | Phone/source connected |
| `BT_DISCONNECTED\n` | Phone/source disconnected |
| `BT_PLAYING\n` | Audio stream started (reported by the phone, also when played from the phone) |
| `BT_PAUSED\n` | Audio stream suspended / stopped |
| `BT_READY\n` | BT ESP32 booted (sent before Bluetooth starts); primary replies with `BT_CONFIG` |

Baud rate: 115200, 8N1. Lines end in `\n` (`\r` is ignored). Play/pause/next/prev
are ignored while no phone is connected. Line breaks inside metadata are sent
as spaces.

---

## File Structure

```
primary_esp32s3/                — Primary ESP32-S3 firmware
├── primary_esp32s3.ino         — Globals, setup(), loop()
├── bt_interface.ino            — UART interface to BT ESP32 (receives metadata)
├── mp3player.ino               — SD card MP3 playback, shuffle, ID3, I2S, volume
├── display.ino                 — TFT rendering (sprite), scroll logic, menu drawing
├── keypad.ino                  — MCP23017 init, button scanning, debounce, PIN hold
├── actions.ino                 — Input switching, relays, button routing by UI mode
├── lighting.ino                — Button + LCD backlights from ILLUM / LIGHTSW
├── settings.ino                — Persistent settings (Preferences / NVS)
├── settings_menu.ino           — Settings menu UI
├── config.h                    — Constants, input states, settings, declarations
├── board.h                     — All pin assignments + display window geometry
├── keypad.h                    — Keypad state shared with the function test
├── mp3_files.h                 — Which SD files are played (shared with the SD test)
├── DSEG14Classic_Regular_18.h  — Font data (18pt, space widened)
└── src/function_test/          — Built-in function test (C++, from the menu)
    ├── function_test.cpp       — Entry/exit, test switching
    ├── function_test.h         — Internal declarations
    ├── function_test_api.h     — Functions the main firmware calls
    ├── display.cpp             — Drawing helpers
    └── mode_keypad / mode_led / mode_headunit / mode_sd / mode_tone .cpp

bt_esp32/                       — Secondary BT ESP32 firmware
└── bt_esp32.ino                — A2DP sink, I2S output, UART protocol

tft_espi_setup/                 — TFT_eSPI user setup for the ESP32-S3 display
└── Setup_MiataS3_ST7789_170x320.h

test/                           — Standalone diagnostics (ESP32-S3, new PCBA)
├── keypad_mcp23017_test/       — Keypad matrix + direct buttons
├── display_test/               — TFT_eSPI bring-up + sprite demo
├── display_raw_spi_test/       — Display without TFT_eSPI
├── sd_card_test/               — SD mount, file list, read speed
├── i2s_tone_test/              — Test tones through DAC + relays
├── led_pwm_test/               — GPIO4 LED PWM set from keypad, shown on display
├── headunit_io_test/           — Headunit sense inputs + output toggles on display
└── function_test/              — Standalone all-in-one test: keypad, LEDs,
                                  headunit I/O, SD, tone (EJECT steps through)

The same function test is also built into the main firmware (settings menu →
Function Test, in `primary_esp32s3/src/function_test/`). The two are separate
copies: changes to one are not made in the other.

bt_music_receiver/              — [LEGACY] Original main ESP32 firmware
mp3_esp32/                      — [LEGACY] Original separate MP3 ESP32 firmware
```

---

## Dependencies

### Primary ESP32-S3

| Library | Author | Install Via |
|---------|--------|-------------|
| AudioTools | Phil Schatzmann | Manual |
| arduino-libhelix | Phil Schatzmann | Manual |
| TFT_eSPI | Bodmer | Arduino Library Manager |
| Adafruit MCP23017 | Adafruit | Arduino Library Manager |
| Adafruit BusIO | Adafruit | Arduino Library Manager (auto) |
| SD | Arduino (built-in) | Included with ESP32 core |

### Secondary BT ESP32 (WROOM)

| Library | Author | Install Via |
|---------|--------|-------------|
| AudioTools | Phil Schatzmann | Manual |
| ESP32-A2DP | Phil Schatzmann | Manual |

### TFT_eSPI Configuration

TFT_eSPI reads its pins from its own setup file, not from `config.h`. The
setup for this board is in the repo at
`tft_espi_setup/Setup_MiataS3_ST7789_170x320.h`.

1. Copy it to `~/Arduino/libraries/TFT_eSPI/User_Setups/`
2. Edit `TFT_eSPI/User_Setup_Select.h` so this is the only active setup include:
   ```c
   #include <User_Setups/Setup_MiataS3_ST7789_170x320.h>
   ```

The sketch takes its `TFT_*` pins from this setup file. `config.h` checks the
setup ID and pins at compile time and stops the build with an error if a
different setup is selected. If you edit the repo copy, re-copy it into the
library.

The setup defines `USE_FSPI_PORT`. This is required on the ESP32-S3 with
Arduino-ESP32 core 3.x; without it TFT_eSPI targets the flash SPI registers and
hangs or crashes in `tft.init()`.

Note: the legacy WROOM sketches in `archive/` used `Setup408_ESP32_ST7789_170x320.h`.
Switch the include back if you need to build those.

---

## Build & Upload

### Primary ESP32-S3

1. Open `primary_esp32s3/primary_esp32s3.ino` in Arduino IDE
2. Select board: **ESP32S3 Dev Module**
3. Settings:
   - Module: ESP32-S3-WROOM-1-N8 (8MB quad flash, no PSRAM)
   - Flash Size: 8MB (64Mb)
   - Flash Mode: QIO 80MHz
   - Partition Scheme: 8M with spiffs (3MB APP/1.5MB SPIFFS)
   - PSRAM: Disabled (the N8 has no PSRAM; enabling it just logs a PSRAM error at boot)
   - USB CDC On Boot: Enabled if connected via native USB (GPIO19/20);
     Disabled if using a USB-UART adapter on GPIO43/44
4. Upload

### Secondary BT ESP32 (WROOM)

1. Open `bt_esp32/bt_esp32.ino` in Arduino IDE
2. Select board: **ESP32 Dev Module**
3. Settings:
   - Module: ESP32-WROOM-32E
   - Flash Size: 4MB
   - Partition Scheme: Default (sketch uses ~84%; use Huge APP if it outgrows it)
4. Upload
5. The receiver appears as **Miata NA** when pairing

**Important:** The ESP32-S3 does NOT support Classic Bluetooth (A2DP/AVRCP).
That's why Bluetooth audio reception must remain on the ESP32-WROOM.

---

## Troubleshooting

### MCP23017 not detected
- Check I2C wiring: SDA → GPIO 1, SCL → GPIO 2 (on ESP32-S3)
- Verify 4.7kΩ pull-ups on SDA and SCL
- Verify address pins (A0, A1, A2 all to GND for 0x20)
- Verify RESET pin tied to VDD

### Display not working
- Verify `User_Setup_Select.h` includes `Setup_MiataS3_ST7789_170x320.h` (MOSI=14, SCLK=13, CS=12, DC=11, RST=10, BL=9)
- Reboot (Guru Meditation) at `tft.init()` → `USE_FSPI_PORT` missing from the setup file
- Silent hang at `tft.init()` → `TFT_MISO` missing from the setup file. On the S3,
  TFT_eSPI then sets MISO = MOSI (GPIO14), and core 3.x stops the SPI bus when the
  same pin is attached twice. The setup uses GPIO33 (not brought out on the
  WROOM-1-N8) as a dummy MISO; don't assign GPIO33 to anything else.
- Check all 6 SPI connections
- Verify 3.3V power and backlight pin (GPIO 9)
- White / noisy flash at power-up: the backlight must stay off until the
  first screen is drawn. The TFT_eSPI setup must **not** define `TFT_BL` or
  `TFT_BACKLIGHT_ON` (the build stops if it does): TFT_eSPI's ST7789 init
  switches `TFT_BL` HIGH at the end of `tft.init()`. A pull-down on GPIO 9
  covers the time from power-on until `setup()` starts (boot ROM and
  bootloader)

### No Bluetooth audio
- The BT ESP32 must be ESP32-WROOM (Classic BT). ESP32-S3/C3 won't work.
- Check Serial Monitor on BT ESP32 for "Miata NA" name on boot
- Forget and re-pair on phone
- Verify UART wiring between the two boards (TX↔RX crossed)
- Check **BT Enable** is ON in the settings menu
- No auto-reconnect at power-up: connect once from the phone with BT
  Auto-Reconnect ON so the phone gets stored (BT ESP32 Serial Monitor shows
  "auto-reconnect on" at boot)

### MP3 not playing
- MP3 playback is now LOCAL on the primary ESP32-S3
- Check SD card wiring: MOSI=40, MISO=41, SCLK=39, CS=42
- Verify SD card: FAT32, MP3 files in the root or in folders up to 4 deep
- Function Test → SD CARD lists every file and folder on Serial and warns
  about MP3s the player won't play (too deep, over 500)
- Check Serial Monitor for file count on boot
- Check I2S DAC wiring: BCK=6, LRCK=15, DATA=7
- Check DAC XSMT (GPIO5) is HIGH and the I2S switch (GPIO8) is LOW
- Run `test/sd_card_test` (card mounts, MP3s listed, read speed) and
  `test/i2s_tone_test` (DAC → relays → amp path, no SD needed)

### UART communication issues
- Verify TX↔RX crossover: Primary TX (18) → BT RX (16), Primary RX (17) ← BT TX (17)
- Verify common GND between boards
- Check Serial Monitor on both boards for TX/RX messages
- Both boards must be at 115200 baud

### Keypad not responding
- Verify diode orientation (anodes to columns)
- Check I2C connection to MCP23017 (SDA=1, SCL=2)
- Upload `keypad_mcp23017_test.ino` for diagnostics

### Relays not switching
- Check GPB0 (DAC_RELAY) / GPB1 (Radio_RELAY) wiring to the relay drivers
- Verify relay module trigger voltage matches MCP23017 output

---

## To Do

- **Smooth scrolling (optional):** the display now renders through a sprite
  (no flicker), so pixel-by-pixel scrolling is possible instead of character
  steps. See Phase 2 of the demo in `test/display_test/display_test.ino` (STEP 6).
- **Display SPI clock:** currently 40 MHz. Try 80 MHz in
  `Setup_MiataS3_ST7789_170x320.h` now that the `tft.init()` hang is fixed.
- **DAC mute handling:** XSMT (GPIO5) is pulsed LOW around I2S source
  changes. Could also be pulsed around MP3 track changes and pause.
- **MCP23017 interrupts:** INTA (GPIO48) / INTB (GPIO21) are wired but the
  keypad is still polled.

---

## Architecture Notes

### Why Two ESP32s?

The ESP32-S3 offers better performance (dual-core 240 MHz, more GPIO, native USB)
but **does not support Classic Bluetooth** (only BLE). Since A2DP audio streaming
requires Classic BT, a separate ESP32-WROOM handles Bluetooth audio reception.

### DAC Sharing

On the new PCBA there is one PCM5102A. The MP3 player (I2S1, ESP32-S3) and the
Bluetooth audio (I2S2, ESP32-WROOM-32E) both feed a TMUX1574, and GPIO8 on the
primary picks which one reaches the DAC (HIGH = BT, LOW = MP3). The DAC output
goes to the relays (DAC_RELAY LOW = DAC path). Leaving the BT input sends an
AVRCP pause to the phone; leaving the MP3 input pauses the MP3 player.

### Core Assignment (Primary ESP32-S3)

| Core | Tasks |
|------|-------|
| Core 0 | FreeRTOS system tasks |
| Core 1 | Main loop: display, keypad, MP3 decode, UART, relay control |

### Core Assignment (Secondary BT ESP32)

| Core | Tasks |
|------|-------|
| Core 0 | Bluetooth A2DP audio, BT controller/AVRC, FreeRTOS system |
| Core 1 | Main loop: UART command processing |
