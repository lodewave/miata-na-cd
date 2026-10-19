/*
  Headunit I/O Test — new PCBA (primary ESP32-S3-WROOM-1-N8)

  Checks the signals between this module and the headunit via the MCP23017.
  Shows each signal's actual pin level on the display and logs every change
  to Serial (115200).

  Inputs (MCP23017 INPUT_PULLUP — read HIGH when nothing pulls them low).
  REVERSE LOGIC: the PCBA inverts these signals. The display shows the level
  READ by the ESP32; the headunit's actual signal is the opposite (HIGH on
  screen = LOW from the headunit, LOW on screen = HIGH from the headunit):
    ILLUM SENSE      GPB4
    RADIO SENSE      GPB5
    HU LIGHTSWITCH   GPB6

  Outputs (MCP23017, all LOW at start), toggled by keypad buttons:
    AMP MUTE         GPB2   TrackUp   (matrix 0,1)
    MODULE ACTIVE    GPB3   TrackDown (matrix 1,1)
    DAC RELAY        GPB0   FF        (matrix 2,1)
    RADIO RELAY      GPB1   REV       (matrix 3,1)

  Output levels shown are read back from the MCP23017 pins, not just the
  commanded value — an output that won't go HIGH (short, overload) shows it.

  Display: regular font (Font 2), white text on black, HIGH in green, LOW in
  red. Kept inside the visible window used by primary_esp32s3
  (320×118 at y offset 26, rotation 3).

  Board: ESP32S3 Dev Module (8MB flash, PSRAM disabled)
  Requires the Setup_MiataS3_ST7789_170x320.h TFT_eSPI setup.
*/

#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <TFT_eSPI.h>

// Same guard as primary_esp32s3/config.h
#if !defined(USER_SETUP_ID) || (USER_SETUP_ID != 900)
  #error "Wrong TFT_eSPI setup. Select Setup_MiataS3_ST7789_170x320.h in TFT_eSPI/User_Setup_Select.h"
#endif

// ---------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------

#define I2C_SDA       1
#define I2C_SCL       2
#define MCP23017_ADDR 0x20

// MCP23017 pin numbers: GPA0–7 = 0–7, GPB0–7 = 8–15
#define PIN_DAC_RELAY      8   // GPB0
#define PIN_RADIO_RELAY    9   // GPB1
#define PIN_AMP_MUTE      10   // GPB2
#define PIN_MODULE_ACTIVE 11   // GPB3
#define PIN_ILLUM_SENSE   12   // GPB4
#define PIN_RADIO_SENSE   13   // GPB5
#define PIN_HU_LIGHTSW    14   // GPB6

// Keypad (same as primary_esp32s3/config.h)
#define KEYPAD_ROW_3 0  // GPA0
#define KEYPAD_ROW_2 1  // GPA1
#define KEYPAD_ROW_1 2  // GPA2
#define KEYPAD_ROW_0 3  // GPA3
#define KEYPAD_COL_1 5  // GPA5
#define KEYPAD_COL_0 6  // GPA6
#define BTN_EJECT    4  // GPA4 (configured, not used here)
#define BTN_PIN      7  // GPA7 (configured, not used here)

// ---------------------------------------------------------------------------
// Display layout — visible window from primary_esp32s3/config.h
// ---------------------------------------------------------------------------

#define DISP_W      320
#define DISP_H      118
#define DISP_PAD_H  26
#define SIDE_MARGIN 5
#define FONT        2      // 16 px tall
#define ROW_H       16

#define COLOR_TEXT  TFT_WHITE
#define COLOR_HIGH  TFT_GREEN
#define COLOR_LOW   TFT_RED
#define COLOR_HINT  TFT_DARKGREY
#define COLOR_BG    TFT_BLACK

// Rows: 3 inputs, divider, 4 outputs → y 27 … 142 (window is 26 … 143)
#define INPUT_TOP   (DISP_PAD_H + 1)
#define DIVIDER_Y   (INPUT_TOP + 3 * ROW_H + 1)
#define OUTPUT_TOP  (DIVIDER_Y + 2)

// ---------------------------------------------------------------------------
// Signal table
// ---------------------------------------------------------------------------

struct Signal {
  const char* name;
  uint8_t     pin;
  bool        isOutput;
  int8_t      row, col;   // keypad button that toggles it (-1 = none)
  const char* hint;       // shown at the right of the row
  int16_t     y;          // screen row
  bool        commanded;  // outputs: level we set
  int8_t      shown;      // level last drawn (-1 = never drawn)
};

Signal signals[] = {
  { "ILLUM SENSE",    PIN_ILLUM_SENSE,   false, -1, -1, "GPB4 IN",       INPUT_TOP + 0 * ROW_H,  false, -1 },
  { "RADIO SENSE",    PIN_RADIO_SENSE,   false, -1, -1, "GPB5 IN",       INPUT_TOP + 1 * ROW_H,  false, -1 },
  { "HU LIGHTSWITCH", PIN_HU_LIGHTSW,    false, -1, -1, "GPB6 IN",       INPUT_TOP + 2 * ROW_H,  false, -1 },
  { "AMP MUTE",       PIN_AMP_MUTE,      true,   0,  1, "[TrackUp]",     OUTPUT_TOP + 0 * ROW_H, false, -1 },
  { "MODULE ACTIVE",  PIN_MODULE_ACTIVE, true,   1,  1, "[TrackDown]",   OUTPUT_TOP + 1 * ROW_H, false, -1 },
  { "DAC RELAY",      PIN_DAC_RELAY,     true,   2,  1, "[FF]",          OUTPUT_TOP + 2 * ROW_H, false, -1 },
  { "RADIO RELAY",    PIN_RADIO_RELAY,   true,   3,  1, "[REV]",         OUTPUT_TOP + 3 * ROW_H, false, -1 },
};
const int NUM_SIGNALS = sizeof(signals) / sizeof(signals[0]);

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

TFT_eSPI tft = TFT_eSPI();
Adafruit_MCP23X17 mcp;

int16_t valueX = 0;      // x where "HIGH"/"LOW" starts (aligned for all rows)
int16_t valueW = 0;      // width of "HIGH" — padding so LOW fully overwrites it

const byte ROW_PINS[] = { KEYPAD_ROW_0, KEYPAD_ROW_1, KEYPAD_ROW_2, KEYPAD_ROW_3 };
const byte COL_PINS[] = { KEYPAD_COL_0, KEYPAD_COL_1 };
const byte NUM_ROWS = 4, NUM_COLS = 2;
bool     lastPressed[NUM_ROWS][NUM_COLS]    = { false };
uint32_t pressStartTime[NUM_ROWS][NUM_COLS] = { 0 };
const uint32_t KEYPAD_DEBOUNCE_MS = 50;

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

void drawStatic() {
  tft.fillScreen(COLOR_BG);
  tft.setTextFont(FONT);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);

  // Value column starts after the widest "LABEL: "
  int16_t widest = 0;
  for (int i = 0; i < NUM_SIGNALS; i++) {
    String label = String(signals[i].name) + ": ";
    widest = max(widest, (int16_t)tft.textWidth(label, FONT));
  }
  valueX = SIDE_MARGIN + widest;
  valueW = tft.textWidth("HIGH", FONT);

  for (int i = 0; i < NUM_SIGNALS; i++) {
    Signal& s = signals[i];
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString(String(s.name) + ":", SIDE_MARGIN, s.y, FONT);

    tft.setTextColor(COLOR_HINT, COLOR_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(s.hint, DISP_W - SIDE_MARGIN, s.y, FONT);
    tft.setTextDatum(TL_DATUM);

    s.shown = -1;  // force value redraw
  }

  // Divider between inputs and outputs
  tft.drawFastHLine(0, DIVIDER_Y, DISP_W, COLOR_HINT);
}

void drawValue(Signal& s, bool level) {
  tft.setTextColor(level ? COLOR_HIGH : COLOR_LOW, COLOR_BG);
  tft.setTextPadding(valueW);  // pads "LOW" with black so no "H" remnant
  tft.drawString(level ? "HIGH" : "LOW", valueX, s.y, FONT);
  tft.setTextPadding(0);
  s.shown = level;
}

void showError(const char* line1, const char* line2) {
  tft.fillScreen(COLOR_BG);
  tft.setTextFont(FONT);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_LOW, COLOR_BG);
  tft.drawString(line1, SIDE_MARGIN, DISP_PAD_H + 20, FONT);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString(line2, SIDE_MARGIN, DISP_PAD_H + 40, FONT);
}

// ---------------------------------------------------------------------------
// Signals
// ---------------------------------------------------------------------------

void updateSignals() {
  for (int i = 0; i < NUM_SIGNALS; i++) {
    Signal& s = signals[i];
    bool level = mcp.digitalRead(s.pin);  // actual pin level (inputs and outputs)
    if (level != s.shown) {
      if (s.shown != -1) {
        Serial.printf("%-15s %s -> %s\n", s.name, s.shown ? "HIGH" : "LOW", level ? "HIGH" : "LOW");
      }
      if (s.isOutput && level != s.commanded) {
        Serial.printf("  WARNING: %s commanded %s but pin reads %s\n",
                      s.name, s.commanded ? "HIGH" : "LOW", level ? "HIGH" : "LOW");
      }
      drawValue(s, level);
    }
  }
}

void toggleOutput(Signal& s) {
  s.commanded = !s.commanded;
  mcp.digitalWrite(s.pin, s.commanded ? HIGH : LOW);
  Serial.printf("%s %s: set %s\n", s.hint, s.name, s.commanded ? "HIGH" : "LOW");
}

// ---------------------------------------------------------------------------
// Keypad
// ---------------------------------------------------------------------------

bool debounceButton(bool reading, bool& lastState, uint32_t& pressStart, uint32_t now) {
  if (reading && !lastState) {
    pressStart = now;
    lastState  = true;
  } else if (!reading && lastState) {
    lastState  = false;
    pressStart = 0;
  } else if (reading && lastState && pressStart != 0) {
    if ((now - pressStart) >= KEYPAD_DEBOUNCE_MS) {
      pressStart = 0;  // fire once per press
      return true;
    }
  }
  return false;
}

void onKeyPress(byte row, byte col) {
  for (int i = 0; i < NUM_SIGNALS; i++) {
    Signal& s = signals[i];
    if (s.isOutput && s.row == row && s.col == col) {
      toggleOutput(s);
      return;
    }
  }
  Serial.printf("Key (%d,%d) - no output assigned\n", row, col);
}

void handleKeypad() {
  uint32_t now = millis();
  for (byte r = 0; r < NUM_ROWS; r++) {
    mcp.digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(100);
    for (byte c = 0; c < NUM_COLS; c++) {
      bool reading = (mcp.digitalRead(COL_PINS[c]) == LOW);
      if (debounceButton(reading, lastPressed[r][c], pressStartTime[r][c], now)) {
        onKeyPress(r, c);
      }
    }
    mcp.digitalWrite(ROW_PINS[r], HIGH);
  }
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n\n=================================");
  Serial.println("Headunit I/O Test (ESP32-S3, new PCBA)");
  Serial.println("=================================\n");

  // Display
  tft.init();
  tft.setRotation(3);
  pinMode(TFT_BL, OUTPUT);       // pinMode before digitalWrite (core 3.x)
  digitalWrite(TFT_BL, HIGH);
  tft.fillScreen(COLOR_BG);

  // MCP23017
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!mcp.begin_I2C(MCP23017_ADDR)) {
    Serial.println("ERROR: MCP23017 not found at 0x20. Halting.");
    showError("MCP23017 NOT FOUND (0x20)", "Check I2C: SDA GPIO1, SCL GPIO2");
    while (true) delay(1000);
  }
  Serial.println("MCP23017 found");

  // Headunit signals
  for (int i = 0; i < NUM_SIGNALS; i++) {
    Signal& s = signals[i];
    if (s.isOutput) {
      mcp.digitalWrite(s.pin, LOW);   // latch LOW before enabling the output
      mcp.pinMode(s.pin, OUTPUT);
      s.commanded = false;
    } else {
      mcp.pinMode(s.pin, INPUT_PULLUP);
    }
  }
  Serial.println("Outputs GPB0-GPB3 set LOW, inputs GPB4-GPB6 INPUT_PULLUP");

  // Keypad
  for (byte c = 0; c < NUM_COLS; c++) mcp.pinMode(COL_PINS[c], INPUT_PULLUP);
  mcp.pinMode(BTN_EJECT, INPUT_PULLUP);
  mcp.pinMode(BTN_PIN,   INPUT_PULLUP);
  for (byte r = 0; r < NUM_ROWS; r++) {
    mcp.digitalWrite(ROW_PINS[r], HIGH);
    mcp.pinMode(ROW_PINS[r], OUTPUT);
  }

  drawStatic();
  updateSignals();

  Serial.println("\nInitial levels:");
  for (int i = 0; i < NUM_SIGNALS; i++) {
    Serial.printf("  %-15s %s\n", signals[i].name, signals[i].shown ? "HIGH" : "LOW");
  }
  Serial.println("\nToggle outputs: TrackUp=AMP MUTE, TrackDown=MODULE ACTIVE, FF=DAC RELAY, REV=RADIO RELAY\n");
}

void loop() {
  handleKeypad();
  updateSignals();
  delay(10);
}
