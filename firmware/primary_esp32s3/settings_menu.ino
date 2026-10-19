// ---------------------------------------------------------------------------
// Settings Menu
//
// Open:  hold PIN for 1 s (from the normal screens)
// Close: press PIN — saves any changed values to flash (settings.ino)
//
//   TrackUp / TrackDown — move up / down the list
//   FF / REV            — increase / decrease (or toggle) the selected value
//   Other buttons are ignored while the menu is open; audio keeps playing.
//
//   SETTINGS 1/10                    FF/REV change  PIN save
//   ────────────────────────────────────────────────────────
//   > MP3 Volume:                       -12.0 dBFS (8231)
//     Button Backlight:                               70%
//     LCD Backlight - Day:                            90%
//     LCD Backlight - ILLUM:                          50%
//     BT Enable:                                       ON
//     BT Auto-Reconnect:                               ON
//   (6 rows visible; the list scrolls to keep the selection on screen —
//    MP3 Enable, Aux Enable, Scroll Speed, Function Test follow)
//
// Values take effect immediately (volume, backlights with preview, scroll
// speed). BT / MP3 / Aux Enable apply on close: if the current input was
// just disabled, the input switches to Radio. BT Enable and BT
// Auto-Reconnect are sent to the BT ESP32 on close (btSendConfig); changing
// BT Enable restarts it.
//
// "Function Test" (FF or REV) saves settings and starts the function test.
//
// Regular Font 2, white text, selected row in green, drawn into the sprite
// frame inside the visible window (display.ino displayMenu* helpers).
// ---------------------------------------------------------------------------

enum MenuItem {
  ITEM_MP3_VOLUME,
  ITEM_BUTTON_BL,
  ITEM_LCD_DAY,
  ITEM_LCD_ILLUM,
  ITEM_BT_ENABLE,
  ITEM_BT_AUTO_RECONNECT,
  ITEM_MP3_ENABLE,
  ITEM_AUX_ENABLE,
  ITEM_SCROLL,
  ITEM_FUNCTION_TEST,
  NUM_MENU_ITEMS
};

static const char* const MENU_LABELS[NUM_MENU_ITEMS] = {
  "MP3 Volume",
  "Button Backlight",
  "LCD Backlight - Day",
  "LCD Backlight - ILLUM",
  "BT Enable",
  "BT Auto-Reconnect",
  "MP3 Enable",
  "Aux Enable",
  "Scroll Speed",
  "Function Test",
};

#define MENU_ROW_H     16
#define MENU_VISIBLE   6
#define MENU_LIST_TOP  (1 + MENU_ROW_H + 3)   // title row + divider gap → y 20
#define MENU_COLOR_SEL TFT_GREEN
#define MENU_COLOR_TXT TFT_WHITE
#define MENU_COLOR_HNT TFT_DARKGREY

static int menuSel = 0;   // selected item
static int menuTop = 0;   // first visible item

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

// ON/OFF items: the setting they toggle, or nullptr
static bool* menuToggle(int item) {
  switch (item) {
    case ITEM_BT_ENABLE:         return &settings.btEnable;
    case ITEM_BT_AUTO_RECONNECT: return &settings.btAutoReconnect;
    case ITEM_MP3_ENABLE:        return &settings.mp3Enable;
    case ITEM_AUX_ENABLE:        return &settings.auxEnable;
    default:                     return nullptr;
  }
}

// Backlight % items: the setting they change (and its minimum), or nullptr
static uint8_t* menuPercent(int item, int* minPct) {
  switch (item) {
    case ITEM_BUTTON_BL: *minPct = BUTTON_BL_MIN; return &settings.buttonBacklight;
    case ITEM_LCD_DAY:   *minPct = LCD_BL_MIN;    return &settings.lcdBacklightDay;
    case ITEM_LCD_ILLUM: *minPct = LCD_BL_MIN;    return &settings.lcdBacklightIllum;
    default:                                      return nullptr;
  }
}

static String menuValueText(int item) {
  if (bool* flag = menuToggle(item)) return *flag ? "ON" : "OFF";
  int minPct;
  if (uint8_t* pct = menuPercent(item, &minPct)) return String(*pct) + "%";

  char buf[32];
  switch (item) {
    case ITEM_MP3_VOLUME:
      snprintf(buf, sizeof(buf), "%.1f dBFS (%d)", mp3VolumeDb(), mp3VolumePeak());
      return buf;
    case ITEM_SCROLL:
      snprintf(buf, sizeof(buf), "%.1f s", settings.scrollTenths / 10.0f);
      return buf;
    case ITEM_FUNCTION_TEST: return "FF/REV = start";
  }
  return "";
}

static int stepClamp(int v, int step, int lo, int hi) {
  return constrain(v + step, lo, hi);
}

// step = +1 (FF) or -1 (REV)
static void menuAdjust(int step) {
  int minPct;
  if (bool* flag = menuToggle(menuSel)) {
    *flag = !*flag;                     // applied on close (menuClose)
  } else if (uint8_t* pct = menuPercent(menuSel, &minPct)) {
    *pct = stepClamp(*pct, step, minPct, 100);
    lightingRefresh();                  // live preview
  } else switch (menuSel) {
    case ITEM_MP3_VOLUME:
      settings.mp3VolumeHalfDb = stepClamp(settings.mp3VolumeHalfDb, step,
                                           MP3_VOLUME_MIN_HALF_DB, MP3_VOLUME_MAX_HALF_DB);
      mp3ApplyVolume();
      break;
    case ITEM_SCROLL:
      settings.scrollTenths = stepClamp(settings.scrollTenths, step,
                                        SCROLL_TENTHS_MIN, SCROLL_TENTHS_MAX);
      break;
    case ITEM_FUNCTION_TEST: {
      Serial.println("MENU: starting function test");
      saveSettings();
      // Stop audio: the test uses the SD card and I2S, and exits by restart
      mp3Pause();
      if (currentInput == INPUT_BT) btSendPause();
      lightingSetPreview(PREVIEW_NONE);
      uiMode = UI_FUNCTION_TEST;
      ftestBegin();
      return;
    }
  }
  Serial.printf("MENU: %s = %s\n", MENU_LABELS[menuSel], menuValueText(menuSel).c_str());
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void menuDraw() {
  displayMenuBegin();

  // Title row + divider
  displayMenuText("SETTINGS " + String(menuSel + 1) + "/" + String(NUM_MENU_ITEMS),
                  SIDE_MARGIN, 1, MENU_COLOR_TXT, TL_DATUM);
  displayMenuText("FF/REV change  PIN save", DISP_W - SIDE_MARGIN, 1, MENU_COLOR_HNT, TR_DATUM);
  displayMenuLine(1 + MENU_ROW_H + 1, MENU_COLOR_HNT);

  // Visible part of the list
  int16_t labelX = SIDE_MARGIN + displayMenuTextWidth("> ");
  for (int i = 0; i < MENU_VISIBLE; i++) {
    int item = menuTop + i;
    if (item >= NUM_MENU_ITEMS) break;
    int16_t y = MENU_LIST_TOP + i * MENU_ROW_H;
    bool sel = (item == menuSel);
    if (sel) displayMenuText(">", SIDE_MARGIN, y, MENU_COLOR_SEL, TL_DATUM);
    displayMenuText(String(MENU_LABELS[item]) + ":", labelX, y,
                    sel ? MENU_COLOR_SEL : MENU_COLOR_TXT, TL_DATUM);
    uint16_t valColor = sel ? MENU_COLOR_SEL :
                        (item == ITEM_FUNCTION_TEST ? MENU_COLOR_HNT : MENU_COLOR_TXT);
    displayMenuText(menuValueText(item), DISP_W - SIDE_MARGIN, y, valColor, TR_DATUM);
  }

  displayMenuEnd();
}

// Backlight preview follows the selected item (see lighting.ino)
static void menuUpdatePreview() {
  switch (menuSel) {
    case ITEM_BUTTON_BL: lightingSetPreview(PREVIEW_BUTTON);    break;
    case ITEM_LCD_DAY:   lightingSetPreview(PREVIEW_LCD_DAY);   break;
    case ITEM_LCD_ILLUM: lightingSetPreview(PREVIEW_LCD_ILLUM); break;
    default:             lightingSetPreview(PREVIEW_NONE);      break;
  }
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void menuOpen() {
  Serial.println("MENU: open");
  uiMode  = UI_MENU;
  menuSel = 0;
  menuTop = 0;
  menuUpdatePreview();
  menuDraw();
}

void menuClose() {
  Serial.println("MENU: close");
  saveSettings();
  uiMode = UI_NORMAL;
  lightingSetPreview(PREVIEW_NONE);

  // An input that was just disabled can't stay selected
  if (!inputEnabled(currentInput)) {
    Serial.println("MENU: current input disabled - switching to Radio");
    switchInput(INPUT_RADIO);      // redraws the screen
  } else {
    updateDisplayForState();       // actions.ino — redraw the current input
  }

  // After any input switch (so a BT pause goes out first). The BT ESP32
  // ignores it if nothing changed.
  btSendConfig();
}

void menuKey(byte row, byte col) {
  switch (keyId(row, col)) {
    case KEY_TRACK_UP:   if (menuSel > 0) menuSel--;                  break;   // move up
    case KEY_TRACK_DOWN: if (menuSel < NUM_MENU_ITEMS - 1) menuSel++; break;   // move down
    case KEY_FF:         menuAdjust(+1);                              break;   // increase
    case KEY_REV:        menuAdjust(-1);                              break;   // decrease
    default:             return;                                               // ignored
  }
  if (uiMode != UI_MENU) return;                 // Function Test took over

  // Keep the selection inside the visible window
  if (menuSel < menuTop) menuTop = menuSel;
  if (menuSel >= menuTop + MENU_VISIBLE) menuTop = menuSel - MENU_VISIBLE + 1;

  menuUpdatePreview();
  menuDraw();
}
