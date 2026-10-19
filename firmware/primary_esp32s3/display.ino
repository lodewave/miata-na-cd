// ---------------------------------------------------------------------------
// Display Functions — sprite (off-screen frame) rendering
//
// Every screen is drawn into `frame`, a 320×118 16-bit TFT_eSprite that
// covers exactly the visible window (75.5 KB of RAM), and then pushed to the
// panel at y = DISP_PAD_H. The panel only ever receives finished pixels, so:
//   - scroll steps no longer blank the text band before reprinting it
//   - screen changes (input switch, splash, BT WAIT) no longer flash black
//
// Push cost at 40 MHz SPI: full window ≈ 15 ms, one 40 px text band ≈ 5 ms.
// Scroll steps push only the band that moved.
//
// All coordinates in this file are window-relative (y = 0 is the top of the
// visible window). WY() converts to canvas coordinates.
//
// Fallback: if the sprite can't be allocated (not enough contiguous RAM),
// drawing goes straight to the panel as before (with the old flicker), and
// a warning is printed at boot.
// ---------------------------------------------------------------------------

ScrollLine artistLine;
ScrollLine titleLine;

TFT_eSprite frame = TFT_eSprite(&tft);
static TFT_eSPI* canvas  = &tft;    // where drawing goes: &frame or &tft
static int       canvasY = 0;       // window y → canvas y offset
static bool      useSprite = false;

#define WY(y) ((y) + canvasY)

// Font metrics (set once in initDisplay)
static int labelH = 16;   // Font 2 height
static int segH   = 38;   // DSEG14 18pt yAdvance

// Window-relative layout of the two scroll bands
#define BAND_OFFSET (TOP_MARGIN + labelH + LABEL_GAP)   // label above the band
#define BAND_H      (HALF_H - BAND_OFFSET)              // 40 px

// ---------------------------------------------------------------------------
// Frame helpers
// ---------------------------------------------------------------------------

// Clear the whole visible window (in the frame, or on the panel in fallback)
static void clearWindow() {
  canvas->fillRect(0, WY(0), DISP_W, DISP_H, BG_COLOR);
}

// Send the whole frame to the panel
static void presentAll() {
  if (useSprite) frame.pushSprite(0, DISP_PAD_H);
}

// Send only window rows [y, y+h) to the panel
static void presentRows(int y, int h) {
  if (useSprite) frame.pushSprite(0, DISP_PAD_H + y, 0, y, DISP_W, h);
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void initDisplay() {
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(BG_COLOR);         // whole panel once; only the window is drawn after this
  // Backlight: still off here. setup() holds LCD_BL_PIN LOW, and TFT_eSPI
  // doesn't know the pin (no TFT_BL in its setup, so the ST7789 init can't
  // switch it on), so the panel's power-up noise is never seen.
  // lighting.ino turns it on after the first screen.

  // Allocate the frame early in setup(), before the MP3 decoder and I2S
  // buffers, while a large contiguous block is still free.
  Serial.printf("DISPLAY: free heap %lu, largest block %lu\n",
                (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
  frame.setColorDepth(16);
  if (frame.createSprite(DISP_W, DISP_H) != nullptr) {
    useSprite = true;
    canvas    = &frame;
    canvasY   = 0;
    frame.fillSprite(BG_COLOR);
    Serial.printf("DISPLAY: sprite frame %dx%d OK (%d bytes)\n", DISP_W, DISP_H, DISP_W * DISP_H * 2);
  } else {
    useSprite = false;
    canvas    = &tft;
    canvasY   = DISP_PAD_H;
    Serial.println("DISPLAY: WARNING - sprite allocation failed, drawing direct (will flicker)");
  }
  canvas->setTextWrap(false, false);

  labelH = canvas->fontHeight(LABEL_FONT);
  canvas->setFreeFont(SEGMENT_FONT);
  segH = canvas->fontHeight();

  prepareScroll(artistLine, "");
  prepareScroll(titleLine, "");
}

// ---------------------------------------------------------------------------
// Static screens
// ---------------------------------------------------------------------------

// Two centered-height lines, e.g. "RADIO" / "INPUT"
void showStaticDisplay(const char* line1, const char* line2) {
  if (!displayIsMine()) return;     // settings menu / function test own the screen
  clearWindow();
  canvas->setFreeFont(SEGMENT_FONT);
  canvas->setTextColor(TEXT_COLOR);

  canvas->setCursor(SIDE_MARGIN, WY((HALF_H / 2) + (segH / 2)));
  canvas->print(line1);
  canvas->setCursor(SIDE_MARGIN, WY(HALF_H + (HALF_H / 2) + (segH / 2)));
  canvas->print(line2);

  presentAll();
}

// One line in the middle of the window, e.g. "BT WAIT".
// Clears the window first (callers no longer clear the screen themselves).
void showSplash(const char* text) {
  if (!displayIsMine()) return;
  clearWindow();
  canvas->setFreeFont(SEGMENT_FONT);
  canvas->setTextColor(TEXT_COLOR);
  canvas->setCursor(SIDE_MARGIN, WY((DISP_H / 2) + (segH / 2)));
  canvas->print(text);
  presentAll();
}

// ---------------------------------------------------------------------------
// Scrolling artist / title
// ---------------------------------------------------------------------------

// Spaces are drawn as real (blank) spaces. The DSEG14 space glyph was only
// 7 px wide versus 29 px for every other character, so its xAdvance was
// widened to 29 in DSEG14Classic_Regular_18.h — a space now takes one full
// character cell, like the gap on a real segment display.
void prepareScroll(ScrollLine &sl, const String &text) {
  canvas->setFreeFont(SEGMENT_FONT);
  sl.text         = text;
  sl.charOffset   = 0;
  sl.pausing      = true;
  sl.nextScrollMs = millis() + PAUSE_MS;
  sl.scrolling    = (canvas->textWidth(sl.text) > (DISP_W - SIDE_MARGIN * 2));
}

// Baseline of the DSEG14 text inside a section (window-relative).
// fontHeight() is the yAdvance (38 px); the height is centered in the space
// below the label. GFX glyphs extend upward from the baseline.
static int textBaselineY(int sectionTop) {
  int available = HALF_H - BAND_OFFSET;
  int topPad = (available - segH) / 2;
  if (topPad < 0) topPad = 0;
  return sectionTop + BAND_OFFSET + topPad + segH;
}

// Redraw one text band into the canvas (does not push)
static void drawBand(ScrollLine &sl, int sectionTop) {
  int bandTop = sectionTop + BAND_OFFSET;
  canvas->fillRect(0, WY(bandTop), DISP_W, BAND_H, BG_COLOR);
  if (sl.text.length() == 0) return;

  canvas->setFreeFont(SEGMENT_FONT);
  canvas->setTextColor(TEXT_COLOR);

  String visible = sl.text.substring(sl.charOffset);
  int maxW = DISP_W - SIDE_MARGIN * 2;
  while (visible.length() > 0 && canvas->textWidth(visible) > maxW) {
    visible.remove(visible.length() - 1);
  }

  canvas->setCursor(SIDE_MARGIN, WY(textBaselineY(sectionTop)));
  canvas->print(visible);
}

// Label + band for one half of the window (does not push)
static void drawSection(ScrollLine &sl, int sectionTop, const char* label) {
  canvas->setTextFont(LABEL_FONT);
  canvas->setTextColor(LABEL_COLOR, BG_COLOR);
  canvas->setCursor(SIDE_MARGIN, WY(sectionTop + TOP_MARGIN));
  canvas->print(label);
  drawBand(sl, sectionTop);
}

bool tickScroll(ScrollLine &sl) {
  if (!sl.scrolling) return false;
  if ((int32_t)(millis() - sl.nextScrollMs) < 0) return false;   // wrap-safe

  if (sl.pausing) {
    sl.pausing      = false;
    sl.nextScrollMs = millis() + scrollIntervalMs();   // "Scroll Speed" setting
    return false;
  }

  sl.charOffset++;

  if (sl.charOffset >= (int)sl.text.length()) {
    sl.charOffset   = 0;
    sl.pausing      = true;
    sl.nextScrollMs = millis() + PAUSE_MS;
  } else {
    sl.nextScrollMs = millis() + scrollIntervalMs();
  }

  return true;
}

// Full ARTIST / TITLE screen
void renderAll() {
  if (!displayIsMine()) return;
  clearWindow();
  drawSection(artistLine, 0, "ARTIST");
  canvas->drawFastHLine(0, WY(HALF_H + 1), DISP_W, LABEL_COLOR);   // divider
  drawSection(titleLine, HALF_H, "TITLE");
  presentAll();
}

// Advance the scroll animation; redraw and push only the band(s) that moved
void updateDisplay() {
  if (!displayIsMine()) return;
  bool artistMoved = tickScroll(artistLine);
  bool titleMoved  = tickScroll(titleLine);

  if (artistMoved) {
    drawBand(artistLine, 0);
    presentRows(BAND_OFFSET, BAND_H);
  }
  if (titleMoved) {
    drawBand(titleLine, HALF_H);
    presentRows(HALF_H + BAND_OFFSET, BAND_H);
  }
}

// ---------------------------------------------------------------------------
// Settings menu drawing (used by settings_menu.ino)
//
// Regular Font 2 (16 px), drawn into the same sprite frame as the main
// screens and pushed in one go. Coordinates are window-relative.
// ---------------------------------------------------------------------------

void displayMenuBegin() {
  clearWindow();
}

void displayMenuText(const String& text, int16_t x, int16_t y, uint16_t color, uint8_t datum) {
  canvas->setTextFont(LABEL_FONT);
  canvas->setTextDatum(datum);
  canvas->setTextColor(color, BG_COLOR);
  canvas->drawString(text, x, WY(y), LABEL_FONT);
  canvas->setTextDatum(TL_DATUM);
}

void displayMenuLine(int16_t y, uint16_t color) {
  canvas->drawFastHLine(0, WY(y), DISP_W, color);
}

void displayMenuEnd() {
  presentAll();
}

int16_t displayMenuTextWidth(const String& text) {
  return canvas->textWidth(text, LABEL_FONT);
}
