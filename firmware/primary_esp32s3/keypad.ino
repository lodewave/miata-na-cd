// ---------------------------------------------------------------------------
// Keypad Hardware — MCP23017 I2C GPIO Expander
//
// MCP23017 initialisation, matrix scanning, direct buttons and debouncing.
// Shared by the main firmware and the function test (state in keypad.h).
// Events go to actions.ino, which routes them by UI mode:
//   onKeyPress(row, col)  — matrix button pressed
//   onDirectButton(pin)   — PIN or EJECT pressed
//   onPinLongPress()      — PIN held PIN_LONG_PRESS_MS (1 s)
// This file has no knowledge of what the buttons do.
//
// Also configures the MCP23017 Port B pins: relay outputs (GPB0/GPB1),
// AMP_MUTE / MODULE_ACTIVE outputs (GPB2/GPB3, LOW at boot) and headunit
// sense inputs (GPB4 ILLUM SENSE, GPB5 RADIO SENSE, GPB6 HU LIGHTSWITCH, all
// pulled up). See board.h.
//
// The MCP23017 INTA/INTB lines (GPIO48/GPIO21) are wired but not used yet;
// the keypad is polled every KEYPAD_SCAN_MS (10 ms) using whole-port Port A
// transfers at 400 kHz I2C — 9 transfers, ~1.2 ms per scan.
//
// Keypad Matrix Logic (Row Scanning with Diodes):
//   - Columns (GPA6 = COL0, GPA5 = COL1): Inputs with internal PULLUP resistors
//   - Rows (GPA3 = ROW0 … GPA0 = ROW3): Outputs, normally HIGH, scan by
//     setting one LOW at a time
//   - Diodes: Anodes connected to columns (prevents current backflow)
//   - When button pressed: Column reads LOW (pulled to ground through active LOW row)
//
// Button Map:
//   Matrix (Row, Col):
//     (0,0): unused    (no button)
//     (0,1): TrackUp   — GPA3 LOW, read GPA5
//     (1,0): Play      — GPA2 LOW, read GPA6
//     (1,1): TrackDown — GPA2 LOW, read GPA5
//     (2,0): Scan      — GPA1 LOW, read GPA6
//     (2,1): FF        — GPA1 LOW, read GPA5
//     (3,0): RPT       — GPA0 LOW, read GPA6
//     (3,1): REV       — GPA0 LOW, read GPA5
//
//   Direct (active LOW, wired to GND):
//     GPA4: EJECT  — cycles input (Radio → BT → MP3 → AUX)
//     GPA7: PIN    — hold 1 s for the settings menu
// ---------------------------------------------------------------------------

#include <Wire.h>

Adafruit_MCP23X17 mcp;
bool mcp_initialized = false;

// Shared state (declared in keypad.h)
const uint8_t ROW_PINS[NUM_ROWS] = { KEYPAD_ROW_0, KEYPAD_ROW_1, KEYPAD_ROW_2, KEYPAD_ROW_3 };
const uint8_t COL_PINS[NUM_COLS] = { KEYPAD_COL_0, KEYPAD_COL_1 };
const char* const KEY_NAMES[NUM_ROWS][NUM_COLS] = {
  { "unused", "TrackUp"   },   // (0,0) has no button on the keypad
  { "Play",   "TrackDown" },
  { "Scan",   "FF"        },
  { "RPT",    "REV"       },
};
bool matrixHeld[NUM_ROWS][NUM_COLS] = { { false } };
bool pinHeld   = false;
bool ejectHeld = false;

// ---------------------------------------------------------------------------
// Debounce
//
// A reading must be stable for KEYPAD_DEBOUNCE_MS before the held state
// changes. Returns true exactly once per press, when it becomes "held".
// ---------------------------------------------------------------------------

// struct KeyDebounce is in keypad.h (Arduino's generated prototypes need it
// before this file)
static KeyDebounce matrixDb[NUM_ROWS][NUM_COLS];
static KeyDebounce pinDb, ejectDb;
static uint32_t pinHeldSince = 0;
static bool     pinLongFired = false;

static bool debounceKey(KeyDebounce& d, bool reading, uint32_t now) {
  if (reading != d.raw) {
    d.raw   = reading;
    d.since = now;
  }
  if (d.raw != d.held && (now - d.since) >= KEYPAD_DEBOUNCE_MS) {
    d.held = d.raw;
    return d.held;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void initKeypad() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);

  if (!mcp.begin_I2C(MCP23017_ADDR)) {
    Serial.println("Warning: MCP23017 not found! Keypad will not work.");
    mcp_initialized = false;
    return;
  }

  Serial.println("MCP23017 initialized");
  mcp_initialized = true;

  // Columns and direct buttons — inputs with pullups
  for (byte c = 0; c < NUM_COLS; c++) mcp.pinMode(COL_PINS[c], INPUT_PULLUP);
  mcp.pinMode(BTN_PIN,   INPUT_PULLUP);
  mcp.pinMode(BTN_EJECT, INPUT_PULLUP);

  // Rows — outputs, latched HIGH (inactive) before enabling so no row glitches
  for (byte r = 0; r < NUM_ROWS; r++) {
    mcp.digitalWrite(ROW_PINS[r], HIGH);
    mcp.pinMode(ROW_PINS[r], OUTPUT);
  }

  // Relays — outputs, latched LOW (Radio path); set by switchInput() in setup()
  mcp.digitalWrite(RADIO_RELAY_PIN, LOW);
  mcp.digitalWrite(DAC_RELAY_PIN,   LOW);
  mcp.pinMode(RADIO_RELAY_PIN, OUTPUT);
  mcp.pinMode(DAC_RELAY_PIN,   OUTPUT);

  // Headunit control outputs, latched LOW: AMP_MUTE off, MODULE_ACTIVE LOW
  // (Radio). Driven by actions.ino switchInput().
  mcp.digitalWrite(PIN_AMP_MUTE,      LOW);
  mcp.digitalWrite(PIN_MODULE_ACTIVE, LOW);
  mcp.pinMode(PIN_AMP_MUTE,      OUTPUT);
  mcp.pinMode(PIN_MODULE_ACTIVE, OUTPUT);

  // Headunit sense inputs (lighting.ino, radioSenseLoop() / the function test)
  mcp.pinMode(PIN_ILLUM_SENSE, INPUT_PULLUP);
  mcp.pinMode(PIN_RADIO_SENSE, INPUT_PULLUP);
  mcp.pinMode(PIN_HU_LIGHTSW,  INPUT_PULLUP);
}

// ---------------------------------------------------------------------------
// Scanning — matrix + direct buttons, every KEYPAD_SCAN_MS
//
// Per row: one writeGPIOA() (that row LOW, others HIGH) and one readGPIOA()
// (both columns + PIN + EJECT). writeGPIOA() only drives the row outputs;
// the MCP23017 ignores bits for input pins. Port B is untouched.
// ---------------------------------------------------------------------------

static const uint8_t ROWS_IDLE = 0xFF;   // all rows HIGH (inactive)

void handleKeypad() {
  if (!mcp_initialized) return;

  static uint32_t lastScanMs = 0;
  uint32_t now = millis();
  if (now - lastScanMs < KEYPAD_SCAN_MS) return;
  lastScanMs = now;

  uint8_t portA = 0xFF;
  for (byte r = 0; r < NUM_ROWS; r++) {
    mcp.writeGPIOA(ROWS_IDLE & ~(1 << ROW_PINS[r]));   // activate this row
    delayMicroseconds(100);                            // let the column settle
    portA = mcp.readGPIOA();

    for (byte c = 0; c < NUM_COLS; c++) {
      bool pressed = !(portA & (1 << COL_PINS[c]));    // LOW = pressed
      bool fired = debounceKey(matrixDb[r][c], pressed, now);
      matrixHeld[r][c] = matrixDb[r][c].held;
      if (fired) onKeyPress(r, c);                     // actions.ino
    }
  }
  mcp.writeGPIOA(ROWS_IDLE);                           // all rows inactive

  // Direct buttons don't depend on the rows — use the last Port A read
  bool pinFired   = debounceKey(pinDb,   !(portA & (1 << BTN_PIN)),   now);
  bool ejectFired = debounceKey(ejectDb, !(portA & (1 << BTN_EJECT)), now);
  pinHeld   = pinDb.held;
  ejectHeld = ejectDb.held;

  if (pinFired) {
    pinHeldSince = now;
    pinLongFired = false;
    onDirectButton(BTN_PIN);                           // actions.ino
  }
  if (pinHeld && !pinLongFired && (now - pinHeldSince) >= PIN_LONG_PRESS_MS) {
    pinLongFired = true;
    onPinLongPress();                                  // actions.ino
  }
  if (ejectFired) onDirectButton(BTN_EJECT);           // actions.ino
}
