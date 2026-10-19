/*
  Function Test — new PCBA (primary ESP32-S3-WROOM-1-N8)

  Five board tests in one sketch. Press EJECT to move to the next test; the
  display briefly shows "TEST n OF 5" and the test name, then the test screen.

    1. KEYPAD        — live state of every button + last button pressed
    2. LED PWM       — button LED (GPIO4, starts 50%) and display backlight
                       (GPIO9, starts 100%). RPT selects which one; then
                       TrackUp/Down ±1%, FF/REV ±10%, Play on/off
    3. HEADUNIT I/O  — GPB4–6 sense inputs, GPB0–3 outputs toggled by
                       TrackUp/TrackDown/FF/REV (same as headunit_io_test)
    4. SD CARD       — waits for Play, then shows mount status, type, size,
                       MP3 count, warnings, read speed. Play again re-scans
                       (e.g. after swapping cards)
    5. I2S TONE      — L 440 Hz / R 660 Hz / both 550 Hz / silence through
                       I2S1 → TMUX1574 → PCM5102A → relays. Starts stopped,
                       -12 dBFS. Play = sequence / stop, RPT = continuous
                       440 Hz L / R / both, TrackUp/Down = volume ±1 dB,
                       FF = DAC MUTE (XSMT) toggle, REV = AMP MUTE toggle.
                       AMP VOLUME LOW.

  Every test starts from a known state when entered and cleans up when left:
    - MCP23017 GPB0–GPB3 (relays, AMP MUTE, MODULE ACTIVE) LOW between tests
    - Button LED off outside the LED test; it restarts at 50% ON each time
    - Display backlight (GPIO9, PWM) at 100% outside the LED test
    - Tones only play in the tone test (I2S outputs silence otherwise)
    - SD card unmounted outside the SD test

  Display: Font 2, white on black, HIGH/good = green, LOW/bad = red, inside
  the primary_esp32s3 visible window (320×118 at y offset 26, rotation 3).
  Serial (115200) logs everything each test used to print.

  Board: ESP32S3 Dev Module (8MB flash, PSRAM disabled)
  Requires the Setup_MiataS3_ST7789_170x320.h TFT_eSPI setup.

  Files:
    function_test.ino  — setup/loop, test switching, shared objects
    function_test.h    — pins, layout, declarations
    display.ino        — shared drawing helpers
    keypad.ino         — keypad scan + debounce (shared by all tests)
    mode_keypad.ino, mode_led.ino, mode_headunit.ino, mode_sd.ino,
    mode_tone.ino      — one file per test
*/

#include "function_test.h"

// ---------------------------------------------------------------------------
// Shared objects
// ---------------------------------------------------------------------------

TFT_eSPI tft = TFT_eSPI();
Adafruit_MCP23X17 mcp;
I2SStream i2s;
SPIClass sdSPI(HSPI);   // SPI3 — display is on FSPI

const char* const KEY_NAMES[NUM_ROWS][NUM_COLS] = {
  { "unused", "TrackUp"  },   // (0,0) has no button on the keypad
  { "Play",  "TrackDown" },
  { "Scan",  "FF"        },
  { "RPT",   "REV"       },
};

const TestMode MODES[] = {
  { "KEYPAD",       keypadEnter,   keypadExit,   keypadLoop,   keypadKey,   keypadPin   },
  { "LED PWM",      ledEnter,      ledExit,      ledLoop,      ledKey,      ledPin      },
  { "HEADUNIT I/O", headunitEnter, headunitExit, headunitLoop, headunitKey, headunitPin },
  { "SD CARD",      sdEnter,       sdExit,       sdLoop,       sdKey,       sdPin       },
  { "I2S TONE",     toneEnter,     toneExit,     toneLoop,     toneKey,     tonePin     },
};
const int NUM_MODES = sizeof(MODES) / sizeof(MODES[0]);
int currentMode = 0;

#define SPLASH_MS 900

// Keypad scan (keypad.ino)
bool initKeypad();
void scanKeypad();

// ---------------------------------------------------------------------------
// Shared MCP23017 outputs
// ---------------------------------------------------------------------------

// GPB0–GPB3 all LOW: relays on Radio path, AMP MUTE off, MODULE ACTIVE off
void headunitOutputsLow() {
  const uint8_t outs[] = { PIN_DAC_RELAY, PIN_RADIO_RELAY, PIN_AMP_MUTE, PIN_MODULE_ACTIVE };
  for (uint8_t p : outs) mcp.digitalWrite(p, LOW);
}

// Same order as primary_esp32s3 setRelays(): Radio_RELAY first, then DAC_RELAY
void setRelays(bool radioRelay, bool dacRelay) {
  mcp.digitalWrite(PIN_RADIO_RELAY, radioRelay ? HIGH : LOW);
  delay(50);
  mcp.digitalWrite(PIN_DAC_RELAY, dacRelay ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
// Test switching
// ---------------------------------------------------------------------------

void showSplash(int mode) {
  tft.fillScreen(COLOR_BG);
  char buf[24];
  snprintf(buf, sizeof(buf), "TEST %d OF %d", mode + 1, NUM_MODES);
  drawLabel(buf, SIDE_MARGIN, rowY(1, MAX_ROWS));
  drawValue(MODES[mode].name, SIDE_MARGIN, rowY(2, MAX_ROWS), COLOR_HIGH, 0);
  drawHint("EJECT = next test", rowY(5, MAX_ROWS));
}

void enterMode(int mode) {
  currentMode = mode;
  Serial.printf("\n========== TEST %d OF %d: %s ==========\n",
                mode + 1, NUM_MODES, MODES[mode].name);
  showSplash(mode);
  delay(SPLASH_MS);
  tft.fillScreen(COLOR_BG);
  MODES[mode].enter();
}

// Called by keypad.ino
void onEject() {
  MODES[currentMode].exit();
  enterMode((currentMode + 1) % NUM_MODES);
}

void onMatrixKey(byte row, byte col) {
  Serial.printf("Key: %s (%d,%d)\n", KEY_NAMES[row][col], row, col);
  MODES[currentMode].onKey(row, col);
}

void onPinButton() {
  Serial.println("Key: PIN");
  MODES[currentMode].onPin();
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n\n=================================");
  Serial.println("Function Test (ESP32-S3, new PCBA)");
  Serial.println("=================================");

  // ESP32-S3 outputs. pinMode() before digitalWrite() — on core 3.x a write
  // to a pin that isn't set up yet is ignored.
  pinMode(I2S_SWITCH, OUTPUT);
  digitalWrite(I2S_SWITCH, LOW);        // TMUX1574 → I2S1 (this ESP32-S3)
  pinMode(DAC_MUTE, OUTPUT);
  digitalWrite(DAC_MUTE, HIGH);         // PCM5102A unmuted
  ledcAttach(LED_PIN, 5000, 12);
  ledcWrite(LED_PIN, 0);                // LED off until the LED test

  // Display
  tft.init();
  tft.setRotation(3);
  // Display backlight (GPIO9) on PWM so the LED test can dim it. Attached
  // AFTER tft.init(), which drives TFT_BL as a plain output; nothing may
  // digitalWrite() GPIO9 after this. Full brightness outside the LED test.
  ledcAttach(TFT_BL, 5000, 12);
  ledcWrite(TFT_BL, 4095);
  tft.fillScreen(COLOR_BG);

  // MCP23017 — needed for EJECT, so stop here if it's missing
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!mcp.begin_I2C(MCP23017_ADDR)) {
    Serial.println("ERROR: MCP23017 not found at 0x20. Halting.");
    drawValue("MCP23017 NOT FOUND (0x20)", SIDE_MARGIN, rowY(1, MAX_ROWS), COLOR_LOW, 0);
    drawLabel("Check I2C: SDA GPIO1, SCL GPIO2", SIDE_MARGIN, rowY(2, MAX_ROWS));
    drawLabel("Keypad (and EJECT) unavailable.", SIDE_MARGIN, rowY(3, MAX_ROWS));
    while (true) delay(1000);
  }
  Wire.setClock(400000);   // faster keypad scans, more headroom for audio
  Serial.println("MCP23017 found (I2C 400 kHz)");

  // Headunit pins: outputs latched LOW, inputs pulled up
  headunitOutputsLow();
  const uint8_t outs[] = { PIN_DAC_RELAY, PIN_RADIO_RELAY, PIN_AMP_MUTE, PIN_MODULE_ACTIVE };
  for (uint8_t p : outs) mcp.pinMode(p, OUTPUT);
  mcp.pinMode(PIN_ILLUM_SENSE, INPUT_PULLUP);
  mcp.pinMode(PIN_RADIO_SENSE, INPUT_PULLUP);
  mcp.pinMode(PIN_HU_LIGHTSW,  INPUT_PULLUP);

  initKeypad();

  // I2S1 — started once; outputs silence whenever the tone test isn't writing
  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.pin_bck         = I2S_BCK;
  cfg.pin_ws          = I2S_WS;
  cfg.pin_data        = I2S_DATA_OUT;
  cfg.sample_rate     = 44100;
  cfg.bits_per_sample = 16;
  cfg.channels        = 2;
  cfg.buffer_count    = 8;      // same as mp3player.ino (~93 ms)
  cfg.buffer_size     = 2048;
  if (!i2s.begin(cfg)) Serial.println("ERROR: i2s.begin() failed");

  // SD SPI bus (SD library drives CS itself)
  sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, -1);

  Serial.println("Press EJECT to step through the tests.");
  enterMode(0);
}

void loop() {
  scanKeypad();
  MODES[currentMode].loop();
  // The tone test is paced by its blocking I2S write; an extra delay would
  // only eat into the audio buffer
  if (MODES[currentMode].loop != toneLoop) delay(10);
}
