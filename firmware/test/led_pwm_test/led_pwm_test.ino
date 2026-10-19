/*
  LED PWM Test — new PCBA (primary ESP32-S3-WROOM-1-N8)

  Manually set the duty cycle of the LED on GPIO4 (LED1_PWM) from the keypad.
  Starts at 50%.

    TrackUp    +1%      (matrix 0,1)
    TrackDown  -1%      (matrix 1,1)
    FF         +10%     (matrix 2,1)
    REV        -10%     (matrix 3,1)
    Play       LED on / off toggle (matrix 1,0)

  The value is clamped to 0–100%. Each press steps once (no auto-repeat).
  While the LED is OFF the duty can still be adjusted; the new value is used
  when it's switched back ON.

  Display: same format as headunit_io_test — regular font (Font 2), white
  text on black, inside the primary_esp32s3 visible window (320×118 at y
  offset 26, rotation 3). The duty value is green, or red at 0% (LED off).
  Serial (115200) logs every change.

  PWM: 5 kHz, 12-bit (0–4095), Arduino-ESP32 core 3.x LEDC API.

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
// Pins / PWM
// ---------------------------------------------------------------------------

#define LED_PIN       4       // LED1_PWM
#define PWM_FREQ_HZ   5000
#define PWM_BITS      12
#define PWM_MAX       ((1 << PWM_BITS) - 1)   // 4095 = 100%
#define START_PERCENT 50

#define I2C_SDA       1
#define I2C_SCL       2
#define MCP23017_ADDR 0x20

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

// Rows: title/LED state, duty, divider, 5 instructions → y 27 … 141
// (window 26 … 143)
#define TITLE_Y     (DISP_PAD_H + 1)
#define DUTY_Y      (TITLE_Y + ROW_H)
#define DIVIDER_Y   (DUTY_Y + ROW_H + 1)
#define HELP_TOP    (DIVIDER_Y + 2)

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

struct Button {
  const char* name;
  byte row, col;
  int  step;     // % change per press; 0 = on/off toggle
};

const Button BUTTONS[] = {
  { "TrackUp",   0, 1,  +1 },
  { "TrackDown", 1, 1,  -1 },
  { "FF",        2, 1, +10 },
  { "REV",       3, 1, -10 },
  { "Play",      1, 0,   0 },
};
const int NUM_BUTTONS = sizeof(BUTTONS) / sizeof(BUTTONS[0]);

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

TFT_eSPI tft = TFT_eSPI();
Adafruit_MCP23X17 mcp;
bool mcpOK = false;

int  dutyPercent = START_PERCENT;
bool ledOn = true;
int16_t valueX = 0;    // x where the duty value starts
int16_t valueW = 0;    // width of "100%" — padding so shorter values fully overwrite
int16_t stateX = 0;    // x where "ON"/"OFF" starts (title row, right side)
int16_t stateW = 0;    // width of "OFF"

const byte ROW_PINS[] = { KEYPAD_ROW_0, KEYPAD_ROW_1, KEYPAD_ROW_2, KEYPAD_ROW_3 };
const byte COL_PINS[] = { KEYPAD_COL_0, KEYPAD_COL_1 };
const byte NUM_ROWS = 4, NUM_COLS = 2;
bool     lastPressed[NUM_ROWS][NUM_COLS]    = { false };
uint32_t pressStartTime[NUM_ROWS][NUM_COLS] = { 0 };
const uint32_t KEYPAD_DEBOUNCE_MS = 50;

// ---------------------------------------------------------------------------
// PWM
// ---------------------------------------------------------------------------

uint32_t percentToDuty(int pct) {
  return ((uint32_t)pct * PWM_MAX + 50) / 100;   // rounded
}

// Duty actually sent to the pin (0 while the LED is switched off)
uint32_t outputDuty() {
  return ledOn ? percentToDuty(dutyPercent) : 0;
}

void applyDuty() {
  ledcWrite(LED_PIN, outputDuty());
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

void drawStatic() {
  tft.fillScreen(COLOR_BG);
  tft.setTextFont(FONT);
  tft.setTextPadding(0);

  // Title row: name + pin on the left, "LED: ON/OFF" on the right
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString("LED PWM TEST", SIDE_MARGIN, TITLE_Y, FONT);
  tft.setTextColor(COLOR_HINT, COLOR_BG);
  tft.drawString("GPIO4 5kHz", SIDE_MARGIN + tft.textWidth("LED PWM TEST  ", FONT),
                 TITLE_Y, FONT);
  stateW = tft.textWidth("OFF", FONT);
  stateX = DISP_W - SIDE_MARGIN - stateW;
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("LED: ", stateX, TITLE_Y, FONT);

  // Duty label (value drawn by drawDuty)
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  String label = "PWM DUTY: ";
  tft.drawString(label, SIDE_MARGIN, DUTY_Y, FONT);
  valueX = SIDE_MARGIN + tft.textWidth(label, FONT);
  valueW = tft.textWidth("100%", FONT);

  tft.drawFastHLine(0, DIVIDER_Y, DISP_W, COLOR_HINT);

  // Instructions, values aligned after the widest button name
  int16_t widest = 0;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    widest = max(widest, (int16_t)tft.textWidth(String(BUTTONS[i].name) + ": ", FONT));
  }
  for (int i = 0; i < NUM_BUTTONS; i++) {
    int16_t y = HELP_TOP + i * ROW_H;
    char step[12];
    if (BUTTONS[i].step == 0) snprintf(step, sizeof(step), "ON / OFF");
    else                      snprintf(step, sizeof(step), "%+d%%", BUTTONS[i].step);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString(String(BUTTONS[i].name) + ":", SIDE_MARGIN, y, FONT);
    tft.drawString(step, SIDE_MARGIN + widest, y, FONT);
  }
}

void drawDuty() {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", dutyPercent);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(dutyPercent == 0 ? COLOR_LOW : COLOR_HIGH, COLOR_BG);
  tft.setTextPadding(valueW);   // clears leftover digits, e.g. 100% → 99%
  tft.drawString(buf, valueX, DUTY_Y, FONT);
  tft.setTextPadding(0);

  // Raw 12-bit value actually on the pin, as a grey hint on the right
  char raw[16];
  snprintf(raw, sizeof(raw), "%lu / %d", (unsigned long)outputDuty(), PWM_MAX);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COLOR_HINT, COLOR_BG);
  tft.setTextPadding(tft.textWidth("4095 / 4095", FONT));
  tft.drawString(raw, DISP_W - SIDE_MARGIN, DUTY_Y, FONT);
  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);
}

void drawState() {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ledOn ? COLOR_HIGH : COLOR_LOW, COLOR_BG);
  tft.setTextPadding(stateW);   // clears the "F" when OFF → ON
  tft.drawString(ledOn ? "ON" : "OFF", stateX, TITLE_Y, FONT);
  tft.setTextPadding(0);
}

void showError(const char* line1, const char* line2) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_LOW, COLOR_BG);
  tft.drawString(line1, SIDE_MARGIN, HELP_TOP, FONT);
  tft.fillRect(0, HELP_TOP + ROW_H, DISP_W, 4 * ROW_H, COLOR_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString(line2, SIDE_MARGIN, HELP_TOP + ROW_H, FONT);
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
  for (int i = 0; i < NUM_BUTTONS; i++) {
    const Button& b = BUTTONS[i];
    if (b.row == row && b.col == col) {
      if (b.step == 0) {                 // Play: on/off toggle
        ledOn = !ledOn;
        applyDuty();
        drawState();
        drawDuty();                      // raw value changes (0 when off)
        Serial.printf("%s: LED %s (set %d%%, duty %lu)\n", b.name, ledOn ? "ON" : "OFF",
                      dutyPercent, (unsigned long)outputDuty());
        return;
      }
      int before = dutyPercent;
      dutyPercent = constrain(dutyPercent + b.step, 0, 100);
      if (dutyPercent == before) {
        Serial.printf("%s: already at %d%%\n", b.name, dutyPercent);
        return;
      }
      applyDuty();
      drawDuty();
      Serial.printf("%s %+d%%: %d%% -> %d%% (duty %lu%s)\n", b.name, b.step, before,
                    dutyPercent, (unsigned long)outputDuty(), ledOn ? "" : ", LED OFF");
      return;
    }
  }
  Serial.printf("Key (%d,%d) - not used in this test\n", row, col);
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
  Serial.println("LED PWM Test (ESP32-S3, GPIO4)");
  Serial.println("=================================\n");

  // PWM first, so the LED is at 50% even if something below fails
  if (!ledcAttach(LED_PIN, PWM_FREQ_HZ, PWM_BITS)) {
    Serial.println("ERROR: ledcAttach() failed on GPIO4");
  }
  applyDuty();
  Serial.printf("PWM on GPIO%d: %d Hz, %d-bit, start %d%%\n",
                LED_PIN, PWM_FREQ_HZ, PWM_BITS, dutyPercent);

  // Display
  tft.init();
  tft.setRotation(3);
  pinMode(TFT_BL, OUTPUT);       // pinMode before digitalWrite (core 3.x)
  digitalWrite(TFT_BL, HIGH);
  drawStatic();
  drawState();
  drawDuty();

  // MCP23017 + keypad
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!mcp.begin_I2C(MCP23017_ADDR)) {
    Serial.println("ERROR: MCP23017 not found at 0x20 - keypad disabled");
    showError("MCP23017 NOT FOUND (0x20)", "Keypad disabled. LED held at 50%.");
    return;
  }
  mcpOK = true;
  for (byte c = 0; c < NUM_COLS; c++) mcp.pinMode(COL_PINS[c], INPUT_PULLUP);
  mcp.pinMode(BTN_EJECT, INPUT_PULLUP);
  mcp.pinMode(BTN_PIN,   INPUT_PULLUP);
  for (byte r = 0; r < NUM_ROWS; r++) {
    mcp.digitalWrite(ROW_PINS[r], HIGH);
    mcp.pinMode(ROW_PINS[r], OUTPUT);
  }
  Serial.println("Keypad ready: TrackUp +1%, TrackDown -1%, FF +10%, REV -10%, Play on/off\n");
}

void loop() {
  if (mcpOK) handleKeypad();
  delay(10);
}
