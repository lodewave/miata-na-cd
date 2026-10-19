// ---------------------------------------------------------------------------
// TEST 3 — HEADUNIT I/O (same function as headunit_io_test)
//
//   ILLUM SENSE:     HIGH              GPB4 IN
//   RADIO SENSE:     HIGH              GPB5 IN
//   HU LIGHTSWITCH:  HIGH              GPB6 IN
//   ─────────────────────────────────────────────
//   AMP MUTE:        LOW             [TrackUp]
//   MODULE ACTIVE:   LOW           [TrackDown]
//   DAC RELAY:       LOW                  [FF]
//   RADIO RELAY:     LOW                 [REV]
//
// Inputs are INPUT_PULLUP. Outputs start LOW each time the test is entered
// and go back LOW when it's left. Levels shown are read back from the pins,
// so an output that won't follow its command shows (and Serial warns).
// ---------------------------------------------------------------------------

#define HU_DIVIDER 3

struct HuSignal {
  const char* name;
  uint8_t     pin;
  bool        isOutput;
  int8_t      row, col;    // toggle button (-1 = input)
  const char* hint;
  bool        commanded;
  int8_t      shown;       // -1 = not drawn yet
};

static HuSignal huSignals[] = {
  { "ILLUM SENSE",    PIN_ILLUM_SENSE,   false, -1, -1, "GPB4 IN",     false, -1 },
  { "RADIO SENSE",    PIN_RADIO_SENSE,   false, -1, -1, "GPB5 IN",     false, -1 },
  { "HU LIGHTSWITCH", PIN_HU_LIGHTSW,    false, -1, -1, "GPB6 IN",     false, -1 },
  { "AMP MUTE",       PIN_AMP_MUTE,      true,   0,  1, "[TrackUp]",   false, -1 },
  { "MODULE ACTIVE",  PIN_MODULE_ACTIVE, true,   1,  1, "[TrackDown]", false, -1 },
  { "DAC RELAY",      PIN_DAC_RELAY,     true,   2,  1, "[FF]",        false, -1 },
  { "RADIO RELAY",    PIN_RADIO_RELAY,   true,   3,  1, "[REV]",       false, -1 },
};
static const int HU_NUM = sizeof(huSignals) / sizeof(huSignals[0]);
static int16_t huValueX;

void headunitEnter() {
  headunitOutputsLow();

  const char* names[HU_NUM];
  for (int i = 0; i < HU_NUM; i++) names[i] = huSignals[i].name;
  huValueX = labelColumn(names, HU_NUM);

  for (int i = 0; i < HU_NUM; i++) {
    HuSignal& s = huSignals[i];
    int16_t y = rowY(i, HU_DIVIDER);
    s.commanded = false;
    s.shown = -1;
    drawLabel((String(s.name) + ":").c_str(), SIDE_MARGIN, y);
    drawHint(s.hint, y);
  }
  drawDivider(HU_DIVIDER);

  headunitLoop();   // draw initial levels
  Serial.println("Initial levels:");
  for (int i = 0; i < HU_NUM; i++) {
    Serial.printf("  %-15s %s\n", huSignals[i].name, huSignals[i].shown ? "HIGH" : "LOW");
  }
  Serial.println("Toggle: TrackUp=AMP MUTE, TrackDown=MODULE ACTIVE, FF=DAC RELAY, REV=RADIO RELAY");
}

void headunitExit() {
  headunitOutputsLow();
}

void headunitLoop() {
  uint8_t portB = mcp.readGPIOB();   // one I2C read for all seven signals
  for (int i = 0; i < HU_NUM; i++) {
    HuSignal& s = huSignals[i];
    bool level = portB & (1 << (s.pin - 8));
    if (level == s.shown) continue;
    if (s.shown != -1) {
      Serial.printf("%-15s %s -> %s\n", s.name, s.shown ? "HIGH" : "LOW", level ? "HIGH" : "LOW");
    }
    if (s.isOutput && level != s.commanded) {
      Serial.printf("  WARNING: %s commanded %s but pin reads %s\n",
                    s.name, s.commanded ? "HIGH" : "LOW", level ? "HIGH" : "LOW");
    }
    drawLevel(level, huValueX, rowY(i, HU_DIVIDER));
    s.shown = level;
  }
}

void headunitKey(byte r, byte c) {
  for (int i = 0; i < HU_NUM; i++) {
    HuSignal& s = huSignals[i];
    if (!s.isOutput || s.row != r || s.col != c) continue;
    s.commanded = !s.commanded;
    mcp.digitalWrite(s.pin, s.commanded ? HIGH : LOW);
    Serial.printf("  %s: set %s\n", s.name, s.commanded ? "HIGH" : "LOW");
    return;
  }
}

void headunitPin() {}
