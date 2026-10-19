#ifndef CONFIG_H
#define CONFIG_H

// ---------------------------------------------------------------------------
// LIBRARY INCLUDES
// ---------------------------------------------------------------------------

#include <TFT_eSPI.h>
#include <Adafruit_MCP23X17.h>
#include <SD.h>
#include <SPI.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "DSEG14Classic_Regular_18.h"

#include "board.h"    // all pin assignments + display window geometry
#include "keypad.h"   // keypad state shared with the function test
#include "mp3_files.h" // SD card file rules shared with the function test
#include "src/function_test/function_test_api.h"

#define SEGMENT_FONT &DSEG14Classic_Regular18pt7b

// ---------------------------------------------------------------------------
// TFT_eSPI SETUP CHECKS
// ---------------------------------------------------------------------------
// The TFT_* pin macros come from the TFT_eSPI setup file
// Setup_MiataS3_ST7789_170x320.h (copy kept in tft_espi_setup/). It must be
// the active include in TFT_eSPI/User_Setup_Select.h.
#if !defined(USER_SETUP_ID) || (USER_SETUP_ID != 900)
  #error "Wrong TFT_eSPI setup. Select Setup_MiataS3_ST7789_170x320.h in TFT_eSPI/User_Setup_Select.h"
#endif
#if (TFT_MOSI != 14) || (TFT_SCLK != 13) || (TFT_CS != 12) || \
    (TFT_DC != 11) || (TFT_RST != 10) || !defined(USE_FSPI_PORT)
  #error "TFT_eSPI setup pins do not match the PCBA (SPI1_*). Check Setup_MiataS3_ST7789_170x320.h"
#endif
// The firmware owns the backlight (LCD_BL_PIN, board.h). If the TFT_eSPI
// setup defines TFT_BL, the ST7789 init switches it on during tft.init(),
// over the panel's uninitialised RAM (white flash at power-up).
#if defined(TFT_BL) || defined(TFT_BACKLIGHT_ON)
  #error "Remove TFT_BL / TFT_BACKLIGHT_ON from Setup_MiataS3_ST7789_170x320.h (copy in tft_espi_setup/, install it in TFT_eSPI/User_Setups/) - they cause a white flash at power-up"
#endif
// TFT_MISO must not equal TFT_MOSI, or core 3.x stops the SPI bus and
// tft.init() hangs. GPIO33 is the dummy MISO — don't assign it elsewhere.
#if (TFT_MISO == TFT_MOSI)
  #error "TFT_MISO equals TFT_MOSI - tft.init() will hang. Define TFT_MISO 33 in the TFT_eSPI setup."
#endif

// ---------------------------------------------------------------------------
// RELAY & INPUT SWITCHING
// ---------------------------------------------------------------------------
//
// RADIO_RELAY (GPB1): LOW → radio to output, HIGH → DAC_RELAY output
// DAC_RELAY   (GPB0): LOW → DAC,             HIGH → AUX-IN
//
// Radio: RADIO=LOW,  DAC=LOW     DAC (BT/MP3): RADIO=HIGH, DAC=LOW
// AUX:   RADIO=HIGH, DAC=HIGH
// ---------------------------------------------------------------------------

// Relay switching sequence (actions.ino switchRelaysMuted):
//   AMP_MUTE HIGH + MODULE_ACTIVE (same instant) → wait 100 ms → switch
//   relay(s), 20 ms between two → wait 200 ms → AMP_MUTE LOW.
//   Skipped entirely if no relay changes.
#define AMP_MUTE_BEFORE_MS 100  // mute held this long before switching relays
#define AMP_MUTE_AFTER_MS  200  // mute held this long after the last relay
#define RELAY_SWITCH_DELAY 20   // between the two relays when both change

// I2S source switching (actions.ino setI2SSourceMuted):
//   DAC_MUTE LOW → 5 ms → toggle I2S_SWITCH → 5 ms → DAC_MUTE HIGH
#define DAC_MUTE_SWITCH_MS 5

// RADIO_SENSE (GPB5) held LOW for RADIO_SENSE_HOLD_MS → MODULE_ACTIVE LOW →
// wait RADIO_SENSE_MODULE_MS → switch to the Radio input
#define RADIO_SENSE_HOLD_MS   100
#define RADIO_SENSE_MODULE_MS 100
#define RADIO_SENSE_POLL_MS   10   // one Port B read (~0.1 ms at 400 kHz)

// EJECT cycles in this order (numeric order matters)
enum InputState {
  INPUT_RADIO = 0,
  INPUT_BT    = 1,   // skipped by EJECT when "BT Enable" is off
  INPUT_MP3   = 2,   // skipped by EJECT when "MP3 Enable" is off
  INPUT_AUX   = 3    // skipped by EJECT when "Aux Enable" is off
};

#define NUM_INPUT_STATES 4

// ---------------------------------------------------------------------------
// SPLASH SCREEN
// ---------------------------------------------------------------------------

#define SPLASH_DURATION_MS 3000

// ---------------------------------------------------------------------------
// DISPLAY SETTINGS (window geometry is in board.h)
// ---------------------------------------------------------------------------

#define TEXT_COLOR  0x54E4  // #A8C64E — yellow-green
#define LABEL_COLOR 0x54E4
#define BG_COLOR    TFT_BLACK

#define TOP_MARGIN  2       // Top margin for each section
#define LABEL_GAP   1       // Gap between label and text
#define LABEL_FONT  2
#define HALF_H      (DISP_H / 2)

// Scroll timing: per-character interval is the "Scroll Speed" setting
#define PAUSE_MS           2000

// ---------------------------------------------------------------------------
// MP3 PLAYER SETTINGS
// ---------------------------------------------------------------------------

// File limits and which files are played: mp3_files.h
#define TRACK_RESTART_THRESHOLD_MS 3000

// ---------------------------------------------------------------------------
// PERSISTENT SETTINGS — settings.ino (stored in NVS flash via Preferences)
// ---------------------------------------------------------------------------

struct Settings {
  int16_t mp3VolumeHalfDb;   // MP3 volume in 0.5 dB steps: -24 = -12.0 dBFS
  uint8_t buttonBacklight;   // %  (0–100)
  uint8_t lcdBacklightDay;   // %  (5–100)
  uint8_t lcdBacklightIllum; // %  (5–100)
  bool    btEnable;          // BT input + Bluetooth on the BT ESP32
  bool    btAutoReconnect;   // BT ESP32 reconnects to the last phone at power-up
  bool    mp3Enable;
  bool    auxEnable;
  uint8_t scrollTenths;      // seconds per character × 10: 15 = 1.5 s
};

// Defaults (used on first boot and if stored values are out of range)
#define DEF_MP3_VOLUME_HALF_DB  -24   // -12.0 dBFS
#define DEF_BUTTON_BACKLIGHT     70
#define DEF_LCD_BACKLIGHT_DAY    90
#define DEF_LCD_BACKLIGHT_ILLUM  50
#define DEF_BT_ENABLE            true
#define DEF_BT_AUTO_RECONNECT    true
#define DEF_MP3_ENABLE           true
#define DEF_AUX_ENABLE           true
#define DEF_SCROLL_TENTHS        15   // 1.5 s per character

// Limits
#define MP3_VOLUME_MIN_HALF_DB  -120  // -60.0 dBFS
#define MP3_VOLUME_MAX_HALF_DB     0  //   0.0 dBFS (full scale)
#define BUTTON_BL_MIN              0
#define LCD_BL_MIN                 5  // never fully dark from a setting
#define SCROLL_TENTHS_MIN          2  // 0.2 s
#define SCROLL_TENTHS_MAX         50  // 5.0 s

extern Settings settings;
void loadSettings();
void saveSettings();                   // writes only values that changed
float    mp3VolumeDb();                // settings → dBFS
float    mp3VolumeGain();              // settings → linear gain
int      mp3VolumePeak();              // settings → peak sample / 32767
uint32_t scrollIntervalMs();

// ---------------------------------------------------------------------------
// UI MODE — who owns the display and the keypad
// ---------------------------------------------------------------------------

enum UiMode {
  UI_NORMAL,        // input screens; normal button actions
  UI_MENU,          // settings menu (settings_menu.ino); audio keeps playing
  UI_FUNCTION_TEST  // function test (src/function_test/); exits by restart
};

extern UiMode uiMode;
bool displayIsMine();   // true in UI_NORMAL — main screens may draw

// ---------------------------------------------------------------------------
// DATA STRUCTURES
// ---------------------------------------------------------------------------

struct ScrollLine {
  String   text;
  int      charOffset;
  bool     scrolling;
  bool     pausing;
  uint32_t nextScrollMs;
};

// ---------------------------------------------------------------------------
// SHARED GLOBALS & FORWARD DECLARATIONS
// ---------------------------------------------------------------------------

// Globals defined in primary_esp32s3.ino
extern TFT_eSPI tft;
extern InputState currentInput;

// Splash screen state — defined in primary_esp32s3.ino
extern bool     splashActive;
extern uint32_t splashEndMs;

// Scroll line instances — defined in display.ino
extern ScrollLine artistLine;
extern ScrollLine titleLine;

// MCP23017 instance — defined in keypad.ino
extern Adafruit_MCP23X17 mcp;
extern bool mcp_initialized;

// Shared hardware objects — defined in mp3player.ino (also used by the
// function test)
extern I2SStream mp3_i2s;
extern SPIClass  sdSPI;

// Forward declarations for functions taking custom types
void switchInput(InputState newState);
void prepareScroll(ScrollLine &sl, const String &text);
bool inputEnabled(InputState s);
void setRelays(uint8_t radioRelay, uint8_t dacRelay);        // raw (function test)
void switchRelaysMuted(uint8_t radioRelay, uint8_t dacRelay,
                       uint8_t moduleActive);                 // with AMP_MUTE sequence
void setI2SSourceMuted(uint8_t level);                       // with DAC_MUTE sequence
void radioSenseLoop();                                       // RADIO_SENSE → Radio

// Display — display.ino (menu drawing helpers used by settings_menu.ino)
void displayMenuBegin();               // clear the frame
void displayMenuText(const String& text, int16_t x, int16_t y, uint16_t color, uint8_t datum);
void displayMenuLine(int16_t y, uint16_t color);
void displayMenuEnd();                 // push the frame
int16_t displayMenuTextWidth(const String& text);

// Backlights — lighting.ino
enum LightPreview { PREVIEW_NONE, PREVIEW_BUTTON, PREVIEW_LCD_DAY, PREVIEW_LCD_ILLUM };
void initLighting();
void lightingLoop();
void lightingRefresh();                // re-apply now (after a setting change)
void lightingSetPreview(LightPreview p);   // settings menu: show the edited value

// Settings menu — settings_menu.ino
void menuOpen();
void menuClose();
void menuKey(byte row, byte col);

// MP3 player — defined in mp3player.ino (local playback)
extern String mp3Artist;
extern String mp3Title;
void initMP3();
void mp3Loop();
void mp3PlayPause();                   // Play button: toggle
void mp3Pause();
void mp3Resume();
void mp3NextTrack();
void mp3PrevTrack();
bool mp3IsPlaying();
void mp3ApplyVolume();                 // re-read the volume setting
bool i2s1Begin();                      // start I2S1 at 44.1 kHz (also the function test)

// Bluetooth interface — defined in bt_interface.ino
extern String btArtist;
extern String btTitle;
extern bool btConnected;
void showBluetoothDisplay();           // actions.ino — ARTIST/TITLE or "BT WAIT"
void showMP3Display();                 // actions.ino — MP3 ARTIST/TITLE
void initBTInterface();
void btLoop();
void btSendPlay();
void btSendPause();
void btSendNext();
void btSendPrev();
void btSendPlayPause();
void btSendStatus();
void btSendConfig();                   // BT Enable + Auto-Reconnect → BT ESP32

#endif
