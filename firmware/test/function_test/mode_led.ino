// ---------------------------------------------------------------------------
// TEST 2 — LED PWM: button backlight (GPIO4) and display backlight (GPIO9)
//
//   > BUTTON LED:  50%   ON                 GPIO4 2048/4095
//     DISPLAY BL:  100%  ON                 GPIO9 4095/4095
//   ADJUSTING:     BUTTON LED                   RPT = switch
//   ─────────────────────────────────────────────────────────
//   TrackUp:   +1%              TrackDown: -1%
//   FF:        +10%             REV:       -10%
//   Play:      ON / OFF         RPT:       switch LED
//                     EJECT restores display backlight to 100%
//
// RPT switches which backlight the other buttons adjust; it starts on the
// BUTTON LED each time the test is entered. The selected row has a green ">".
//
// Start levels each time the test is entered:
//   BUTTON LED  50% ON   (off outside this test, as before)
//   DISPLAY BL 100% ON   (the normal level — always 100% outside this test)
//
// Duty clamped 0–100%; adjustable while OFF, applied when switched back ON.
// Turning the display backlight OFF or to 0% blanks the screen — select it
// with RPT if needed and press Play (or REV/FF back up), or press EJECT to
// leave the test, which restores it to 100%.
//
// Both pins run LEDC PWM at 5 kHz, 12-bit. GPIO9 is attached to PWM once in
// setup() (function_test.ino), after tft.init().
// ---------------------------------------------------------------------------

#define LED_PWM_BITS   12
#define LED_PWM_MAX    ((1 << LED_PWM_BITS) - 1)
#define LED_DIVIDER    3
#define LED_COL2_X     160

// struct PwmChannel is in function_test.h (Arduino's generated prototypes
// need the type before this tab)
static PwmChannel ledCh[2] = {
  { "BUTTON LED", LED_PIN, 50,  50,  true },
  { "DISPLAY BL", TFT_BL,  100, 100, true },
};
static int ledSel = 0;   // 0 = button LED, 1 = display backlight

static int16_t ledLabelX, ledPctX, ledStateX, ledPctW, ledStateW, ledHintW;

static uint32_t ledPctToDuty(int pct)            { return ((uint32_t)pct * LED_PWM_MAX + 50) / 100; }
static uint32_t ledOutputDuty(const PwmChannel& c) { return c.on ? ledPctToDuty(c.pct) : 0; }
static void     ledApply(const PwmChannel& c)      { ledcWrite(c.pin, ledOutputDuty(c)); }

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void ledDrawChannel(int i) {
  const PwmChannel& c = ledCh[i];
  int16_t y = rowY(i, LED_DIVIDER);

  // Selection marker
  drawValue(i == ledSel ? ">" : " ", SIDE_MARGIN, y, COLOR_HIGH, tft.textWidth(">", FONT));

  drawValue(String(c.pct) + "%", ledPctX, y, c.pct == 0 ? COLOR_LOW : COLOR_HIGH, ledPctW);
  drawValue(c.on ? "ON" : "OFF", ledStateX, y, c.on ? COLOR_HIGH : COLOR_LOW, ledStateW);

  char raw[24];
  snprintf(raw, sizeof(raw), "GPIO%d %lu/%d", c.pin, (unsigned long)ledOutputDuty(c), LED_PWM_MAX);
  drawHint(raw, y, ledHintW);
}

static void ledDrawSelected() {
  int16_t y = rowY(2, LED_DIVIDER);
  tft.fillRect(ledPctX, y, DISP_W - ledPctX, ROW_H, COLOR_BG);
  drawValue(ledCh[ledSel].name, ledPctX, y, COLOR_HIGH, 0);
  drawHint("RPT = switch", y);
}

// One instruction cell: "Name:" in white, action in white after a fixed gap
static void ledDrawHelp(const char* name, const char* action, int16_t x, int16_t actionX, int row) {
  int16_t y = rowY(row, LED_DIVIDER);
  drawLabel((String(name) + ":").c_str(), x, y);
  drawLabel(action, actionX, y);
}

// ---------------------------------------------------------------------------
// Mode interface
// ---------------------------------------------------------------------------

void ledEnter() {
  for (PwmChannel& c : ledCh) {
    c.pct = c.startPct;
    c.on  = true;
    ledApply(c);
  }
  ledSel = 0;   // start on the button backlight

  // Column positions for the two channel rows + ADJUSTING row
  ledLabelX = SIDE_MARGIN + tft.textWidth("> ", FONT);
  const char* labels[] = { ledCh[0].name, ledCh[1].name };
  ledPctX   = max<int16_t>(labelColumn(labels, 2, ledLabelX),
                           SIDE_MARGIN + tft.textWidth("ADJUSTING: ", FONT));
  ledPctW   = tft.textWidth("100%", FONT);
  ledStateX = ledPctX + ledPctW + tft.textWidth("  ", FONT);
  ledStateW = tft.textWidth("OFF", FONT);
  ledHintW  = tft.textWidth("GPIO4 4095/4095", FONT);

  for (int i = 0; i < 2; i++) {
    drawLabel((String(ledCh[i].name) + ":").c_str(), ledLabelX, rowY(i, LED_DIVIDER));
  }
  drawLabel("ADJUSTING:", SIDE_MARGIN, rowY(2, LED_DIVIDER));
  drawDivider(LED_DIVIDER);

  // Instructions in two columns
  int16_t a1 = SIDE_MARGIN + tft.textWidth("TrackUp: ", FONT);
  int16_t a2 = LED_COL2_X  + tft.textWidth("TrackDown: ", FONT);
  ledDrawHelp("TrackUp",   "+1%",              SIDE_MARGIN, a1, 3);
  ledDrawHelp("TrackDown", "-1%",              LED_COL2_X,  a2, 3);
  ledDrawHelp("FF",        "+10%",             SIDE_MARGIN, a1, 4);
  ledDrawHelp("REV",       "-10%",             LED_COL2_X,  a2, 4);
  ledDrawHelp("Play",      "ON / OFF",         SIDE_MARGIN, a1, 5);
  ledDrawHelp("RPT",       "switch LED",       LED_COL2_X,  a2, 5);
  drawHint("EJECT restores display backlight to 100%", rowY(6, LED_DIVIDER));

  ledDrawChannel(0);
  ledDrawChannel(1);
  ledDrawSelected();

  Serial.println("Button LED (GPIO4) 50% ON, display backlight (GPIO9) 100% ON.");
  Serial.println("RPT = switch BUTTON LED / DISPLAY BL, TrackUp/Down +/-1%, FF/REV +/-10%, Play on/off");
  Serial.println("Adjusting: BUTTON LED");
}

void ledExit() {
  ledcWrite(LED_PIN, 0);              // button LED off outside this test
  ledcWrite(TFT_BL, LED_PWM_MAX);     // display backlight back to full
  ledCh[1].pct = 100;
  ledCh[1].on  = true;
}

void ledLoop() {}

void ledKey(byte r, byte c) {
  if (r == 3 && c == 0) {             // RPT: switch channel
    int prev = ledSel;
    ledSel = 1 - ledSel;
    ledDrawChannel(prev);             // move the ">" marker
    ledDrawChannel(ledSel);
    ledDrawSelected();
    Serial.printf("  Adjusting: %s\n", ledCh[ledSel].name);
    return;
  }

  PwmChannel& ch = ledCh[ledSel];
  int step = 0;
  if      (r == 0 && c == 1) step = +1;    // TrackUp
  else if (r == 1 && c == 1) step = -1;    // TrackDown
  else if (r == 2 && c == 1) step = +10;   // FF
  else if (r == 3 && c == 1) step = -10;   // REV
  else if (r == 1 && c == 0) {             // Play: on/off
    ch.on = !ch.on;
    ledApply(ch);
    ledDrawChannel(ledSel);
    Serial.printf("  %s %s (set %d%%, duty %lu)\n", ch.name, ch.on ? "ON" : "OFF",
                  ch.pct, (unsigned long)ledOutputDuty(ch));
    return;
  } else {
    return;
  }

  int before = ch.pct;
  ch.pct = constrain(ch.pct + step, 0, 100);
  if (ch.pct == before) {
    Serial.printf("  %s already at %d%%\n", ch.name, ch.pct);
    return;
  }
  ledApply(ch);
  ledDrawChannel(ledSel);
  Serial.printf("  %s %+d%%: %d%% -> %d%% (duty %lu%s)\n", ch.name, step, before, ch.pct,
                (unsigned long)ledOutputDuty(ch), ch.on ? "" : ", OFF");
}

void ledPin() {}
