// ---------------------------------------------------------------------------
// Keypad scan — shared by every test
//
// Same matrix and debounce as primary_esp32s3/keypad.ino, but reads/writes
// Port A as a whole byte: 2 I2C transfers per row instead of 6, so a full
// scan is short enough not to starve the tone test's audio buffer.
//
// Rows GPA3 (ROW0) … GPA0 (ROW3): outputs, idle HIGH, driven LOW one at a time
// Cols GPA6 (COL0), GPA5 (COL1):  inputs, pulled up, LOW = pressed
// Direct GPA4 EJECT, GPA7 PIN:    inputs, pulled up, LOW = pressed
//
// Debounced "held" states are exported for the keypad test screen. Presses
// (once per press, after 50 ms stable) go to onMatrixKey / onEject /
// onPinButton in function_test.ino.
// ---------------------------------------------------------------------------

const byte ROW_PINS[NUM_ROWS] = { KEYPAD_ROW_0, KEYPAD_ROW_1, KEYPAD_ROW_2, KEYPAD_ROW_3 };
const byte COL_PINS[NUM_COLS] = { KEYPAD_COL_0, KEYPAD_COL_1 };
const uint8_t ROWS_IDLE = 0xFF;   // all rows HIGH (non-row bits are inputs, ignored)
const uint32_t KEYPAD_DEBOUNCE_MS = 50;

bool matrixHeld[NUM_ROWS][NUM_COLS] = { { false } };
bool pinHeld   = false;
bool ejectHeld = false;

Debounce matrixDb[NUM_ROWS][NUM_COLS];
Debounce pinDb, ejectDb;

void onEject();
void onMatrixKey(byte row, byte col);
void onPinButton();

// Returns true once when the debounced state becomes "pressed"
bool debounce(Debounce& d, bool reading, uint32_t now) {
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

bool initKeypad() {
  for (byte c = 0; c < NUM_COLS; c++) mcp.pinMode(COL_PINS[c], INPUT_PULLUP);
  mcp.pinMode(BTN_EJECT, INPUT_PULLUP);
  mcp.pinMode(BTN_PIN,   INPUT_PULLUP);
  for (byte r = 0; r < NUM_ROWS; r++) {
    mcp.digitalWrite(ROW_PINS[r], HIGH);   // latch HIGH before enabling output
    mcp.pinMode(ROW_PINS[r], OUTPUT);
  }
  return true;
}

void scanKeypad() {
  uint32_t now = millis();
  uint8_t portA = 0xFF;

  for (byte r = 0; r < NUM_ROWS; r++) {
    mcp.writeGPIOA(ROWS_IDLE & ~(1 << ROW_PINS[r]));   // this row LOW
    delayMicroseconds(100);
    portA = mcp.readGPIOA();
    for (byte c = 0; c < NUM_COLS; c++) {
      bool pressed = !(portA & (1 << COL_PINS[c]));
      bool fired = debounce(matrixDb[r][c], pressed, now);
      matrixHeld[r][c] = matrixDb[r][c].held;
      if (fired) onMatrixKey(r, c);
    }
  }
  mcp.writeGPIOA(ROWS_IDLE);

  // Direct buttons don't depend on the rows — use the last Port A read
  bool pinFired   = debounce(pinDb,   !(portA & (1 << BTN_PIN)),   now);
  bool ejectFired = debounce(ejectDb, !(portA & (1 << BTN_EJECT)), now);
  pinHeld   = pinDb.held;
  ejectHeld = ejectDb.held;

  if (pinFired) onPinButton();
  if (ejectFired) onEject();
}
