/*
  Copyright (C) 2026 lodewave
  This program is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation, either version 3 of the License, or (at your option)
  any later version. This program is distributed WITHOUT ANY WARRANTY. See
  the GNU General Public License (LICENSE at the repo root) for details.

  Miata NA — Bluetooth A2DP Receiver (Secondary ESP32-WROOM)

  Dedicated Bluetooth audio receiver. This ESP32-WROOM handles:
    - A2DP sink (Bluetooth audio reception from phone/source)
    - I2S audio output to PCM5102A DAC
    - AVRC metadata (artist/title) forwarding to primary ESP32-S3
    - AVRCP playback control (play/pause/next/prev) from primary

  UART Protocol (text-based, 115200 8N1):

    Commands received (from Primary ESP32-S3):
      "BT_PLAY\n"      — AVRCP play
      "BT_PAUSE\n"     — AVRCP pause
      "BT_PLAYPAUSE\n" — AVRCP toggle play/pause
      "BT_NEXT\n"      — AVRCP next track
      "BT_PREV\n"      — AVRCP previous track
      "BT_STATUS\n"    — report connection status + current metadata
      "BT_CONFIG:e,a\n" — settings from the primary's menu, e/a = 0 or 1:
                         e = Bluetooth enabled, a = auto-reconnect.
                         Sent at primary boot, on BT_READY and when the
                         settings menu closes. Ignored if unchanged.
    Play/pause/next/prev are ignored while no phone is connected.

    Responses sent (to Primary ESP32-S3):
      "BT_ARTIST:name\n"   — current artist (from AVRC metadata)
      "BT_TITLE:name\n"    — current title (from AVRC metadata)
      "BT_CONNECTED\n"     — source connected
      "BT_DISCONNECTED\n"  — source disconnected
      "BT_PLAYING\n"       — audio stream started (reported by the phone)
      "BT_PAUSED\n"        — audio stream suspended / stopped
      "BT_READY\n"         — booted (sent before Bluetooth starts), ready
                             for commands

  Threading: the A2DP callbacks run in the Bluetooth task, commands in
  loop(). Every UART line goes out in one write under commLock, and the
  stored metadata is only touched under the same lock, so lines from the two
  tasks can't interleave and the Strings can't be changed mid-read.

  Hardware (module: ESP32-WROOM-32E):
    I2S2 (schematic name) → TMUX1574 → PCM5102A DAC (shared with the MP3
    player's I2S1; the primary's GPIO8 selects the source, HIGH = I2S2/BT):
      BCK=32, DATA=14, LRCK=15  (no MCLK)
    UART2 to Primary ESP32-S3:
      GPIO17 (TX) → ESP32-S3 GPIO17 (RX)
      GPIO16 (RX) ← ESP32-S3 GPIO18 (TX)

  Bluetooth name when pairing: "Miata NA"

  Settings (stored here in NVS, namespace "miata_bt", so they apply from
  power-up without waiting for the primary; the primary's settings menu is
  the source and keeps them in sync with BT_CONFIG):
    bt_en   Bluetooth enabled. OFF = A2DP never started: the module is
            invisible to phones. Changing it restarts this ESP32.
    bt_auto Auto-reconnect. ON = at power-up this ESP32 connects to the last
            phone itself, so you don't have to pick "Miata NA" on the phone.
            The phone is remembered each time it connects (and when this is
            turned on while connected). Applies straight away; a phone that
            isn't connected is picked up at the next power-up.

  Board: ESP32 Dev Module, Flash 4MB. Default partition works (~85% used);
  switch to "Huge APP (3MB No OTA/1MB SPIFFS)" if it outgrows that.

  Architecture:
    This is the secondary controller. The primary ESP32-S3 handles display,
    keypad, MP3 playback, and relays. This WROOM handles only Bluetooth
    because ESP32-S3 does not support Classic Bluetooth (A2DP).

  Libraries:
    - AudioTools by Phil Schatzmann
    - ESP32-A2DP (BluetoothA2DPSink, part of AudioTools ecosystem)
*/

#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include <HardwareSerial.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Pin Definitions
// ---------------------------------------------------------------------------

// I2S2 output (to PCM5102A DAC via TMUX1574) — confirmed on the new PCBA
#define I2S_BCK      32   // I2S2 BCK
#define I2S_WS       15   // I2S2 LRCK (word select)
#define I2S_DATA_OUT 14   // I2S2 DATA (→ DAC DIN)

// UART2 — Communication with Primary ESP32-S3 — confirmed on the new PCBA
#define COMM_TX 17   // TX to Primary ESP32-S3 RX (GPIO17)
#define COMM_RX 16   // RX from Primary ESP32-S3 TX (GPIO18)

// UART0 — Programming / Serial Monitor
// TXD0 = GPIO 1, RXD0 = GPIO 3 (fixed on ESP32-WROOM)
// EN pin — Reset button (active LOW, module EN/CHIP_PU pin)
// GPIO 0 — Boot button (hold LOW during reset to enter download mode)

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

#define BT_DEVICE_NAME "Miata NA"
#define UART_BAUD      115200
#define RX_LINE_MAX    64          // longest command is ~16 chars

#define PREFS_NAMESPACE      "miata_bt"
#define DEF_BT_ENABLE        true
#define DEF_AUTO_RECONNECT   true
#define CONFIG_DISCONNECT_MS 500   // let the phone see a clean disconnect
                                   // before restarting for BT Enable

// ---------------------------------------------------------------------------
// A2DP sink with one addition: store the connected phone as the
// auto-reconnect target on request. The library only records the phone when
// it connects with auto-reconnect already on, and set_last_connection() is
// protected. Used when auto-reconnect is turned on while a phone is
// connected, so the next power-up reconnects to it.
// ---------------------------------------------------------------------------

class MiataA2DPSink : public BluetoothA2DPSink {
 public:
  using BluetoothA2DPSink::BluetoothA2DPSink;
  // Writes NVS only with auto-reconnect on and if the address changed
  void rememberCurrentPeer() { set_last_connection(*get_current_peer_address()); }
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

I2SStream i2s;
MiataA2DPSink a2dp_sink(i2s);

static HardwareSerial commSerial(2);  // UART2
static Preferences prefs;
static SemaphoreHandle_t commLock;    // UART TX + stored metadata (see header)

// Settings (see header). btEnabled is what this boot is running with.
static bool btEnabled     = DEF_BT_ENABLE;
static bool autoReconnect = DEF_AUTO_RECONNECT;

// Set in the Bluetooth task, read in loop()
static volatile bool isConnected = false;

// Stored metadata for BT_STATUS — only under commLock
static String currentArtist = "";
static String currentTitle  = "";

// UART receive buffer
static String rxBuffer = "";

// ---------------------------------------------------------------------------
// UART output
// ---------------------------------------------------------------------------

// One protocol line, sent in a single write under the lock
static void sendLine(const String &line) {
  String out = line + '\n';
  xSemaphoreTake(commLock, portMAX_DELAY);
  commSerial.write((const uint8_t *)out.c_str(), out.length());
  xSemaphoreGive(commLock);
}

// Store a metadata value (or clear it with "") under the lock
static void setMeta(String &field, const String &value) {
  xSemaphoreTake(commLock, portMAX_DELAY);
  field = value;
  xSemaphoreGive(commLock);
}

// ---------------------------------------------------------------------------
// A2DP callbacks (Bluetooth task)
// ---------------------------------------------------------------------------

void connection_state_changed(esp_a2d_connection_state_t state, void *) {
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    isConnected = true;
    sendLine("BT_CONNECTED");
    Serial.println("BT: Source connected");
  } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    isConnected = false;
    setMeta(currentArtist, "");
    setMeta(currentTitle,  "");
    sendLine("BT_DISCONNECTED");
    Serial.println("BT: Source disconnected");
  }
}

// The phone's real stream state, so play/pause from the phone is reported
// too (the commands below don't report anything themselves)
void audio_state_changed(esp_a2d_audio_state_t state, void *) {
  bool playing = (state == ESP_A2D_AUDIO_STATE_STARTED);
  sendLine(playing ? "BT_PLAYING" : "BT_PAUSED");
  Serial.println(playing ? "BT: Playing" : "BT: Paused");
}

void avrc_metadata_callback(uint8_t id, const uint8_t *text) {
  String value = String((const char *)text);
  // A line break inside a title would split the protocol line
  value.replace('\n', ' ');
  value.replace('\r', ' ');

  switch (id) {
    case ESP_AVRC_MD_ATTR_TITLE:
      setMeta(currentTitle, value);
      sendLine("BT_TITLE:" + value);
      Serial.println("BT META Title: " + value);
      break;
    case ESP_AVRC_MD_ATTR_ARTIST:
      setMeta(currentArtist, value);
      sendLine("BT_ARTIST:" + value);
      Serial.println("BT META Artist: " + value);
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Settings (NVS) and BT_CONFIG
// ---------------------------------------------------------------------------

void loadSettings() {
  if (prefs.begin(PREFS_NAMESPACE, true)) {   // read-only; fails if never written
    btEnabled     = prefs.getBool("bt_en",   DEF_BT_ENABLE);
    autoReconnect = prefs.getBool("bt_auto", DEF_AUTO_RECONNECT);
    prefs.end();
  }
  Serial.printf("SETTINGS: Bluetooth %s, auto-reconnect %s\n",
                btEnabled ? "on" : "off", autoReconnect ? "on" : "off");
}

static bool isBit(char c) { return c == '0' || c == '1'; }

// "BT_CONFIG:e,a" — e = Bluetooth enabled, a = auto-reconnect (0/1)
void applyConfig(const String &args) {
  if (args.length() != 3 || args[1] != ',' || !isBit(args[0]) || !isBit(args[2])) {
    Serial.println("CONFIG: bad format, ignored");
    return;
  }
  bool en = (args[0] == '1');
  bool ar = (args[2] == '1');
  bool enChanged = (en != btEnabled);
  bool arChanged = (ar != autoReconnect);
  if (!enChanged && !arChanged) return;   // already in sync

  if (prefs.begin(PREFS_NAMESPACE, false)) {
    if (enChanged) prefs.putBool("bt_en",   en);
    if (arChanged) prefs.putBool("bt_auto", ar);
    prefs.end();
  } else {
    Serial.println("CONFIG: ERROR - could not open NVS for writing");
  }

  if (arChanged) {
    autoReconnect = ar;
    Serial.printf("CONFIG: auto-reconnect %s\n", ar ? "on" : "off");
    if (btEnabled) {
      a2dp_sink.set_auto_reconnect(ar);   // off: also stops any retries
      if (ar && isConnected) {
        a2dp_sink.rememberCurrentPeer();  // reconnect to this phone next boot
        Serial.println("CONFIG: current phone stored for auto-reconnect");
      }
    }
  }

  // Bluetooth on/off: A2DP can't be cleanly stopped and restarted (end()
  // also wipes the stored phone), so restart and come up in the new state.
  if (enChanged) {
    Serial.printf("CONFIG: Bluetooth %s - restarting\n", en ? "on" : "off");
    if (isConnected) {
      esp_a2d_sink_disconnect(*a2dp_sink.get_current_peer_address());
      delay(CONFIG_DISCONNECT_MS);
    }
    commSerial.flush();
    Serial.flush();
    ESP.restart();
  }
}

// ---------------------------------------------------------------------------
// UART Command Processing (loop task)
// ---------------------------------------------------------------------------

static void sendStatus() {
  if (!isConnected) {
    sendLine("BT_DISCONNECTED");
    return;
  }
  sendLine("BT_CONNECTED");
  bool playing = (a2dp_sink.get_audio_state() == ESP_A2D_AUDIO_STATE_STARTED);
  sendLine(playing ? "BT_PLAYING" : "BT_PAUSED");

  // Copy under the lock, send after (sendLine takes the lock itself)
  xSemaphoreTake(commLock, portMAX_DELAY);
  String artist = currentArtist;
  String title  = currentTitle;
  xSemaphoreGive(commLock);
  if (artist.length() > 0) sendLine("BT_ARTIST:" + artist);
  if (title.length()  > 0) sendLine("BT_TITLE:"  + title);
}

void processCommand(const String &cmd) {
  Serial.println("CMD RX: " + cmd);

  if (cmd == "BT_STATUS") {
    sendStatus();
    Serial.println("BT: Status sent");
    return;
  }
  if (cmd.startsWith("BT_CONFIG:")) {
    applyConfig(cmd.substring(10));
    return;
  }

  // Playback commands — only with a phone connected. The resulting
  // BT_PLAYING / BT_PAUSED comes from audio_state_changed().
  if (!isConnected) return;
  if (cmd == "BT_PLAY") {
    a2dp_sink.play();
  } else if (cmd == "BT_PAUSE") {
    a2dp_sink.pause();
  } else if (cmd == "BT_PLAYPAUSE") {
    if (a2dp_sink.get_audio_state() == ESP_A2D_AUDIO_STATE_STARTED) a2dp_sink.pause();
    else                                                             a2dp_sink.play();
  } else if (cmd == "BT_NEXT") {
    a2dp_sink.next();
  } else if (cmd == "BT_PREV") {
    a2dp_sink.previous();
  }
}

void readUART() {
  while (commSerial.available()) {
    char c = commSerial.read();
    if (c == '\n') {
      rxBuffer.trim();
      if (rxBuffer.length() > 0) processCommand(rxBuffer);
      rxBuffer = "";
    } else if (c != '\r' && rxBuffer.length() < RX_LINE_MAX) {
      rxBuffer += c;   // overlong lines are cut (and then ignored as unknown)
    }
  }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);  // USB debug
  delay(500);
  Serial.println("\n=== Miata NA — BT ESP32 Boot ===");

  commLock = xSemaphoreCreateMutex();   // before anything can send

  // UART2 for communication with primary ESP32-S3
  Serial.println("INIT: UART2...");
  commSerial.begin(UART_BAUD, SERIAL_8N1, COMM_RX, COMM_TX);
  Serial.printf("UART2 ready (TX=%d, RX=%d)\n", COMM_TX, COMM_RX);

  loadSettings();

  // Tell the primary we're up before Bluetooth starts, so this always comes
  // before any BT_CONNECTED. The primary answers with BT_CONFIG (read in
  // loop()).
  sendLine("BT_READY");

  // I2S audio output
  Serial.println("INIT: I2S...");
  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.pin_bck  = I2S_BCK;
  cfg.pin_ws   = I2S_WS;
  cfg.pin_data = I2S_DATA_OUT;
  cfg.pin_mck  = -1;           // PCM5102A makes its own MCLK from BCK
  cfg.sample_rate     = 44100; // A2DP sink updates this to the source's rate
  cfg.bits_per_sample = 16;
  cfg.channels        = 2;
  if (!i2s.begin(cfg)) {
    Serial.println("ERROR: I2S start failed");
  }
  Serial.printf("I2S2 ready (BCK=%d, LRCK=%d, DATA=%d)\n", I2S_BCK, I2S_WS, I2S_DATA_OUT);

  // Bluetooth A2DP sink (not started at all when Bluetooth is disabled)
  if (btEnabled) {
    Serial.println("INIT: Bluetooth A2DP...");
    a2dp_sink.set_avrc_metadata_attribute_mask(
        ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST);
    a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
    a2dp_sink.set_on_connection_state_changed(connection_state_changed);
    a2dp_sink.set_on_audio_state_changed(audio_state_changed);
    // With auto-reconnect on, the library reads the last phone from NVS and
    // connects to it once the stack is up (start() waits ~1 s for that);
    // other phones can still connect after two failed attempts.
    a2dp_sink.start(BT_DEVICE_NAME, autoReconnect);
    Serial.printf("BT: A2DP sink started - pair with \"%s\"%s\n", BT_DEVICE_NAME,
                  autoReconnect ? " (auto-reconnect on)" : "");
  } else {
    Serial.println("BT: disabled in settings - A2DP not started");
  }

  Serial.println("INIT: Complete! Ready.");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {
  readUART();   // commands from the primary ESP32-S3
  delay(10);
}
