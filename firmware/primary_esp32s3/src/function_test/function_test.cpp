// ---------------------------------------------------------------------------
// Function Test — built into the main firmware
//
// Started from the settings menu ("Function Test", FF or REV). Five board
// tests; EJECT moves to the next one. Hold PIN for 1 s to leave: the ESP32
// restarts, so the main firmware comes back up in a clean state (relays, SD
// card, I2S rate, backlights, MP3 position all re-initialised; settings are
// saved before the test starts).
//
//   1. KEYPAD        — live state of every button + last button pressed
//   2. LED PWM       — button LED (GPIO4, starts 50%) and display backlight
//                      (GPIO9, starts 100%). RPT selects which one; then
//                      TrackUp/Down ±1%, FF/REV ±10%, Play on/off
//   3. HEADUNIT I/O  — GPB4–6 sense inputs, GPB0–3 outputs toggled by
//                      TrackUp/TrackDown/FF/REV
//   4. SD CARD       — waits for Play, then mount status, type, size, MP3
//                      count, warnings, read speed. Play again re-scans
//   5. I2S TONE      — L 440 / R 660 / both 550 Hz sequence (Play) or a
//                      continuous 440 Hz tone L/R/both (RPT); TrackUp/Down
//                      volume ±1 dB from -12 dBFS; FF = DAC MUTE, REV = AMP
//                      MUTE. AMP VOLUME LOW.
//
// Every test starts from a known state when entered and cleans up when left:
//   - MCP23017 GPB0–GPB3 (relays, AMP MUTE, MODULE ACTIVE) LOW between tests
//   - Button LED off and display backlight 100% outside the LED test
//   - Tones only play in the tone test
//   - SD card unmounted outside the SD test
//
// The keypad is scanned by the main firmware (keypad.ino); actions.ino
// routes button events here while uiMode == UI_FUNCTION_TEST. The main loop
// suspends MP3, Bluetooth UART and the backlight logic while the test runs.
//
// Files (src/function_test/, compiled as C++):
//   function_test.cpp  — this file: entry/exit, test switching
//   function_test.h    — internal declarations, layout, shared objects
//   function_test_api.h — what the main firmware calls
//   display.cpp        — drawing helpers
//   mode_keypad.cpp, mode_led.cpp, mode_headunit.cpp, mode_sd.cpp,
//   mode_tone.cpp      — one file per test
// ---------------------------------------------------------------------------

#include "function_test.h"

namespace ftest {

static const TestMode MODES[] = {
  { "KEYPAD",       keypadEnter,   keypadExit,   keypadLoop,   keypadKey,   keypadPin   },
  { "LED PWM",      ledEnter,      ledExit,      ledLoop,      ledKey,      ledPin      },
  { "HEADUNIT I/O", headunitEnter, headunitExit, headunitLoop, headunitKey, headunitPin },
  { "SD CARD",      sdEnter,       sdExit,       sdLoop,       sdKey,       sdPin       },
  { "I2S TONE",     toneEnter,     toneExit,     toneLoop,     toneKey,     tonePin     },
};
static const int NUM_MODES = sizeof(MODES) / sizeof(MODES[0]);
static int  currentMode = 0;
static bool ready = false;     // false if the MCP23017 is missing

#define SPLASH_MS 900

// GPB0–GPB3 all LOW: relays on Radio path, AMP MUTE off, MODULE ACTIVE off
void headunitOutputsLow() {
  const uint8_t outs[] = { DAC_RELAY_PIN, RADIO_RELAY_PIN, PIN_AMP_MUTE, PIN_MODULE_ACTIVE };
  for (uint8_t p : outs) mcp.digitalWrite(p, LOW);
}

static void showModeSplash(int mode) {
  tft.fillScreen(COLOR_BG);
  char buf[24];
  snprintf(buf, sizeof(buf), "TEST %d OF %d", mode + 1, NUM_MODES);
  drawLabel(buf, SIDE_MARGIN, rowY(1, MAX_ROWS));
  drawValue(MODES[mode].name, SIDE_MARGIN, rowY(2, MAX_ROWS), COLOR_HIGH, 0);
  drawHint("EJECT = next test", rowY(5, MAX_ROWS));
  drawHint("hold PIN 1 s = exit (restart)", rowY(6, MAX_ROWS));
}

static void enterMode(int mode) {
  currentMode = mode;
  Serial.printf("\n========== TEST %d OF %d: %s ==========\n",
                mode + 1, NUM_MODES, MODES[mode].name);
  showModeSplash(mode);
  delay(SPLASH_MS);
  tft.fillScreen(COLOR_BG);
  MODES[mode].enter();
}

}  // namespace ftest

// ---------------------------------------------------------------------------
// API (function_test_api.h)
// ---------------------------------------------------------------------------

using namespace ftest;

void ftestBegin() {
  Serial.println("\n=================================");
  Serial.println("Function Test (hold PIN 1 s to exit)");
  Serial.println("=================================");

  // Audio path: I2S1 into the DAC, unmuted (pins set up by initMP3)
  digitalWrite(I2S_SWITCH, I2S_SWITCH_MP3);
  digitalWrite(DAC_MUTE, DAC_MUTE_LEVEL);

  // Backlights (already on PWM via initLighting): button LED off, LCD full
  ledcWrite(BUTTON_BL_PIN, 0);
  ledcWrite(LCD_BL_PIN, BL_PWM_MAX);

  tft.fillScreen(COLOR_BG);

  if (!mcp_initialized) {
    Serial.println("ERROR: MCP23017 not found - keypad unavailable. Power-cycle to exit.");
    drawValue("MCP23017 NOT FOUND (0x20)", SIDE_MARGIN, rowY(1, MAX_ROWS), COLOR_LOW, 0);
    drawLabel("Check I2C: SDA GPIO1, SCL GPIO2", SIDE_MARGIN, rowY(2, MAX_ROWS));
    drawLabel("Keypad unavailable. Power-cycle to exit.", SIDE_MARGIN, rowY(3, MAX_ROWS));
    ready = false;
    return;
  }

  // Headunit outputs (relays are already outputs; AMP MUTE / MODULE ACTIVE
  // are only driven by the test): latch LOW, then enable
  headunitOutputsLow();
  mcp.pinMode(PIN_AMP_MUTE, OUTPUT);
  mcp.pinMode(PIN_MODULE_ACTIVE, OUTPUT);

  // I2S1 restarted at 44.1 kHz (an MP3 may have changed the rate, or I2S may
  // not be running at all if there was no SD card). Outputs silence whenever
  // the tone test isn't writing.
  mp3_i2s.end();
  i2s1Begin();   // same config as the MP3 player (logs on failure)

  ready = true;
  enterMode(0);
}

void ftestLoop() {
  if (!ready) { delay(10); return; }
  MODES[currentMode].loop();
  // The tone test is paced by its blocking I2S write; other tests idle here
  if (MODES[currentMode].loop != toneLoop) delay(10);
}

void ftestMatrixKey(uint8_t row, uint8_t col) {
  if (!ready) return;
  Serial.printf("Key: %s (%d,%d)\n", KEY_NAMES[row][col], row, col);
  MODES[currentMode].onKey(row, col);
}

void ftestEject() {
  if (!ready) return;
  MODES[currentMode].exit();
  enterMode((currentMode + 1) % NUM_MODES);
}

void ftestPin() {
  if (!ready) return;
  Serial.println("Key: PIN");
  MODES[currentMode].onPin();
}

void ftestExit() {
  Serial.println("Function test: exit - restarting");
  if (ready) MODES[currentMode].exit();   // e.g. stop a tone before restart
  tft.fillScreen(COLOR_BG);
  drawValue("RESTARTING...", SIDE_MARGIN, rowY(2, MAX_ROWS), COLOR_HIGH, 0);
  Serial.flush();
  delay(500);
  ESP.restart();
}
