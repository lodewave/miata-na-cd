# Changelog

> Firmware version history. The repository itself is versioned separately
> via Git tags (currently `v0.1.0`); these firmware numbers predate the repo.

## v3.8 — 2026-10-18
### White flash at power-up — actual cause fixed
- The flash came ~1 s after reset, during `tft.init()`. v3.7 only removed
  `TFT_BACKLIGHT_ON`, but TFT_eSPI's ST7789 init
  (`TFT_Drivers/ST7789_Init.h`) also ends with `digitalWrite(TFT_BL, HIGH)`
  whenever `TFT_BL` is defined, so the backlight still came on over the
  uninitialised panel
- `TFT_BL` removed from `Setup_MiataS3_ST7789_170x320.h`; the firmware uses
  its own `LCD_BL_PIN` (GPIO9, `board.h`). `config.h` stops the build if the
  setup defines `TFT_BL` or `TFT_BACKLIGHT_ON`. **Updated setup file
  installed in `TFT_eSPI/User_Setups/` on this PC** (and in `libraries/`)
- v3.7's startup order kept: backlight LOW from the first line of
  `setup()`, on only after the Radio screen is drawn

## v3.7 — 2026-10-18
### No white flash at power-up
- `TFT_BACKLIGHT_ON` removed from `Setup_MiataS3_ST7789_170x320.h`:
  `tft.init()` used to switch the backlight on while the panel RAM still
  held noise. `config.h` stops the build if it's defined again.
  **Copy the updated setup file into `TFT_eSPI/User_Setups/`** (done on
  this PC)
- Both backlights driven LOW as the first thing in `setup()`;
  `initLighting()` attaches PWM at 0%, and the first `lightingLoop()` turns
  them on after the Radio screen is drawn
- Hardware: a pull-down on GPIO9 (SPI1_BL) is still recommended for the
  time before `setup()` runs

## v3.6 — 2026-10-18
### MP3 folders
- MP3s are found in folders as well as the root, up to 4 levels deep
  (`MP3_MAX_DEPTH`), and all shuffled together. Folder selection to come
- Limit raised from 100 to 500 files (`MP3_MAX_FILES`); full paths are kept
  in RAM (~35 KB at 500). Boot log shows scan time and free heap
- Skipped: hidden folders (`.Trashes`, `.Spotlight-V100`…),
  `System Volume Information`, `$RECYCLE.BIN`, hidden `._` files
- No tags: the title is the file name without its folder
- New `mp3_files.h`: limits and `mp3WalkCard()`, used by both the player and
  the function test's SD card test, so the test lists exactly what the
  player plays (folder tree on Serial; warnings for folders too deep and
  files over the limit). The SD test no longer warns about mixed-case
  `.Mp3` (played since v3.5) or `._` files (skipped)
- SD mounted with 8 open files (was 5) for the folder walk

## v3.5 — 2026-10-18
### Review and refactor (both boards)
**Fixes**
- BT ESP32: UART lines from the Bluetooth task (metadata, connect/disconnect)
  and the command loop could interleave mid-line, and the stored
  artist/title Strings were changed in one task while read in the other.
  Every line now goes out in one write under a mutex, and the metadata is
  only touched under it
- BT ESP32: `BT_PLAYING` / `BT_PAUSED` come from the A2DP audio-state
  callback (the phone's real state, including play/pause on the phone)
  instead of being sent blindly after each command
- BT ESP32: line breaks in metadata are sent as spaces (they split the
  protocol line). UART receive buffers capped on both boards
- Leaving BT/MP3 for Radio/AUX during the 3 s splash: the splash could
  expire later and redraw the screen. Now ended on the input switch
- Scroll and splash timers are wrap-safe (`millis()` rollover)
- MP3: files ending in `.Mp3` etc. are found, macOS `._` files skipped
- ID3: titles/artists over 255 bytes are cut instead of dropped; UTF-16BE
  and ID3v2.2 UTF-16 decoded; ID3v2 extended header skipped; non-Latin-1
  characters shown as `?`

**Refactor (no behaviour change)**
- Removed `pending_artist` / `pending_title` / `bt_metadata_updated`: the BT
  screens are always drawn from `btArtist` / `btTitle` by
  `showBluetoothDisplay()`, so the duplicate handling in `loop()`,
  `checkSplashExpired()` and `btProcessLine()` is gone
- MP3 track changes redraw through `showMP3Display()` (same pattern as BT)
- `mp3Play()` renamed `mp3PlayPause()`; it, `mp3Loop()` and
  `mp3NextTrack()` reuse `mp3Resume()` / `mp3NextTrack()` instead of copies.
  Shuffle order is a static array (no heap)
- ID3 parser: one loop for v2.2–2.4, shared text decoder, results returned
  instead of file-level globals
- `i2s1Begin()` shared by the MP3 player and the function test (one I2S1
  config)
- `KeyId` names for the matrix buttons (`keypad.h`); `onKeyPress()` and the
  settings menu use them. BT and MP3 share one key table
- Settings menu: ON/OFF and backlight % items handled generically
- Serial messages print the timing constants instead of hard-coded values

## v3.4 — 2026-10-18
### Bluetooth auto-reconnect and BT Enable
- BT ESP32 reconnects to the last phone at power-up (ESP32-A2DP
  auto-reconnect; it was off, so the phone had to be picked from its menu
  every time). The phone is stored in the BT ESP32's NVS when it connects
- New settings, after LCD Backlight - ILLUM and before MP3 Enable:
  **BT Enable** (ON) and **BT Auto-Reconnect** (ON). NVS keys `bt_en`,
  `bt_auto`. Menu now has 10 items
- BT Enable OFF: EJECT skips BT (`inputEnabled()`), and the BT ESP32 doesn't
  start A2DP, so phones can't see or connect to it. Disabling it while on
  BT switches to Radio when the menu closes
- New UART command `BT_CONFIG:e,a` (primary → BT ESP32), sent at boot, on
  `BT_READY` and on menu close. The BT ESP32 keeps its own copy in NVS
  (namespace `miata_bt`) so the settings apply from its power-up; it ignores
  unchanged values, applies auto-reconnect straight away (storing the
  connected phone when turned on) and restarts for a BT Enable change
- BT ESP32 sends `BT_READY` before starting Bluetooth (was after); the
  primary clears its BT connection state on `BT_READY`
- **Both boards need the new firmware**

### Fix: MP3 text on the BT WAIT screen
- The scroll animation kept running on the BT input while "BT WAIT" was
  shown, drawing the last MP3 artist/title bands over it. It now only runs
  when a phone is connected
- `showBluetoothDisplay()` reloads the scroll lines from the BT metadata
  (cleared on BT WAIT), so MP3 text can't show on the BT screens either.
  `btLoop()` redraws through it

## v3.3 — 2026-10-17
### Input switching
- Input order is now Radio → BT → MP3 → AUX (`InputState` renumbered:
  MP3 = 2, AUX = 3)
- MP3 splash: "MP3" / "PLAYER" (was "MP3" / "INPUT")
- New `switchRelaysMuted()`: AMP_MUTE (GPB2) HIGH → 500 ms → switch only the
  relays that change, 30 ms apart if both (was always both, 50 ms apart) →
  500 ms → AMP_MUTE LOW. No mute when no relay changes (BT ↔ MP3)
- New `setI2SSourceMuted()`: DAC_MUTE (GPIO5) LOW → 5 ms → I2S_SWITCH →
  5 ms → DAC_MUTE HIGH, only when I2S_SWITCH changes
- MODULE_ACTIVE (GPB3): LOW on Radio, HIGH on any other input. Set in the
  same Port B write as AMP_MUTE HIGH, so it changes as the amp is muted.
  AMP_MUTE and MODULE_ACTIVE configured as outputs (LOW) at boot
- RADIO_SENSE (GPB5) read LOW for 0.5 s switches to Radio (`radioSenseLoop()`,
  50 ms poll). Fires once per LOW period, re-arms on HIGH
- `setRelays()` kept as the raw write for the function test (now 30 ms apart)

### Timing update
- Relay switching: AMP_MUTE held 15 ms before and 15 ms after (was 500 ms),
  20 ms between two relays (was 30 ms). `AMP_MUTE_SETTLE_MS` split into
  `AMP_MUTE_BEFORE_MS` / `AMP_MUTE_AFTER_MS`
- RADIO_SENSE: LOW for 100 ms (was 0.5 s) → MODULE_ACTIVE LOW → wait 100 ms
  → switch to Radio. Polled every 10 ms (was 50 ms)
- Relay switching: AMP_MUTE held 100 ms before and 200 ms after (was 15 ms
  each) to remove the remaining relay pop

## v3.2 — 2026-10-17
### Settings menu with persistent settings
- Hold PIN 1 s to open; TrackUp/TrackDown move, FF/REV change, PIN saves
  and exits (`settings_menu.ino`). Other buttons ignored; audio keeps playing
- Settings stored in NVS flash with Preferences (`settings.ino`, namespace
  `miata`); only changed values are written. Survive power loss and uploads
- Items: MP3 Volume (−12.0 dBFS default, 0.5 dB steps, −60…0, peak sample
  shown in parentheses), Button Backlight (70%), LCD Backlight - Day (90%),
  LCD Backlight - ILLUM (50%), MP3 Enable, Aux Enable, Scroll Speed (1.5 s,
  0.1 s steps, 0.2–5.0 s), Function Test
- Backlight settings change in 1% steps; LCD backlights limited to 5–100%

### Backlights follow the headunit (`lighting.ino`)
- Button backlight (GPIO4) and LCD backlight (GPIO9) on LEDC PWM, driven from
  ILLUM SENSE (GPB4) and HU LIGHTSWITCH (GPB6), filtered over two 50 ms reads
- Buttons: lit at Button Backlight only when ILLUM LOW and LIGHTSW HIGH;
  off when ILLUM HIGH (LIGHTSW ignored) or LIGHTSW LOW
- LCD: Day value when ILLUM HIGH; ILLUM value when ILLUM LOW and LIGHTSW
  HIGH; off when both LOW
- Menu preview: the edited backlight shows its value live; the LCD never
  goes fully off while the menu is open

- Documented reverse logic on ILLUM SENSE, RADIO SENSE and HU LIGHTSWITCH:
  the PCBA inverts them, so a HIGH read by the ESP32 is a LOW from the
  headunit. All logic and docs use the level read by the ESP32

### Inputs
- EJECT skips AUX / MP3 when disabled in the menu. If the current input is
  disabled, closing the menu switches to Radio

### MP3 volume
- `VolumeStream` replaced by `Mp3GainOutput` (16.16 fixed-point gain) — the
  library rounded the volume to 0.01, too coarse for 0.5 dB steps.
  `MP3_VOLUME` removed; level comes from the menu

### Function test built into the main firmware
- `test/function_test` moved to `primary_esp32s3/src/function_test/` as C++
  (`namespace ftest`), started from the menu. Exits by holding PIN 1 s,
  which restarts the ESP32. Standalone copy in `test/function_test` was
  removed, then restored from the 20261017_220550 backup to keep for future
  work (unchanged; separate from the built-in copy)
- One keypad driver: `keypad.ino` exports button states (`keypad.h`) and a
  PIN long-press event; `actions.ino` routes buttons by UI mode
- Shares the main firmware's display, MCP23017, I2S (`mp3_i2s`) and SD SPI
  bus; uses the main `setRelays()`
- All pins moved to `board.h`, used by both the firmware and the test.
  Headunit pins added: AMP MUTE GPB2, MODULE ACTIVE GPB3, ILLUM SENSE GPB4,
  RADIO SENSE GPB5, HU LIGHTSWITCH GPB6 (sense inputs configured with pullups
  at boot)

## v3.1 — 2026-10-10
### New PCBA — keypad, relays, display pin mapping (primary ESP32-S3)

### Display — sprite rendering
- `display.ino` draws every screen into a 320×118 16-bit `TFT_eSprite`
  (75.5 KB, allocated in `initDisplay()` before the MP3 decoder) and pushes
  it to the visible window. Removes the blank-then-reprint flicker on scroll
  steps and the black flash on input/splash changes
- Scroll steps push only the band that moved (`pushSprite` sub-region)
- `showStaticDisplay()` moved from `actions.ino` to `display.ino`;
  `showSplash()` now clears the window itself (callers no longer call
  `tft.fillScreen()`)
- Falls back to direct drawing (old behaviour) if the sprite can't be
  allocated; boot log prints free heap and the result
- Layout and scroll timing unchanged

### Keypad polling
- I2C1 raised from 100 kHz to 400 kHz (`I2C_CLOCK_HZ`)
- Keypad scanned every 10 ms (`KEYPAD_SCAN_MS`) instead of every loop pass
- Scan uses whole-port Port A `writeGPIOA()` / `readGPIOA()`: 9 I2C
  transfers per scan instead of ~26 per-pin transfers; PIN and EJECT are
  read from the same Port A read
- `handleDirectButtons()` merged into `handleKeypad()`; debounce (50 ms,
  fire once per press) and button behaviour unchanged

### MP3 volume
- MP3 output now goes decoder → `VolumeStream` → I2S at a fixed linear gain
  of 0.24 (`MP3_VOLUME` in `config.h`): full-scale peak 7864 / 32767,
  -12.4 dBFS, matching the tone tests' ~8000 level. Linear curve selected
  via `allow_boost`; sample-rate changes still reach I2S

### Display — real spaces
- Display text shows real spaces instead of underscores. The DSEG14 space
  glyph's xAdvance was widened from 7 to 29 px (one full character cell) in
  `DSEG14Classic_Regular_18.h`; `normalizeSpaces()` removed
- "BT_WAIT" → "BT WAIT", default MP3 artist "SD_CARD" → "SD CARD"

### Changed
- Keypad matrix on MCP23017 Port A: ROW3=GPA0, ROW2=GPA1, ROW1=GPA2, ROW0=GPA3,
  COL1=GPA5, COL0=GPA6 (button positions unchanged)
- Direct buttons: EJECT=GPA4, PIN=GPA7
- Audio relays moved to GPB0 (DAC_RELAY) and GPB1 (Radio_RELAY); renamed from
  RELAY2/RELAY1. Truth table unchanged.
- Relay and row outputs are latched to their idle level before being switched
  to outputs (no glitch at boot)
- `setRelays()` skips I2C writes if the MCP23017 was not found

### Added
- `tft_espi_setup/Setup_MiataS3_ST7789_170x320.h` — TFT_eSPI setup for the S3
  display pins (9–14), with `USE_FSPI_PORT` for Arduino-ESP32 core 3.x,
  `TFT_MISO 33` (dummy; fixes `tft.init()` hang caused by TFT_eSPI defaulting
  MISO to the MOSI pin), and SPI clock 40 MHz.
  Installed in the TFT_eSPI library and selected in `User_Setup_Select.h`
  (Setup408 left commented out for the legacy sketches)
- Compile-time check in `config.h` that the Miata S3 TFT_eSPI setup is active;
  the sketch now uses the setup's `TFT_*` pins instead of redefining them
- `test/display_test` (TFT_eSPI step-by-step bring-up with watchdog backtrace)
  and `test/display_raw_spi_test` (panel test without TFT_eSPI)
- `test/keypad_mcp23017_test` updated for the ESP32-S3 (I2C GPIO1/2) and the
  new keypad/button pinout; also prints direct-button raw states and latches
  the relays LOW (Radio path) during the test
- Documented MCP23017 INTA (pin 20 → GPIO48) and INTB (pin 19 → GPIO21);
  not used by firmware yet

### MP3 player on the new PCBA
- I2S pins on the new PCBA: BCK 6, LRCK 15, DATA 7 (BCK and DATA swapped
  from the old board)
- SD (CS 42, MISO 41, MOSI 40, CLK 39) confirmed unchanged
- `initMP3()` now drives I2S_SWITCH (GPIO8) LOW and DAC XSMT (GPIO5) HIGH
  before anything else, even if the SD card is missing. `pinMode()` is called
  before `digitalWrite()`: on core 3.x a write to a pin that isn't set up yet
  is ignored, which had left XSMT LOW (DAC muted). Boot log reads the pins
  back to confirm their levels.
- I2S DMA buffer raised to 8 × 2048 bytes (~93 ms) from the library default
  (~17 ms) to ride through keypad/display work in the main loop
- Main loop skips its 10 ms idle delay while MP3 is playing
- First track is loaded (with ID3 tags) at boot, so the display shows
  artist/title on first play instead of "SD_CARD" with a blank title
- SD SPI bus started with SS = -1; the SD library drives CS itself
- New `test/sd_card_test` and `test/i2s_tone_test`
- New `test/function_test` — one sketch with five tests, EJECT cycles:
  keypad (button states on screen), LED PWM (button LED GPIO4 and display
  backlight GPIO9, RPT selects; backlight restored to 100% on exit), headunit I/O, SD card (waits for
  Play to start, results on screen, Play again rescans), I2S tone (current tone + routing on screen;
  starts stopped, Play starts / stops the sequence, RPT plays a continuous
  440 Hz tone cycling L / R / both, TrackUp/Down set volume ±1 dB from
  -12 dBFS, range -60 to 0 dBFS; screen shows dBFS and peak amplitude
  out of 32767; FF toggles DAC MUTE / XSMT (starts HIGH), REV toggles
  AMP MUTE (starts LOW), both shown on the bottom two rows).
  Each test resets its outputs on entry and cleans up on exit
- New `test/headunit_io_test` — MCP23017 GPB4–6 inputs (ILLUM SENSE, RADIO
  SENSE, HU LIGHTSWITCH) and GPB0–3 outputs (DAC RELAY, RADIO RELAY, AMP MUTE,
  MODULE ACTIVE) shown live on the display; outputs toggled from the keypad
- New `test/led_pwm_test` — GPIO4 LED (LEDC 5 kHz, 12-bit). Starts at 50%;
  TrackUp/TrackDown ±1%, FF/REV ±10%, clamped 0–100%; Play toggles the LED
  on/off (keeps the set duty). Instructions and
  current duty shown on the display (headunit_io_test format)

### Bluetooth (bt_esp32, ESP32-WROOM-32E) on the new PCBA
- BT ESP32 I2S2 pins: BCK 32, DATA 14, LRCK 15 (BCK and DATA swapped from
  the old board). I2S config now explicit (TX, 44.1 kHz, 16-bit, no MCLK)
- Primary UART1 pins swapped: TX = GPIO18 (→ BT GPIO16), RX = GPIO17
  (← BT GPIO17). BT side unchanged (TX 17, RX 16)
- `switchInput()` drives I2S_SWITCH (GPIO8 → TMUX1574 select): HIGH for the
  Bluetooth input (I2S2), LOW for everything else (I2S1, MP3 player)
- Leaving BT / MP3 now pauses before the audio is re-routed
- Docs: I2S1 = MP3 player bus (ESP32-S3), I2S2 = Bluetooth bus (BT ESP32);
  single PCM5102A shared through the TMUX1574

### Removed
- ILLUM_12V, RADIOTAPE_ON, HU_LIGHTSWITCH, MODULE_ACTIVE, AMP_MUTE pin defines
  (GPB0–GPB4 on the old board; not yet assigned on the new PCBA, and unused
  in code)

## v3.0 — 2026-07-14
### Architecture Change — ESP32-S3 Primary + ESP32-WROOM BT Secondary
Complete platform restructuring. The primary controller is now an ESP32-S3,
and the secondary ESP32-WROOM handles only Bluetooth audio.

### Added
- New firmware: `primary_esp32s3/` — full primary controller on ESP32-S3
- New firmware: `bt_esp32/` — dedicated Bluetooth-only secondary on ESP32-WROOM
- Local MP3 playback on ESP32-S3 (SD card + I2S decode, no external ESP32 needed)
- Separate SPI buses: FSPI for TFT display, HSPI for SD card
- BT connection state tracking (BT_CONNECTED/BT_DISCONNECTED notifications)
- BT_PLAYPAUSE command for proper play/pause toggle via AVRCP
- BT_STATUS command to request current connection + metadata from BT ESP32

### Changed
- **Primary MCU**: ESP32-WROOM → ESP32-S3 (more GPIO, better performance)
- **Secondary MCU role**: MP3 player → Bluetooth A2DP receiver only
- MP3 playback now local on primary (was UART-remote on dedicated ESP32)
- Bluetooth audio now on secondary (was on primary ESP32-WROOM)
- UART protocol: new BT_ prefixed commands (BT_PLAY, BT_PAUSE, BT_NEXT, etc.)
- Primary uses UART1 (was UART2) for inter-ESP32 communication
- All ESP32-S3 pin assignments (I2S: 7/15/6, TFT: 11/12/10/13/14/9, SD: 40/39/41/42, I2C: 1/2, UART: 17/18)
- BT ESP32 pins simplified (I2S: 14/15/32, UART: 17/16)
- Removed AudioTools/BluetoothA2DPSink from primary (not needed on S3)
- Replaced `mp3player.ino` UART interface with direct SD/I2S playback code
- Replaced `bluetooth.ino` with `bt_interface.ino` (UART client to BT ESP32)

### Why ESP32-S3?
- ESP32-S3 does NOT support Classic Bluetooth (A2DP/AVRCP)
- ESP32-S3 has more available GPIO, native USB, and better memory
- Consolidating display + keypad + MP3 on one chip reduces UART complexity
- BT stays on WROOM because Classic BT requires the original ESP32 silicon

### Legacy
- `bt_music_receiver/` — original primary firmware (ESP32-WROOM, BT+display+keypad)
- `mp3_esp32/` — original MP3 firmware (ESP32-WROOM, SD+decode+I2S)

## v2.3 — 2026-06-13
### Added
- MP3 player input state (INPUT_MP3) — dedicated second ESP32 for MP3 playback
- New file: `mp3player.ino` — UART command interface to MP3 ESP32
- New sketch: `mp3_esp32/mp3_esp32.ino` — standalone MP3 player firmware
- Two-ESP32 architecture: main handles BT/display/keypad, MP3 ESP32 handles decoding
- UART protocol for inter-ESP32 communication (PLAY/PAUSE/NEXT/PREV/STATUS)
- ID3v2 and ID3v1 tag parsing (artist/title from MP3 metadata)
- Shuffle playback by default (Fisher-Yates algorithm)
- Starts playing immediately when MP3 input is selected
- Pause/resume on input switch (resumes from where it left off)
- TrackUp = next track, TrackDown = restart (or previous if < 3s into song)
- Play button toggles play/pause
- Artist/Title display with labels and horizontal divider (same as BT mode)
- Splash screen "MP3" / "INPUT" for 3 seconds on input switch
- STATUS request after splash expires to ensure display shows current track
- Relay setting: RELAY1=HIGH, RELAY2=LOW (DAC output path)

### Changed
- Input states increased from 3 to 4 (Radio, BT, AUX, MP3)
- EJECT cycle: Radio → BT → AUX → MP3 → Radio
- TFT SPI frequency increased from 27 MHz to 80 MHz (reduces display flicker)
- config.h: added UART2 pins (TX=GPIO16, RX=GPIO35), removed I2S1/SD pins
- MP3 decoding moved to external ESP32 (WROOM heap too small for BT + MP3)

### Architecture
- Main ESP32: BT A2DP, display, keypad, relays, UART command sender
- MP3 ESP32: SD card, MP3 decode (libhelix), I2S output, UART command listener
- Communication: 115200 baud UART, text-based protocol

## v2.2 — 2026-06-07
### Removed
- MP3 player feature (SD card playback) — not viable on ESP32-WROOM due to
  I2S contention with Bluetooth A2DP (see MP3_implementation.md for details)
- SD card pin definitions and HSPI bus configuration
- Unused display dimension constants (DISP_PHYSICAL_W/H, DISP_VISIBLE_W/H)

### Fixed
- BT metadata display: metadata now stored regardless of current input state,
  so switching to BT mode shows current track info immediately

### Changed
- Input states reduced from 4 to 3 (Radio, BT, AUX)
- EJECT cycle: Radio → BT → AUX → Radio
- Restored simple BT architecture: BluetoothA2DPSink(i2s) with direct I2S output
- Splash screen now only applies to BT mode

## v2.1 — 2026-04-26
### Added
- Input state machine: cycle between Radio, Bluetooth, and AUX-IN via EJECT button
- Relay control (RELAY1=GPB6, RELAY2=GPB5) with 50ms inter-relay delay
- Static display modes for Radio ("RADIO/INPUT") and AUX ("AUXILIARY/INPUT")
- Splash screen for Bluetooth ("BLUETOOTH/INPUT" for 3 seconds)
- Auto-pause BT playback when leaving BT mode, auto-resume when returning
- Device starts in Radio mode on power-up
- 7 new keypad button assignments (TrackUp, Play, TrackDown, Scan, FF, RPT, REV)
- Direct buttons: PIN (GPA0), EJECT (GPA3) — wired directly to GND
- Refactored code into 5 files: bt_music_receiver, bluetooth, display, keypad, actions
- Extracted reusable `debounceButton()` helper (eliminates 3× duplicated logic)
- Consolidated README.md with full wiring, state table, and architecture

### Changed
- Keypad rows moved from GPB0-3 to GPA4-7 (all keypad on Port A now)
- Keypad columns moved from GPA0-1 to GPA1-2
- I2C pins changed from GPIO 25/26 to GPIO 13/27 (avoids DAC conflict)
- Relay pins swapped: RELAY1=GPB6, RELAY2=GPB5

## v2.0 — 2026-04-21
### Added
- MCP23017 I2C GPIO expander for keypad (replaced direct GPIO)
- Graceful degradation if MCP23017 not found
- Test sketch (`keypad_mcp23017_test.ino`) with I2C scanner and diagnostics
- Freed 4 ESP32 GPIO pins (16, 25, 26, 33)

### Changed
- Keypad interface: direct ESP32 GPIO → I2C via MCP23017

## v1.1 — 2026-04-19
### Added
- Display visible window constraints (118×320, 26px vertical offset)
- DSEG14 18pt font (fits 33px band height)
- Character-based scrolling (1.5s/char, 2s pause)
- Keypad debounce fix
- Color fix for TFT_eSPI free font RGB swap issue

### Changed
- Font size: 24pt → 18pt
- Display color: 0xBCA5 → 0x54E4 (neutral yellow-green)
- Margin: 6px → 5px

## v1.0 — 2026-04-19
### Initial Release
- Bluetooth A2DP music receiver
- I2S output to external DAC
- TFT display with artist/title metadata
- DSEG14 Classic font (14-segment display style)
- Basic keypad control (play/pause, next, previous)
