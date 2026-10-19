#include "function_test.h"

namespace ftest {

// ---------------------------------------------------------------------------
// TEST 1 — KEYPAD
//
// Screen (two columns laid out like the matrix, then direct buttons):
//
//   unused:    OFF          TrackUp:   OFF
//   Play:      OFF          TrackDown: OFF
//   Scan:      OFF          FF:        OFF
//   RPT:       OFF          REV:       OFF
//   ─────────────────────────────────────────
//   PIN:       OFF          EJECT:     next test
//   LAST: TrackUp (0,1)  GPA3/GPA5              12
//
// ON (green) while a button is held, OFF (red) otherwise. LAST shows the most
// recent press with its row/column pins; the grey number is the total count.
// EJECT can't be shown held here — pressing it moves to the next test.
// "unused" is matrix position (0,0), which has no button; it should always
// read OFF. ON means ROW0 (GPA3) and COL0 (GPA6) are connected — a short,
// solder bridge, or a missing/reversed diode letting another key ghost.
// ---------------------------------------------------------------------------

#define KP_DIVIDER 4
#define KP_COL2_X  165

static int16_t kpValX[NUM_COLS];
static int16_t kpOnOffW;
static int8_t  kpShown[NUM_ROWS][NUM_COLS];
static int8_t  kpPinShown;
static uint32_t kpPresses;

static int16_t kpColX(byte col) { return col == 0 ? SIDE_MARGIN : KP_COL2_X; }

static void kpDrawState(bool on, int16_t x, int16_t y) {
  drawValue(on ? "ON" : "OFF", x, y, on ? COLOR_HIGH : COLOR_LOW, kpOnOffW);
}

void keypadEnter() {
  const char* col0[] = { KEY_NAMES[0][0], KEY_NAMES[1][0], KEY_NAMES[2][0], KEY_NAMES[3][0], "PIN" };
  const char* col1[] = { KEY_NAMES[0][1], KEY_NAMES[1][1], KEY_NAMES[2][1], KEY_NAMES[3][1], "EJECT" };
  kpValX[0] = labelColumn(col0, 5, kpColX(0));
  kpValX[1] = labelColumn(col1, 5, kpColX(1));
  kpOnOffW  = tft.textWidth("OFF", FONT);

  for (byte r = 0; r < NUM_ROWS; r++) {
    for (byte c = 0; c < NUM_COLS; c++) {
      drawLabel((String(KEY_NAMES[r][c]) + ":").c_str(), kpColX(c), rowY(r, KP_DIVIDER));
      kpShown[r][c] = -1;
    }
  }
  drawDivider(KP_DIVIDER);

  int16_t y4 = rowY(4, KP_DIVIDER);
  drawLabel("PIN:", kpColX(0), y4);
  drawLabel("EJECT:", kpColX(1), y4);
  drawValue("next test", kpValX[1], y4, COLOR_HINT, 0);
  kpPinShown = -1;

  int16_t y5 = rowY(5, KP_DIVIDER);
  drawLabel("LAST:", SIDE_MARGIN, y5);
  drawValue("-", SIDE_MARGIN + tft.textWidth("LAST: ", FONT), y5, COLOR_HINT, 0);
  kpPresses = 0;

  Serial.println("Press any button. EJECT moves to the next test.");
}

void keypadExit() {}

void keypadLoop() {
  for (byte r = 0; r < NUM_ROWS; r++) {
    for (byte c = 0; c < NUM_COLS; c++) {
      if (kpShown[r][c] != matrixHeld[r][c]) {
        kpShown[r][c] = matrixHeld[r][c];
        kpDrawState(matrixHeld[r][c], kpValX[c], rowY(r, KP_DIVIDER));
      }
    }
  }
  if (kpPinShown != pinHeld) {
    kpPinShown = pinHeld;
    kpDrawState(pinHeld, kpValX[0], rowY(4, KP_DIVIDER));
  }
}

static void kpShowLast(const String& text) {
  int16_t y = rowY(5, KP_DIVIDER);
  int16_t x = SIDE_MARGIN + tft.textWidth("LAST: ", FONT);
  kpPresses++;
  drawValue(text, x, y, COLOR_HIGH, DISP_W - SIDE_MARGIN - x - 40);
  drawHint(String(kpPresses), y, 35);
}

void keypadKey(byte r, byte c) {
  char buf[48];
  // row pin / column pin
  snprintf(buf, sizeof(buf), "%s (%d,%d)  GPA%d/GPA%d", KEY_NAMES[r][c], r, c,
           ROW_PINS[r], COL_PINS[c]);
  kpShowLast(buf);
}

void keypadPin() {
  kpShowLast("PIN  GPA7");
}

}  // namespace ftest
