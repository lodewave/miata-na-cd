// ---------------------------------------------------------------------------
// Backlights — button backlight (GPIO4) and LCD backlight (GPIO9, LCD_BL_PIN)
//
// Both are LEDC PWM, 5 kHz, 12-bit. Levels follow two headunit signals on
// the MCP23017 (inputs with pullups, read every 50 ms, a change must be seen
// on two reads in a row before it's used):
//   ILLUM SENSE    GPB4
//   HU LIGHTSWITCH GPB6
//
// REVERSE LOGIC: these inputs are inverted by the PCBA. Every HIGH / LOW in
// this file (and the table below) is the level READ by the ESP32. A HIGH
// read is a LOW signal from the headunit, and vice versa. See board.h.
//
//   Levels as READ by the ESP32 (headunit sends the opposite):
//   ILLUM SENSE  HU LIGHTSWITCH | Button backlight (GPIO4)  | Display backlight (GPIO9)
//   (GPB4)       (GPB6)         |                           |
//   --------------------------- | ------------------------- | ------------------------------
//   HIGH         HIGH           | OFF                       | ON at lcd_backlight_day
//   HIGH         LOW            | OFF                       | ON at lcd_backlight_day
//   LOW          HIGH           | ON at button_backlight    | ON at lcd_backlight_ILLUM
//   LOW          LOW            | OFF                       | OFF
//
//   Menu names: button_backlight = "Button Backlight",
//   lcd_backlight_day = "LCD Backlight - Day",
//   lcd_backlight_ILLUM = "LCD Backlight - ILLUM".
//
// Settings menu preview: while the menu is open
//   - on "Button Backlight", the buttons light at that value
//   - on an LCD item, the LCD uses that item's value
//   - otherwise the LCD never goes fully OFF (falls back to the ILLUM value)
//     so the menu stays readable
// In the function test this file does nothing; the test drives both pins.
// If the MCP23017 is missing: LCD at the Day value, buttons off.
// ---------------------------------------------------------------------------

#define LIGHT_SENSE_MS 50

static bool senseIllumHigh  = true;    // filtered GPB4 (read level)
static bool senseLightSwHigh = false;  // filtered GPB6
static uint8_t lastRawB  = 0xFF;
static int  outButtonPct = -1;         // last applied, -1 = never
static int  outLcdPct    = -1;
static LightPreview preview = PREVIEW_NONE;

static uint32_t pctToDuty(int pct) {
  return ((uint32_t)pct * BL_PWM_MAX + 50) / 100;
}

// ON only when ILLUM SENSE is LOW and HU LIGHTSWITCH is HIGH
static int buttonLevel() {
  if (senseIllumHigh)    return 0;                 // ILLUM HIGH → OFF (LIGHTSW ignored)
  if (!senseLightSwHigh) return 0;                 // ILLUM LOW, LIGHTSW LOW → OFF
  return settings.buttonBacklight;                 // ILLUM LOW, LIGHTSW HIGH → ON
}

static int lcdLevel() {
  if (senseIllumHigh)   return settings.lcdBacklightDay;
  if (senseLightSwHigh) return settings.lcdBacklightIllum;
  return 0;                                        // ILLUM LOW, switch off → OFF
}

static void applyLevels() {
  int btn = buttonLevel();
  int lcd = lcdLevel();

  if (uiMode == UI_MENU) {
    switch (preview) {
      case PREVIEW_BUTTON:    btn = settings.buttonBacklight;   break;
      case PREVIEW_LCD_DAY:   lcd = settings.lcdBacklightDay;   break;
      case PREVIEW_LCD_ILLUM: lcd = settings.lcdBacklightIllum; break;
      default: break;
    }
    if (lcd == 0) lcd = settings.lcdBacklightIllum;  // keep the menu visible
  }

  if (btn != outButtonPct) {
    ledcWrite(BUTTON_BL_PIN, pctToDuty(btn));
    outButtonPct = btn;
    Serial.printf("LIGHT: button backlight %d%%\n", btn);
  }
  if (lcd != outLcdPct) {
    ledcWrite(LCD_BL_PIN, pctToDuty(lcd));
    outLcdPct = lcd;
    Serial.printf("LIGHT: LCD backlight %d%%\n", lcd);
  }
}

// Call after initDisplay(). Attaches PWM with both backlights still OFF:
// setup() keeps them LOW so nothing lights before the first screen is
// drawn. The first lightingLoop() (in loop(), after setup() has drawn the
// Radio screen) turns them on. Nothing may digitalWrite() GPIO9 or GPIO4
// after this.
void initLighting() {
  ledcAttach(BUTTON_BL_PIN, BL_PWM_FREQ_HZ, BL_PWM_BITS);
  ledcAttach(LCD_BL_PIN,    BL_PWM_FREQ_HZ, BL_PWM_BITS);
  ledcWrite(BUTTON_BL_PIN, 0);
  ledcWrite(LCD_BL_PIN, 0);
  outButtonPct = 0;
  outLcdPct    = 0;
}

void lightingLoop() {
  if (uiMode == UI_FUNCTION_TEST) return;

  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs < LIGHT_SENSE_MS) return;
  lastMs = now;

  if (mcp_initialized) {
    uint8_t b = mcp.readGPIOB();
    const uint8_t mask = (1 << (PIN_ILLUM_SENSE - 8)) | (1 << (PIN_HU_LIGHTSW - 8));
    // Accept a change only when two reads in a row agree
    if ((b & mask) == (lastRawB & mask)) {
      bool illum = b & (1 << (PIN_ILLUM_SENSE - 8));
      bool sw    = b & (1 << (PIN_HU_LIGHTSW - 8));
      if (illum != senseIllumHigh || sw != senseLightSwHigh) {
        senseIllumHigh   = illum;
        senseLightSwHigh = sw;
        Serial.printf("LIGHT: ILLUM SENSE %s, HU LIGHTSWITCH %s\n",
                      illum ? "HIGH" : "LOW", sw ? "HIGH" : "LOW");
      }
    }
    lastRawB = b;
  }
  applyLevels();
}

void lightingRefresh() {
  if (uiMode != UI_FUNCTION_TEST) applyLevels();
}

void lightingSetPreview(LightPreview p) {
  preview = p;
  lightingRefresh();
}
