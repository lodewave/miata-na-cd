// ---------------------------------------------------------------------------
// keypad.h — keypad state shared by the main firmware and the function test
//
// The driver is keypad.ino. It scans the MCP23017 every KEYPAD_SCAN_MS and
// calls (in actions.ino):
//   onKeyPress(row, col)  — matrix button pressed (once per press)
//   onDirectButton(pin)   — PIN or EJECT pressed (once per press)
//   onPinLongPress()      — PIN held for PIN_LONG_PRESS_MS (once per press)
// ---------------------------------------------------------------------------
#pragma once
#include <stdint.h>

constexpr uint8_t NUM_ROWS = 4;
constexpr uint8_t NUM_COLS = 2;

#define KEYPAD_SCAN_MS     10     // scan interval
#define KEYPAD_DEBOUNCE_MS 50     // press/release must be stable this long
#define PIN_LONG_PRESS_MS  1000   // PIN hold time to open the settings menu

// MCP23017 pin of each row / column (row 0 = GPA3 … row 3 = GPA0)
extern const uint8_t ROW_PINS[NUM_ROWS];
extern const uint8_t COL_PINS[NUM_COLS];

// Debounced "held" state of every button, updated each scan
extern bool matrixHeld[NUM_ROWS][NUM_COLS];
extern bool pinHeld;
extern bool ejectHeld;

// Per-button debounce state (used inside keypad.ino)
struct KeyDebounce {
  bool     raw   = false;   // last raw reading
  uint32_t since = 0;       // when raw last changed
  bool     held  = false;   // debounced state
};

// Button names by matrix position — (0,0) has no button
extern const char* const KEY_NAMES[NUM_ROWS][NUM_COLS];

// Matrix buttons by id = row * NUM_COLS + col (same order as KEY_NAMES)
enum KeyId : uint8_t {
  KEY_UNUSED,      // (0,0) no button
  KEY_TRACK_UP,    // (0,1)
  KEY_PLAY,        // (1,0)
  KEY_TRACK_DOWN,  // (1,1)
  KEY_SCAN,        // (2,0)
  KEY_FF,          // (2,1)
  KEY_RPT,         // (3,0)
  KEY_REV,         // (3,1)
};
inline KeyId keyId(uint8_t row, uint8_t col) { return (KeyId)(row * NUM_COLS + col); }
