/*
  MCP23017 Keypad Test — new PCBA (primary ESP32-S3-WROOM-1)

  Tests the MCP23017 I2C GPIO expander with the 2x4 keypad matrix and the
  two direct buttons. Prints key presses and raw pin states to the Serial
  Monitor (115200) for troubleshooting.

  Board: ESP32S3 Dev Module

  Keypad Matrix Logic (Row Scanning with Diodes):
    - Columns (GPA6 = COL0, GPA5 = COL1): Inputs with internal PULLUP resistors
    - Rows (GPA3 = ROW0 … GPA0 = ROW3): Outputs, normally HIGH, scan by
      setting one LOW at a time
    - Diodes: Anodes connected to columns (prevents current backflow)
    - When button pressed: Column reads LOW (pulled to ground through active LOW row)

  Direct Buttons (active LOW, wired directly to GND):
    - GPA4: EJECT
    - GPA7: PIN   (datasheet Rev D lists GPA7 as output-only — see note below)

  Button Map:
    Matrix (Row, Col):
      (0,0): unmapped
      (0,1): TrackUp   — GPA3 LOW, read GPA5
      (1,0): Play      — GPA2 LOW, read GPA6
      (1,1): TrackDown — GPA2 LOW, read GPA5
      (2,0): Scan      — GPA1 LOW, read GPA6
      (2,1): FF        — GPA1 LOW, read GPA5
      (3,0): RPT       — GPA0 LOW, read GPA6
      (3,1): REV       — GPA0 LOW, read GPA5

  Hardware (MCP23017 pin : ESP32-S3):
    Pin 13 SDA  : GPIO1  (I2C1SDA)
    Pin 12 SCL  : GPIO2  (I2C1SCL)
    Pin 20 INTA : GPIO48 (I2C1INTA) — not used by this test
    Pin 19 INTB : GPIO21 (I2C1INTB) — not used by this test
    A0, A1, A2 -> GND (address 0x20)

  Relays (GPB0 DAC_RELAY, GPB1 Radio_RELAY) are latched LOW (Radio path) at
  startup so they sit in a known state during the test.
*/

#include <Wire.h>
#include <Adafruit_MCP23X17.h>

// I2C1 pins (ESP32-S3)
#define I2C_SDA 1
#define I2C_SCL 2

// MCP23017 I2C address
#define MCP23017_ADDR 0x20

// Keypad matrix pins on MCP23017 (all on Port A)
#define KEYPAD_ROW_3 0  // GPA0
#define KEYPAD_ROW_2 1  // GPA1
#define KEYPAD_ROW_1 2  // GPA2
#define KEYPAD_ROW_0 3  // GPA3
#define KEYPAD_COL_1 5  // GPA5
#define KEYPAD_COL_0 6  // GPA6

// Direct buttons (input pullup, active LOW — wired directly to GND)
#define BTN_EJECT 4  // GPA4 — EJECT
#define BTN_PIN   7  // GPA7 — PIN

// Relays on Port B (MCP pin numbers 8–15 = GPB0–GPB7)
#define DAC_RELAY_PIN   8  // GPB0
#define RADIO_RELAY_PIN 9  // GPB1

Adafruit_MCP23X17 mcp;
bool mcp_initialized = false;

const byte COL_PINS[] = { KEYPAD_COL_0, KEYPAD_COL_1 };
const byte ROW_PINS[] = { KEYPAD_ROW_0, KEYPAD_ROW_1, KEYPAD_ROW_2, KEYPAD_ROW_3 };
const byte NUM_COLS = 2;
const byte NUM_ROWS = 4;
bool     lastPressed[NUM_ROWS][NUM_COLS]    = { false };
uint32_t pressStartTime[NUM_ROWS][NUM_COLS] = { 0 };

// Direct buttons
const byte  DIRECT_PINS[]  = { BTN_PIN, BTN_EJECT };
const char* DIRECT_NAMES[] = { "PIN (GPA7)", "EJECT (GPA4)" };
const byte  NUM_DIRECT = 2;
bool     lastDirect[NUM_DIRECT]      = { false };
uint32_t directPressTime[NUM_DIRECT] = { 0 };

const uint32_t KEYPAD_DEBOUNCE_MS = 50;

// Forward declarations
void scanI2C();
void handleKeypad();
void handleDirectButtons();
void onKeyPress(byte row, byte col);
void printRawPinStates();
bool debounceButton(bool reading, bool &lastState, uint32_t &pressStart, uint32_t now);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=================================");
  Serial.println("MCP23017 Keypad Test (ESP32-S3, new PCBA)");
  Serial.println("=================================\n");

  // Initialize I2C
  Serial.printf("Initializing I2C on SDA=GPIO%d, SCL=GPIO%d...\n", I2C_SDA, I2C_SCL);
  Wire.begin(I2C_SDA, I2C_SCL);

  // Scan I2C bus
  Serial.println("\nScanning I2C bus...");
  scanI2C();

  // Initialize MCP23017
  Serial.printf("\nInitializing MCP23017 at address 0x%02X...\n", MCP23017_ADDR);

  if (!mcp.begin_I2C(MCP23017_ADDR)) {
    Serial.println("ERROR: MCP23017 not found!");
    Serial.println("\nTroubleshooting:");
    Serial.println("1. Check power: VDD to 3.3V, VSS to GND, RESET tied to VDD");
    Serial.println("2. Check I2C wiring:");
    Serial.println("   - MCP23017 pin 13 (SDA) -> ESP32-S3 GPIO1");
    Serial.println("   - MCP23017 pin 12 (SCL) -> ESP32-S3 GPIO2");
    Serial.println("3. Check address pins: A0, A1, A2 should be GND for 0x20");
    Serial.println("4. Check 4.7k pull-ups on SDA and SCL");
    Serial.println("\nHalting...");
    while (1) {
      delay(1000);
    }
  }

  Serial.println("SUCCESS: MCP23017 found!");
  mcp_initialized = true;

  // Relays — latch LOW (Radio path) before enabling as outputs
  mcp.digitalWrite(RADIO_RELAY_PIN, LOW);
  mcp.digitalWrite(DAC_RELAY_PIN,   LOW);
  mcp.pinMode(RADIO_RELAY_PIN, OUTPUT);
  mcp.pinMode(DAC_RELAY_PIN,   OUTPUT);
  Serial.println("\nRelays: GPB1 Radio_RELAY, GPB0 DAC_RELAY -> OUTPUT (LOW, Radio path)");

  Serial.println("\nConfiguring keypad matrix...");

  // Columns as inputs with pullups
  for (byte c = 0; c < NUM_COLS; c++) {
    mcp.pinMode(COL_PINS[c], INPUT_PULLUP);
    Serial.printf("  Column %d (GPA%d) -> INPUT_PULLUP\n", c, COL_PINS[c]);
  }

  // Direct buttons as inputs with pullups
  for (byte d = 0; d < NUM_DIRECT; d++) {
    mcp.pinMode(DIRECT_PINS[d], INPUT_PULLUP);
    Serial.printf("  %-12s -> INPUT_PULLUP\n", DIRECT_NAMES[d]);
  }

  // Rows as outputs, latched HIGH (inactive) before enabling
  for (byte r = 0; r < NUM_ROWS; r++) {
    mcp.digitalWrite(ROW_PINS[r], HIGH);
    mcp.pinMode(ROW_PINS[r], OUTPUT);
    Serial.printf("  Row %d (GPA%d) -> OUTPUT (HIGH)\n", r, ROW_PINS[r]);
  }

  Serial.println("\n=================================");
  Serial.println("Ready! Press keys on the keypad.");
  Serial.println("Key positions (row, col):");
  Serial.println("  (0,0) (0,1)");
  Serial.println("  (1,0) (1,1)");
  Serial.println("  (2,0) (2,1)");
  Serial.println("  (3,0) (3,1)");
  Serial.println("=================================\n");
}

void loop() {
  handleKeypad();
  handleDirectButtons();

  // Print raw pin states every 2 seconds for debugging
  static uint32_t lastDebugPrint = 0;
  if (millis() - lastDebugPrint > 2000) {
    lastDebugPrint = millis();
    printRawPinStates();
  }

  delay(10);
}

void scanI2C() {
  int nDevices = 0;

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found device at 0x%02X\n", address);
      nDevices++;
    }
  }

  if (nDevices == 0) {
    Serial.println("  No I2C devices found!");
  } else {
    Serial.printf("  Found %d device(s)\n", nDevices);
  }
}

// Returns true exactly once per press, after the reading has been stable
// for KEYPAD_DEBOUNCE_MS. Same logic as primary_esp32s3/keypad.ino.
bool debounceButton(bool reading, bool &lastState, uint32_t &pressStart, uint32_t now) {
  if (reading && !lastState) {
    pressStart = now;
    lastState  = true;
  } else if (!reading && lastState) {
    lastState  = false;
    pressStart = 0;
  } else if (reading && lastState && pressStart != 0) {
    if ((now - pressStart) >= KEYPAD_DEBOUNCE_MS) {
      pressStart = 0;
      return true;
    }
  }
  return false;
}

void handleKeypad() {
  if (!mcp_initialized) return;

  uint32_t now = millis();

  for (byte r = 0; r < NUM_ROWS; r++) {
    mcp.digitalWrite(ROW_PINS[r], LOW);   // Activate row
    delayMicroseconds(100);

    for (byte c = 0; c < NUM_COLS; c++) {
      bool reading = (mcp.digitalRead(COL_PINS[c]) == LOW);  // LOW = pressed
      if (debounceButton(reading, lastPressed[r][c], pressStartTime[r][c], now)) {
        onKeyPress(r, c);
      }
    }

    mcp.digitalWrite(ROW_PINS[r], HIGH);  // Deactivate row
  }
}

void onKeyPress(byte row, byte col) {
  Serial.printf("KEY PRESSED: Row %d (GPA%d), Col %d (GPA%d)\n",
                row, ROW_PINS[row], col, COL_PINS[col]);

  if      (row == 0 && col == 1) Serial.println("  -> TrackUp");
  else if (row == 1 && col == 0) Serial.println("  -> Play");
  else if (row == 1 && col == 1) Serial.println("  -> TrackDown");
  else if (row == 2 && col == 0) Serial.println("  -> Scan");
  else if (row == 2 && col == 1) Serial.println("  -> FF");
  else if (row == 3 && col == 0) Serial.println("  -> RPT");
  else if (row == 3 && col == 1) Serial.println("  -> REV");
  else                           Serial.println("  -> Not mapped");
  Serial.println();
}

void handleDirectButtons() {
  if (!mcp_initialized) return;

  uint32_t now = millis();

  for (byte d = 0; d < NUM_DIRECT; d++) {
    bool reading = (mcp.digitalRead(DIRECT_PINS[d]) == LOW);  // LOW = pressed
    if (debounceButton(reading, lastDirect[d], directPressTime[d], now)) {
      Serial.printf("DIRECT BUTTON: %s\n\n", DIRECT_NAMES[d]);
    }
  }
}

void printRawPinStates() {
  Serial.println("\n--- Raw Pin States ---");

  for (byte r = 0; r < NUM_ROWS; r++) {
    mcp.digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(100);

    Serial.printf("Row %d (GPA%d) LOW -> Columns: ", r, ROW_PINS[r]);
    for (byte c = 0; c < NUM_COLS; c++) {
      bool state = mcp.digitalRead(COL_PINS[c]);
      Serial.printf("COL%d/GPA%d=%s", c, COL_PINS[c], state ? "HIGH" : "LOW");
      if (c < NUM_COLS - 1) Serial.print(", ");
    }
    Serial.println();

    mcp.digitalWrite(ROW_PINS[r], HIGH);
  }

  Serial.print("Direct: ");
  for (byte d = 0; d < NUM_DIRECT; d++) {
    bool state = mcp.digitalRead(DIRECT_PINS[d]);
    Serial.printf("%s=%s", DIRECT_NAMES[d], state ? "HIGH" : "LOW");
    if (d < NUM_DIRECT - 1) Serial.print(", ");
  }
  Serial.println();

  Serial.println("(Press a key - its column should go LOW when its row is scanned)");
  Serial.println("----------------------\n");
}
