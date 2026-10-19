// ---------------------------------------------------------------------------
// Persistent Settings — stored in NVS flash with the Preferences library
//
// NVS survives power loss, resets and firmware uploads (unless "Erase All
// Flash Before Sketch Upload" is enabled). Writes are atomic: a power cut
// during a save leaves either the old or the new value, never a corrupt one.
//
// Namespace "miata". Each value is its own key, written only when it differs
// from what's stored, so saving an unchanged menu costs no flash writes.
// Out-of-range stored values fall back to the default.
// bt_en / bt_auto are also stored on the BT ESP32 (so they apply from its
// power-up); btSendConfig() keeps that copy in sync.
//
//   Key         Type    Setting                     Default
//   mp3_vol     int16   MP3 volume, 0.5 dB steps    -24 (-12.0 dBFS)
//   btn_bl      uint8   Button backlight %          70
//   lcd_day     uint8   LCD backlight (day) %       90
//   lcd_ill     uint8   LCD backlight (ILLUM) %     50
//   bt_en       bool    Bluetooth enabled           true
//   bt_auto     bool    BT auto-reconnect           true
//   mp3_en      bool    MP3 input enabled           true
//   aux_en      bool    AUX input enabled           true
//   scroll      uint8   Scroll speed, 0.1 s steps   15 (1.5 s)
// ---------------------------------------------------------------------------

#include <Preferences.h>

#define SETTINGS_NAMESPACE "miata"

Settings settings = {
  DEF_MP3_VOLUME_HALF_DB, DEF_BUTTON_BACKLIGHT, DEF_LCD_BACKLIGHT_DAY,
  DEF_LCD_BACKLIGHT_ILLUM, DEF_BT_ENABLE, DEF_BT_AUTO_RECONNECT,
  DEF_MP3_ENABLE, DEF_AUX_ENABLE, DEF_SCROLL_TENTHS,
};

static Settings stored;          // what's currently in flash
static Preferences prefs;

static int clampInt(int v, int lo, int hi, int def) {
  return (v < lo || v > hi) ? def : v;
}

void loadSettings() {
  if (!prefs.begin(SETTINGS_NAMESPACE, true)) {   // read-only; fails if never written
    Serial.println("SETTINGS: none stored yet - using defaults");
    stored = settings;
    return;
  }
  settings.mp3VolumeHalfDb   = clampInt(prefs.getShort("mp3_vol", DEF_MP3_VOLUME_HALF_DB),
                                        MP3_VOLUME_MIN_HALF_DB, MP3_VOLUME_MAX_HALF_DB,
                                        DEF_MP3_VOLUME_HALF_DB);
  settings.buttonBacklight   = clampInt(prefs.getUChar("btn_bl", DEF_BUTTON_BACKLIGHT),
                                        BUTTON_BL_MIN, 100, DEF_BUTTON_BACKLIGHT);
  settings.lcdBacklightDay   = clampInt(prefs.getUChar("lcd_day", DEF_LCD_BACKLIGHT_DAY),
                                        LCD_BL_MIN, 100, DEF_LCD_BACKLIGHT_DAY);
  settings.lcdBacklightIllum = clampInt(prefs.getUChar("lcd_ill", DEF_LCD_BACKLIGHT_ILLUM),
                                        LCD_BL_MIN, 100, DEF_LCD_BACKLIGHT_ILLUM);
  settings.btEnable          = prefs.getBool("bt_en",   DEF_BT_ENABLE);
  settings.btAutoReconnect   = prefs.getBool("bt_auto", DEF_BT_AUTO_RECONNECT);
  settings.mp3Enable         = prefs.getBool("mp3_en", DEF_MP3_ENABLE);
  settings.auxEnable         = prefs.getBool("aux_en", DEF_AUX_ENABLE);
  settings.scrollTenths      = clampInt(prefs.getUChar("scroll", DEF_SCROLL_TENTHS),
                                        SCROLL_TENTHS_MIN, SCROLL_TENTHS_MAX, DEF_SCROLL_TENTHS);
  prefs.end();
  stored = settings;

  Serial.printf("SETTINGS: MP3 vol %.1f dBFS, button BL %d%%, LCD day %d%% / ILLUM %d%%, "
                "BT %s (auto-reconnect %s), MP3 %s, AUX %s, scroll %.1f s\n",
                mp3VolumeDb(), settings.buttonBacklight, settings.lcdBacklightDay,
                settings.lcdBacklightIllum, settings.btEnable ? "on" : "off",
                settings.btAutoReconnect ? "on" : "off", settings.mp3Enable ? "on" : "off",
                settings.auxEnable ? "on" : "off", settings.scrollTenths / 10.0f);
}

void saveSettings() {
  if (!prefs.begin(SETTINGS_NAMESPACE, false)) {
    Serial.println("SETTINGS: ERROR - could not open NVS for writing");
    return;
  }
  int writes = 0;
  if (settings.mp3VolumeHalfDb   != stored.mp3VolumeHalfDb)   { prefs.putShort("mp3_vol", settings.mp3VolumeHalfDb);   writes++; }
  if (settings.buttonBacklight   != stored.buttonBacklight)   { prefs.putUChar("btn_bl",  settings.buttonBacklight);   writes++; }
  if (settings.lcdBacklightDay   != stored.lcdBacklightDay)   { prefs.putUChar("lcd_day", settings.lcdBacklightDay);   writes++; }
  if (settings.lcdBacklightIllum != stored.lcdBacklightIllum) { prefs.putUChar("lcd_ill", settings.lcdBacklightIllum); writes++; }
  if (settings.btEnable          != stored.btEnable)          { prefs.putBool("bt_en",    settings.btEnable);          writes++; }
  if (settings.btAutoReconnect   != stored.btAutoReconnect)   { prefs.putBool("bt_auto",  settings.btAutoReconnect);   writes++; }
  if (settings.mp3Enable         != stored.mp3Enable)         { prefs.putBool("mp3_en",   settings.mp3Enable);         writes++; }
  if (settings.auxEnable         != stored.auxEnable)         { prefs.putBool("aux_en",   settings.auxEnable);         writes++; }
  if (settings.scrollTenths      != stored.scrollTenths)      { prefs.putUChar("scroll",  settings.scrollTenths);      writes++; }
  prefs.end();
  stored = settings;
  Serial.printf("SETTINGS: saved (%d value%s changed)\n", writes, writes == 1 ? "" : "s");
}

// ---------------------------------------------------------------------------
// Derived values
// ---------------------------------------------------------------------------

float mp3VolumeDb() {
  return settings.mp3VolumeHalfDb * 0.5f;
}

float mp3VolumeGain() {
  return powf(10.0f, mp3VolumeDb() / 20.0f);
}

// Peak sample of a full-scale MP3 after the gain, out of 32767
int mp3VolumePeak() {
  return (int)lroundf(32767.0f * mp3VolumeGain());
}

uint32_t scrollIntervalMs() {
  return settings.scrollTenths * 100UL;
}
