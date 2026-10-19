// ---------------------------------------------------------------------------
// function_test.h — internal header for the built-in function test
//
// Pins come from board.h and keypad state from keypad.h, shared with the
// main firmware. Hardware objects (display, MCP23017, I2S, SD SPI) are the
// main firmware's — see the externs below. Everything here is in namespace
// ftest so names can't collide with the main sketch.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_MCP23X17.h>
#include <TFT_eSPI.h>
#include "AudioTools.h"

#include "../../board.h"
#include "../../keypad.h"
#include "../../mp3_files.h"
#include "function_test_api.h"

// ---------------------------------------------------------------------------
// Main-firmware objects used by the tests
// ---------------------------------------------------------------------------

extern TFT_eSPI          tft;              // primary_esp32s3.ino
extern Adafruit_MCP23X17 mcp;              // keypad.ino
extern bool              mcp_initialized;  // keypad.ino
extern I2SStream         mp3_i2s;          // mp3player.ino — I2S1
extern SPIClass          sdSPI;            // mp3player.ino — HSPI for the SD card
void setRelays(uint8_t radioRelay, uint8_t dacRelay);   // actions.ino
bool i2s1Begin();                                       // mp3player.ino — I2S1 at 44.1 kHz

namespace ftest {

// ---------------------------------------------------------------------------
// Display layout — Font 2 (16 px), white on black, HIGH/good = green,
// LOW/bad = red, hints grey. Drawn directly on the panel inside the visible
// window (board.h). Same format as the original headunit_io_test.
// ---------------------------------------------------------------------------

#define FONT        2
#define ROW_H       16
#define MAX_ROWS    7      // 7 × 16 px + divider = 115 px of the 118 px window

#define COLOR_TEXT  TFT_WHITE
#define COLOR_HIGH  TFT_GREEN
#define COLOR_LOW   TFT_RED
#define COLOR_HINT  TFT_DARKGREY
#define COLOR_BG    TFT_BLACK

// ---------------------------------------------------------------------------
// Test modes — cycled with EJECT
// ---------------------------------------------------------------------------

struct TestMode {
  const char* name;
  void (*enter)();
  void (*exit)();
  void (*loop)();
  void (*onKey)(byte row, byte col);   // matrix button pressed
  void (*onPin)();                     // PIN button pressed
};

// One PWM output in the LED test (mode_led.cpp)
struct PwmChannel {
  const char* name;       // label on screen
  uint8_t     pin;
  int         startPct;   // level on entering the test
  int         pct;        // current setting
  bool        on;
};

// Display helpers (display.cpp)
int16_t rowY(int row, int dividerAfter);
void    drawDivider(int dividerAfter);
void    drawLabel(const char* text, int16_t x, int16_t y);
void    drawValue(const String& text, int16_t x, int16_t y, uint16_t color, int16_t padW);
void    drawLevel(bool high, int16_t x, int16_t y);
void    drawHint(const String& text, int16_t y, int16_t padW = 0);
int16_t labelColumn(const char* const* labels, int count, int16_t x = SIDE_MARGIN);

// Shared MCP23017 output state (function_test.cpp)
void headunitOutputsLow();

// Mode functions
void keypadEnter();   void keypadExit();   void keypadLoop();   void keypadKey(byte r, byte c);   void keypadPin();
void ledEnter();      void ledExit();      void ledLoop();      void ledKey(byte r, byte c);      void ledPin();
void headunitEnter(); void headunitExit(); void headunitLoop(); void headunitKey(byte r, byte c); void headunitPin();
void sdEnter();       void sdExit();       void sdLoop();       void sdKey(byte r, byte c);       void sdPin();
void toneEnter();     void toneExit();     void toneLoop();     void toneKey(byte r, byte c);     void tonePin();

}  // namespace ftest
