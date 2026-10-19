/*
  keypad_test.ino
  ---------------
  Button matrix test: 2 columns (GPIO 13, 16), 4 rows (GPIO 25, 26, 27, 33).
  Open Serial Monitor at 115200 baud.
*/

const byte COL_PINS[] = { 13, 16 };
const byte ROW_PINS[] = { 25, 26, 27, 33 };
const byte NUM_COLS = 2;
const byte NUM_ROWS = 4;
bool lastPressed[NUM_ROWS][NUM_COLS] = { false };

char keys[NUM_ROWS][NUM_COLS] = {
  { '1', '5' },
  { '2', '6' },
  { '3', '7' },
  { '4', '8' }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Button test starting...");
  
  for (byte c = 0; c < NUM_COLS; c++) {
    pinMode(COL_PINS[c], OUTPUT);
    digitalWrite(COL_PINS[c], LOW);
  }
  for (byte r = 0; r < NUM_ROWS; r++) {
    pinMode(ROW_PINS[r], INPUT_PULLDOWN);
  }
}

void loop() {
  for (byte c = 0; c < NUM_COLS; c++) {
    digitalWrite(COL_PINS[c], HIGH);
    delayMicroseconds(100);
    
    for (byte r = 0; r < NUM_ROWS; r++) {
      bool pressed = (digitalRead(ROW_PINS[r]) == HIGH);
      
      if (pressed && !lastPressed[r][c]) {
        Serial.print("PRESSED: ");
        Serial.println(keys[r][c]);
      } else if (!pressed && lastPressed[r][c]) {
        Serial.print("RELEASED: ");
        Serial.println(keys[r][c]);
      }
      
      lastPressed[r][c] = pressed;
    }
    
    digitalWrite(COL_PINS[c], LOW);
  }
  
  delay(20);
}
