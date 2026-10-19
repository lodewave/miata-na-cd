/*
  ST7789 Display Test — new PCBA (primary ESP32-S3-WROOM-1-N8)

  Step-by-step display bring-up. Watch the display AND the Serial Monitor
  (115200). Each step prints before it runs, so if the board hangs or
  reboots, the last line printed shows where.

    Step 1 — Print the TFT_eSPI setup that was actually compiled in
    Step 2 — Blink the backlight 3× (before any SPI) — tests SPI1_BL only
    Step 3 — tft.init()  (SPI bus + reset + ST7789 init sequence)
    Step 4 — Full-screen colour fills: RED, GREEN, BLUE, WHITE, BLACK
    Step 5 — Layout screen: full-panel outline + project visible window
    Step 6 — Sprite demo: same scrolling DSEG14 text drawn two ways,
             top band = DIRECT (fillRect + print, like display.ino today)
             bottom band = SPRITE (drawn off-screen, pushed in one go)
               Phase 1: character-step scroll (what the firmware does now,
                        sped up to 300 ms/step so flicker is easy to see)
               Phase 2: smooth 2-pixel scroll at ~30 fps (only practical
                        with a sprite)
             Serial prints average draw time per frame for each method.
    Loop   — Repeats the colour fills, layout, and sprite demo

  Board: ESP32S3 Dev Module (8MB flash, PSRAM disabled)

  Display pins (ESP32-S3 : PCBA net):
    GPIO9  : SPI1_BL     GPIO10 : SPI1_RST    GPIO11 : SPI1_DC
    GPIO12 : SPI1_CS     GPIO13 : SPI1_SCLK   GPIO14 : SPI1_MOSI

  Pins come from TFT_eSPI/User_Setups/Setup_MiataS3_ST7789_170x320.h,
  which must be selected in TFT_eSPI/User_Setup_Select.h.
*/

#include <SPI.h>
#include <TFT_eSPI.h>
#include "DSEG14Classic_Regular_18.h"  // copy of primary_esp32s3/ font

#define SEGMENT_FONT &DSEG14Classic_Regular18pt7b

// Same guard as primary_esp32s3/config.h — refuse to build with the wrong setup
#if !defined(USER_SETUP_ID) || (USER_SETUP_ID != 900)
  #error "Wrong TFT_eSPI setup. Select Setup_MiataS3_ST7789_170x320.h in TFT_eSPI/User_Setup_Select.h"
#endif

// Visible window used by the main firmware (rotation 3, landscape)
#define DISP_W      320
#define DISP_H      118
#define DISP_PAD_H  26
#define TEXT_COLOR  0x54E4  // project yellow-green

// Sprite demo layout — matches primary_esp32s3/display.ino (two half-height
// sections, each a small label above a 40 px text band)
#define HALF_H       (DISP_H / 2)                 // 59
#define SIDE_MARGIN  5
#define LABEL_FONT   2
#define TOP_SEC      DISP_PAD_H                   // 26
#define BOT_SEC      (DISP_PAD_H + HALF_H)        // 85
#define BAND_OFFSET  19   // TOP_MARGIN 2 + label 16 + gap 1
#define BAND_H       40
#define BASELINE     39   // DSEG14 baseline inside the band (same as firmware)
#define TOP_BAND     (TOP_SEC + BAND_OFFSET)      // 45
#define BOT_BAND     (BOT_SEC + BAND_OFFSET)      // 104 (ends at 144 = window bottom)

#define CHAR_STEP_MS   300   // firmware uses 1500; faster here to show flicker
#define PIXEL_FRAME_MS 33    // ~30 fps
#define PIXEL_STEP     2     // px per frame
#define PHASE_MS       8000  // length of each demo phase

// Spaces rendered as underscores, as in the firmware (DSEG14 space is narrow)
const char* DEMO_TEXT = "THE_QUICK_BROWN_FOX_-_SPRITE_DEMO_12345";

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);  // one 320×40 band, 16-bit = 25.6 KB
bool sprOK = false;
uint32_t loopCount = 0;

void step(const char* msg) {
  Serial.println(msg);
  Serial.flush();  // make sure it's out before anything that might hang
}

void fillTest(uint16_t color, const char* name) {
  Serial.printf("  fill %s\n", name);
  tft.fillScreen(color);
  delay(1000);
}

void drawLayout() {
  tft.fillScreen(TFT_BLACK);

  // Full physical panel outline (320×170 in rotation 3) — RED
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_RED);

  // Visible window the main firmware draws in — GREEN
  tft.drawRect(0, DISP_PAD_H, DISP_W, DISP_H, TFT_GREEN);

  // Corner markers so orientation/offset problems are obvious
  tft.fillRect(0, 0, 10, 10, TFT_WHITE);                              // top-left
  tft.fillRect(tft.width() - 10, tft.height() - 10, 10, 10, TFT_BLUE); // bottom-right

  tft.setTextFont(4);
  tft.setTextColor(TEXT_COLOR, TFT_BLACK);
  tft.setCursor(10, DISP_PAD_H + 10);
  tft.print("DISPLAY OK");

  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, DISP_PAD_H + 45);
  tft.printf("%dx%d  rot %d  SPI %d MHz", tft.width(), tft.height(),
             tft.getRotation(), SPI_FREQUENCY / 1000000);
  tft.setCursor(10, DISP_PAD_H + 65);
  tft.print("Red=panel edge  Green=visible window");
  tft.setCursor(10, DISP_PAD_H + 85);
  tft.printf("Loop %lu", (unsigned long)loopCount);
}

// ---------------------------------------------------------------------------
// Sprite demo
// ---------------------------------------------------------------------------

bool createDemoSprite() {
  Serial.printf("  Free heap before sprite: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
  spr.setColorDepth(16);
  if (spr.createSprite(DISP_W, BAND_H) == nullptr) {
    Serial.println("  ERROR: createSprite failed (not enough RAM) - sprite demo skipped");
    return false;
  }
  spr.setFreeFont(SEGMENT_FONT);
  spr.setTextColor(TEXT_COLOR);
  spr.setTextWrap(false, false);
  Serial.printf("  Sprite %dx%d created, free heap now: %lu bytes\n",
                DISP_W, BAND_H, (unsigned long)ESP.getFreeHeap());
  return true;
}

void drawDemoLabels(const char* phase) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(LABEL_FONT);
  tft.setTextColor(TEXT_COLOR, TFT_BLACK);
  tft.setCursor(SIDE_MARGIN, TOP_SEC + 2);
  tft.printf("DIRECT - %s", phase);
  tft.drawFastHLine(0, BOT_SEC + 1, DISP_W, TEXT_COLOR);  // divider, as firmware
  tft.setCursor(SIDE_MARGIN, BOT_SEC + 2);
  tft.printf("SPRITE - %s", phase);
}

// Current method in display.ino: clear the band on screen, then print.
// The band is blank between the two steps -> visible flicker.
uint32_t drawDirect(const String& s, int16_t x) {
  uint32_t t0 = micros();
  tft.fillRect(0, TOP_BAND, DISP_W, BAND_H, TFT_BLACK);
  tft.setFreeFont(SEGMENT_FONT);
  tft.setTextColor(TEXT_COLOR);
  tft.setTextWrap(false, false);
  tft.setCursor(x, TOP_BAND + BASELINE);
  tft.print(s);
  return micros() - t0;
}

// Sprite method: clear + print in RAM, then push the finished band once.
// The screen only ever shows complete frames.
uint32_t drawSprite(const String& s, int16_t x) {
  uint32_t t0 = micros();
  spr.fillSprite(TFT_BLACK);
  spr.setCursor(x, BASELINE);
  spr.print(s);
  spr.pushSprite(0, BOT_BAND);
  return micros() - t0;
}

void runSpriteDemo() {
  if (!sprOK) return;
  String text = DEMO_TEXT;

  // --- Phase 1: character-step scroll (firmware behaviour today) ---
  Serial.println("  Phase 1: character-step scroll (watch the TOP band flicker)");
  drawDemoLabels("CHAR STEP");
  uint32_t dSum = 0, sSum = 0, frames = 0;
  int offset = 0;
  uint32_t end = millis() + PHASE_MS;
  while (millis() < end) {
    String visible = text.substring(offset);
    dSum += drawDirect(visible, SIDE_MARGIN);
    sSum += drawSprite(visible, SIDE_MARGIN);
    frames++;
    offset = (offset + 1) % text.length();
    delay(CHAR_STEP_MS);
  }
  Serial.printf("    avg per frame: DIRECT %lu us, SPRITE %lu us (%lu frames)\n",
                (unsigned long)(dSum / frames), (unsigned long)(sSum / frames),
                (unsigned long)frames);

  // --- Phase 2: smooth pixel scroll (marquee) ---
  Serial.println("  Phase 2: smooth pixel scroll (sprite should be steady, direct flickers)");
  drawDemoLabels("SMOOTH SCROLL");
  String gap = "___";
  String marquee = text + gap + text;  // second copy makes the wrap seamless
  tft.setFreeFont(SEGMENT_FONT);
  int16_t cycleW = tft.textWidth(text + gap);
  dSum = sSum = frames = 0;
  int16_t px = 0;
  end = millis() + PHASE_MS;
  while (millis() < end) {
    uint32_t frameStart = millis();
    int16_t x = SIDE_MARGIN - px;
    dSum += drawDirect(marquee, x);
    sSum += drawSprite(marquee, x);
    frames++;
    px += PIXEL_STEP;
    if (px >= cycleW) px = 0;
    uint32_t used = millis() - frameStart;
    if (used < PIXEL_FRAME_MS) delay(PIXEL_FRAME_MS - used);
  }
  Serial.printf("    avg per frame: DIRECT %lu us, SPRITE %lu us (%lu frames)\n",
                (unsigned long)(dSum / frames), (unsigned long)(sSum / frames),
                (unsigned long)frames);
}

void setup() {
  Serial.begin(115200);
  delay(1500);  // time for USB serial to enumerate

  Serial.println("\n\n=================================");
  Serial.println("ST7789 Display Test (ESP32-S3, new PCBA)");
  Serial.println("=================================\n");

  // --- Step 1: compiled-in setup ---
  step("STEP 1: TFT_eSPI setup compiled in:");
  Serial.printf("  USER_SETUP_ID = %d\n", USER_SETUP_ID);
  Serial.printf("  MOSI=%d SCLK=%d CS=%d DC=%d RST=%d BL=%d MISO=%d\n",
                TFT_MOSI, TFT_SCLK, TFT_CS, TFT_DC, TFT_RST, TFT_BL, TFT_MISO);
  if (TFT_MISO == TFT_MOSI) {
    Serial.println("  WARNING: TFT_MISO == TFT_MOSI - core 3.x will stop the SPI bus and tft.init() will hang");
  }
  Serial.printf("  TFT_WIDTH=%d TFT_HEIGHT=%d SPI_FREQUENCY=%d\n",
                TFT_WIDTH, TFT_HEIGHT, SPI_FREQUENCY);
#ifdef USE_FSPI_PORT
  Serial.println("  USE_FSPI_PORT defined (correct for ESP32-S3, core 3.x)");
#else
  Serial.println("  WARNING: USE_FSPI_PORT NOT defined - tft.init() will likely crash");
#endif

  // --- Step 2: backlight only ---
  step("\nSTEP 2: Blinking backlight 3x (GPIO9 / SPI1_BL) - watch the panel glow");
  pinMode(TFT_BL, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(TFT_BL, HIGH);
    Serial.println("  BL HIGH (on)");
    delay(700);
    digitalWrite(TFT_BL, LOW);
    Serial.println("  BL LOW  (off)");
    delay(700);
  }
  digitalWrite(TFT_BL, HIGH);
  step("  BL left ON. No glow at all -> check display power / BL circuit.");

  // --- Step 3: init ---
  // Watchdog: if tft.init() hangs for 5 s, the ESP32 panics and prints a
  // "Backtrace: 0x... 0x..." line showing exactly where it was stuck, then
  // reboots. Paste that Backtrace line (plus the build anchors below).
  step("\nSTEP 3: tft.init() ... (watchdog armed: a hang >5 s prints a Backtrace)");
  Serial.printf("  build anchors: setup=%p loop=%p\n", (void*)&setup, (void*)&loop);
  Serial.flush();
  enableLoopWDT();
  tft.init();
  disableLoopWDT();
  step("  tft.init() returned OK");
  tft.setRotation(3);
  Serial.printf("  rotation 3 -> %d x %d\n", tft.width(), tft.height());
  digitalWrite(TFT_BL, HIGH);  // init also drives BL; make sure it's on

  // --- Step 4: colour fills ---
  step("\nSTEP 4: Full-screen colour fills (1s each)");
  fillTest(TFT_RED,   "RED");
  fillTest(TFT_GREEN, "GREEN");
  fillTest(TFT_BLUE,  "BLUE");
  fillTest(TFT_WHITE, "WHITE");
  fillTest(TFT_BLACK, "BLACK");
  step("  If the backlight is on but colours never appear -> check MOSI/SCLK/CS/DC/RST.");
  step("  If colours are wrong (red<->blue, inverted) -> TFT_RGB_ORDER / TFT_INVERSION.");

  // --- Step 5: layout ---
  step("\nSTEP 5: Layout screen (panel outline + visible window)");
  drawLayout();
  delay(3000);

  // --- Step 6: sprite demo ---
  step("\nSTEP 6: Sprite demo - TOP = direct draw (current firmware), BOTTOM = sprite");
  sprOK = createDemoSprite();
  runSpriteDemo();

  step("\nSetup complete. Looping: colour fills, layout, sprite demo.\n");
}

void loop() {
  loopCount++;
  Serial.printf("Heartbeat %lu - running colour cycle\n", (unsigned long)loopCount);
  fillTest(TFT_RED,   "RED");
  fillTest(TFT_GREEN, "GREEN");
  fillTest(TFT_BLUE,  "BLUE");
  drawLayout();
  delay(3000);
  Serial.println("Sprite demo:");
  runSpriteDemo();
}
