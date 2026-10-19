// ---------------------------------------------------------------------------
// function_test — shared pins, layout and declarations
//
// Kept in a header so every tab sees the same types before Arduino's
// auto-generated prototypes.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_MCP23X17.h>
#include <TFT_eSPI.h>
#include "AudioTools.h"

// Same guard as primary_esp32s3/config.h
#if !defined(USER_SETUP_ID) || (USER_SETUP_ID != 900)
  #error "Wrong TFT_eSPI setup. Select Setup_MiataS3_ST7789_170x320.h in TFT_eSPI/User_Setup_Select.h"
#endif

// ---------------------------------------------------------------------------
// ESP32-S3 pins (match primary_esp32s3/config.h)
// ---------------------------------------------------------------------------

#define I2C_SDA       1
#define I2C_SCL       2
#define MCP23017_ADDR 0x20

#define LED_PIN       4      // LED1_PWM
#define DAC_MUTE      5      // PCM5102A XSMT, HIGH = unmute
#define I2S_SWITCH    8      // TMUX1574 select, LOW = I2S1 (this ESP32-S3)

#define I2S_BCK       6      // I2S1
#define I2S_WS        15
#define I2S_DATA_OUT  7

#define SD_CS         42
#define SD_MISO       41
#define SD_MOSI       40
#define SD_SCLK       39

// ---------------------------------------------------------------------------
// MCP23017 pins (GPA0–7 = 0–7, GPB0–7 = 8–15)
// ---------------------------------------------------------------------------

#define KEYPAD_ROW_3 0   // GPA0
#define KEYPAD_ROW_2 1   // GPA1
#define KEYPAD_ROW_1 2   // GPA2
#define KEYPAD_ROW_0 3   // GPA3
#define BTN_EJECT    4   // GPA4
#define KEYPAD_COL_1 5   // GPA5
#define KEYPAD_COL_0 6   // GPA6
#define BTN_PIN      7   // GPA7

#define PIN_DAC_RELAY      8   // GPB0
#define PIN_RADIO_RELAY    9   // GPB1
#define PIN_AMP_MUTE      10   // GPB2
#define PIN_MODULE_ACTIVE 11   // GPB3
#define PIN_ILLUM_SENSE   12   // GPB4
#define PIN_RADIO_SENSE   13   // GPB5
#define PIN_HU_LIGHTSW    14   // GPB6

#define NUM_ROWS 4
#define NUM_COLS 2

// ---------------------------------------------------------------------------
// Display layout — visible window from primary_esp32s3/config.h
// Same format as headunit_io_test: Font 2 (16 px), white on black,
// HIGH/good = green, LOW/bad = red, hints grey.
// ---------------------------------------------------------------------------

#define DISP_W      320
#define DISP_H      118
#define DISP_PAD_H  26
#define SIDE_MARGIN 5
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

// Per-button debounce state (keypad.ino)
struct Debounce {
  bool     raw   = false;   // last raw reading
  uint32_t since = 0;       // time raw last changed
  bool     held  = false;   // debounced state
};

// One PWM output in the LED test (mode_led.ino)
struct PwmChannel {
  const char* name;       // label on screen
  uint8_t     pin;
  int         startPct;   // level on entering the test
  int         pct;        // current setting
  bool        on;
};

// Matrix button names, [row][col]
extern const char* const KEY_NAMES[NUM_ROWS][NUM_COLS];

// Shared objects (function_test.ino)
extern TFT_eSPI tft;
extern Adafruit_MCP23X17 mcp;
extern I2SStream i2s;
extern SPIClass sdSPI;

// Debounced held state (keypad.ino)
extern bool matrixHeld[NUM_ROWS][NUM_COLS];
extern bool pinHeld;
extern bool ejectHeld;

// Display helpers (display.ino)
int16_t rowY(int row, int dividerAfter);
void    drawDivider(int dividerAfter);
void    drawLabel(const char* text, int16_t x, int16_t y);
void    drawValue(const String& text, int16_t x, int16_t y, uint16_t color, int16_t padW);
void    drawLevel(bool high, int16_t x, int16_t y);
void    drawHint(const String& text, int16_t y, int16_t padW = 0);
int16_t labelColumn(const char* const* labels, int count, int16_t x = SIDE_MARGIN);

// Shared MCP23017 output state
void headunitOutputsLow();
void setRelays(bool radioRelay, bool dacRelay);

// Mode functions
void keypadEnter();   void keypadExit();   void keypadLoop();   void keypadKey(byte r, byte c);   void keypadPin();
void ledEnter();      void ledExit();      void ledLoop();      void ledKey(byte r, byte c);      void ledPin();
void headunitEnter(); void headunitExit(); void headunitLoop(); void headunitKey(byte r, byte c); void headunitPin();
void sdEnter();       void sdExit();       void sdLoop();       void sdKey(byte r, byte c);       void sdPin();
void toneEnter();     void toneExit();     void toneLoop();     void toneKey(byte r, byte c);     void tonePin();
