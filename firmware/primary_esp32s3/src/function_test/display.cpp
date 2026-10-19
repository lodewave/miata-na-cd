#include "function_test.h"

namespace ftest {

// ---------------------------------------------------------------------------
// Display helpers — headunit_io_test format
//
// Rows are 16 px (Font 2) starting 1 px below the top of the visible window.
// A grey divider can sit after any row; rows after it move down 3 px.
// With 7 rows + divider the bottom row ends at y 142 (window ends at 143).
// ---------------------------------------------------------------------------

// Screen y of a row. dividerAfter = number of rows above the divider
// (pass MAX_ROWS or more for "no divider").
int16_t rowY(int row, int dividerAfter) {
  return DISP_PAD_H + 1 + row * ROW_H + (row >= dividerAfter ? 3 : 0);
}

void drawDivider(int dividerAfter) {
  int16_t y = DISP_PAD_H + 1 + dividerAfter * ROW_H + 1;
  tft.drawFastHLine(0, y, DISP_W, COLOR_HINT);
}

// White label text, top-left aligned
void drawLabel(const char* text, int16_t x, int16_t y) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString(text, x, y, FONT);
}

// Coloured value. padW > 0 blanks that width first so a shorter value fully
// replaces a longer one (e.g. LOW over HIGH) without clearing the row.
void drawValue(const String& text, int16_t x, int16_t y, uint16_t color, int16_t padW) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(padW);
  tft.setTextColor(color, COLOR_BG);
  tft.drawString(text, x, y, FONT);
  tft.setTextPadding(0);
}

// "HIGH" in green / "LOW" in red
void drawLevel(bool high, int16_t x, int16_t y) {
  drawValue(high ? "HIGH" : "LOW", x, y, high ? COLOR_HIGH : COLOR_LOW,
            tft.textWidth("HIGH", FONT));
}

// Grey hint, right-aligned at the right margin
void drawHint(const String& text, int16_t y, int16_t padW) {
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(padW);
  tft.setTextColor(COLOR_HINT, COLOR_BG);
  tft.drawString(text, DISP_W - SIDE_MARGIN, y, FONT);
  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);
}

// x where values start so they line up after the widest "LABEL: "
int16_t labelColumn(const char* const* labels, int count, int16_t x) {
  int16_t widest = 0;
  for (int i = 0; i < count; i++) {
    widest = max(widest, (int16_t)tft.textWidth(String(labels[i]) + ": ", FONT));
  }
  return x + widest;
}

}  // namespace ftest
