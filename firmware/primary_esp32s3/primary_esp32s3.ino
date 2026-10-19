/*
  Copyright (C) 2026 lodewave
  This program is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation, either version 3 of the License, or (at your option)
  any later version. This program is distributed WITHOUT ANY WARRANTY. See
  the GNU General Public License (LICENSE at the repo root) for details.

  Miata NA — Primary ESP32-S3 Controller

  Main controller for the head unit replacement. Handles:
    - TFT display (ST7789 170×320)
    - Keypad matrix via MCP23017 I2C GPIO expander
    - Relay-based audio input switching
    - Local MP3 playback from SD card (I2S to PCM5102A DAC)
    - UART communication with secondary BT ESP32

  INPUT STATES (cycle with the EJECT button):
    1. RADIO — relays route the car radio to the output.
               Display shows "RADIO" / "INPUT".
    2. BT    — Bluetooth A2DP via secondary ESP32.
               Shows splash "BLUETOOTH" / "INPUT" for 3s,
               then "BT WAIT" until connected, then scrolling artist/title.
               Pauses when switching away, resumes when switching back.
    3. MP3   — SD card MP3 playback (local on this ESP32-S3).
               Shows splash "MP3" / "PLAYER" for 3s, then artist/title.
               Shuffle play by default. Pauses/resumes on input switch.
    4. AUX   — relays route the AUX-IN jack to the output.
               Display shows "AUXILIARY" / "INPUT".
  BT, MP3 and AUX can be disabled in the settings menu (EJECT skips them;
  BT off also turns Bluetooth off on the BT ESP32). With BT Auto-Reconnect
  on, the BT ESP32 reconnects to the last phone at power-up.
  See config.h for the relay truth table.

  HEADUNIT SIGNALS (MCP23017 Port B):
    MODULE_ACTIVE (GPB3) — LOW on Radio, HIGH on any other input
    AMP_MUTE (GPB2)      — HIGH 100 ms before / 200 ms after relay switching
    RADIO_SENSE (GPB5)   — read LOW for 100 ms → MODULE_ACTIVE LOW → 100 ms
                           → switch to Radio
  DAC_MUTE (GPIO5) is pulsed LOW for 10 ms around BT ↔ MP3 I2S switching.

  Display: landscape 320×118 visible, rotation 3.
  Font: DSEG14 Classic Regular 18pt — 14-segment display style.
        Font by Keshikan (www.keshikan.net), licensed SIL OFL 1.1.

  ARCHITECTURE:
    This ESP32-S3 is the primary controller. A secondary ESP32-WROOM
    handles only Bluetooth A2DP reception (Classic BT not supported on S3)
    and communicates track metadata over UART.

  LIBRARIES:
    - AudioTools by Phil Schatzmann
    - arduino-libhelix (MP3 decoder)
    - TFT_eSPI by Bodmer
    - Adafruit MCP23017
    - DSEG14 Classic font by Keshikan
    - SD (built-in)

  SETTINGS MENU: hold PIN for 1 s. TrackUp/TrackDown move, FF/REV change,
    PIN saves and exits. Settings persist in flash (NVS). "Function Test"
    starts the built-in board test; hold PIN 1 s there to restart.

  FILE STRUCTURE:
    primary_esp32s3.ino  — globals, setup(), loop()
    bt_interface.ino     — UART protocol with BT ESP32 (receives metadata)
    mp3player.ino        — Local SD card MP3 playback, shuffle, ID3, I2S, volume
    display.ino          — TFT rendering (sprite frame), scroll logic, menu drawing
    keypad.ino           — MCP23017 init, button scanning, debounce, PIN long press
    actions.ino          — input switching, relay control, button routing
    lighting.ino         — button + LCD backlights from ILLUM / HU LIGHTSWITCH
    settings.ino         — persistent settings (Preferences / NVS)
    settings_menu.ino    — settings menu UI
    config.h             — constants, input states, settings, shared declarations
    board.h              — all pin assignments + display window geometry
    keypad.h             — keypad state shared with the function test
    src/function_test/   — built-in function test (C++, entered from the menu)
*/

#include "config.h"

// ---------------------------------------------------------------------------
// Global Objects
// ---------------------------------------------------------------------------

TFT_eSPI tft = TFT_eSPI();

InputState currentInput = INPUT_RADIO;  // Default to Radio on startup

// Splash screen state
bool     splashActive = false;
uint32_t splashEndMs  = 0;

// Who owns the display / keypad: normal screens, settings menu, function test
UiMode uiMode = UI_NORMAL;

bool displayIsMine() {
  return uiMode == UI_NORMAL;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void setup() {
  // Backlights off first thing, until the first screen is drawn (they come
  // on in the first lightingLoop()). Before this line the pins aren't
  // driven — a pull-down on the backlight driver covers reset/boot.
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, LOW);
  pinMode(BUTTON_BL_PIN, OUTPUT);
  digitalWrite(BUTTON_BL_PIN, LOW);

  Serial.begin(115200);
  delay(500);  // Give serial time to connect
  Serial.println("\n=== Miata NA — ESP32-S3 Primary Boot ===");

  Serial.println("INIT: Settings (NVS)...");
  loadSettings();      // settings.ino — first, everything below uses them

  Serial.println("INIT: Display...");
  initDisplay();       // display.ino
  initLighting();      // lighting.ino — backlights to PWM, still off

  Serial.println("INIT: Keypad...");
  initKeypad();        // keypad.ino — also the headunit sense inputs

  Serial.println("INIT: MP3 Player (local SD)...");
  initMP3();           // mp3player.ino

  Serial.println("INIT: BT Interface (UART to BT ESP32)...");
  initBTInterface();   // bt_interface.ino

  // Set initial input state (Radio)
  Serial.println("INIT: Setting input to Radio...");
  switchInput(currentInput);

  Serial.println("INIT: Complete!");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {
  handleKeypad();         // keypad.ino — matrix + PIN/EJECT, scanned every 10 ms

  // Function test owns everything until it restarts the ESP32. MP3, BT and
  // the backlight logic are suspended (it uses the SD card, I2S and LEDs).
  if (uiMode == UI_FUNCTION_TEST) {
    ftestLoop();
    return;
  }

  lightingLoop();         // lighting.ino — backlights follow ILLUM / LIGHTSW
  radioSenseLoop();       // actions.ino — RADIO_SENSE LOW 100 ms → Radio input

  // Check if splash screen has expired
  checkSplashExpired();   // actions.ino

  // Advance the scroll animation, only while an ARTIST / TITLE screen is
  // shown. ("BT WAIT" isn't one: ticking there would draw text bands over it.)
  bool artistTitleScreen = !splashActive &&
      (currentInput == INPUT_MP3 || (currentInput == INPUT_BT && btConnected));
  if (artistTitleScreen) updateDisplay();   // display.ino

  btLoop();    // bt_interface.ino — BT UART (metadata can arrive any time)
  mp3Loop();   // mp3player.ino — feed the decoder, auto-advance at track end

  // While MP3 is playing, the blocking I2S write in mp3Loop() paces the
  // loop, so skip the idle delay — it would only eat into the audio buffer.
  if (!mp3IsPlaying()) delay(10);
}
