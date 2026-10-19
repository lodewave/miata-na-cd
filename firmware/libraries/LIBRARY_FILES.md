# Custom library files

Files changed or added inside installed Arduino libraries. They are not part
of the sketch folders, so a fresh library install loses them. Restore them
after installing the libraries below.

These live in the Arduino sketchbook `libraries/` folder (Arduino IDE →
Settings → Sketchbook location, then `libraries/` inside it). The paths below
are relative to that `libraries/` folder.

| File in this zip | Belongs in | What it is |
|---|---|---|
| `libraries/TFT_eSPI/User_Setup_Select.h` | `libraries/TFT_eSPI/User_Setup_Select.h` (replace) | Library file, edited: the only active include is `#include <User_Setups/Setup_MiataS3_ST7789_170x320.h>` |
| `libraries/TFT_eSPI/User_Setups/Setup_MiataS3_ST7789_170x320.h` | `libraries/TFT_eSPI/User_Setups/` (add) | **Required.** Display setup for the primary ESP32-S3 PCBA (`USER_SETUP_ID 900`). Same as `tft_espi_setup/` in the project, which is the master copy |
| `libraries/TFT_eSPI/User_Setups/Setup408_ESP32_ST7789_170x320.h` | `libraries/TFT_eSPI/User_Setups/` (add) | Old setup for the earlier ESP32 board (backlight on GPIO21). Not used by the current firmware; kept for the archived sketches |

Only the Miata setup file is needed to build the current firmware, together
with the one-line include in `User_Setup_Select.h`. Rather than replacing
`User_Setup_Select.h` on a newer TFT_eSPI version, you can edit the new file:
comment out its default `#include <User_Setup.h>` and add the Miata include.
The firmware's `config.h` stops the build with a message if the wrong setup
is active (`USER_SETUP_ID` must be 900, and `TFT_BL` / `TFT_BACKLIGHT_ON`
must not be defined).

**Use the v3.8 setup file (in this zip) or newer.** Older copies define
`TFT_BL 9`; TFT_eSPI's ST7789 init then switches the backlight on during
`tft.init()`, which causes a white flash at power-up. The firmware drives
GPIO9 itself (`LCD_BL_PIN` in `board.h`). If you see the `TFT_BL` build
error, re-copy the setup file from this zip.

After installing on a new PC: build `primary_esp32s3` once. If it compiles,
the setup is active and correct (the checks above are compile-time).

No other library was modified (checked by file dates on 2026-10-18, again
for the v3.8 backup). All three TFT_eSPI files in this zip match the
installed ones on the source PC.

## Library versions on the source PC

| Library | Version | Used by |
|---|---|---|
| ESP32 core (Espressif, Boards Manager) | 3.3.8 | both boards |
| TFT_eSPI (Bodmer) | 2.5.43 | primary |
| Adafruit MCP23017 Arduino Library | 2.3.2 | primary |
| Adafruit BusIO | 1.17.4 | primary (MCP23017 dependency) |
| arduino-audio-tools (pschatzmann, GitHub zip `-main`) | 1.2.2 | both boards |
| arduino-libhelix (pschatzmann, GitHub zip `-main`) | 0.9.3 | primary (MP3 decoder) |
| ESP32-A2DP (pschatzmann, GitHub zip `-main`) | 1.8.10 | BT ESP32 |

Also installed but not used by the current firmware: Adafruit_Keypad 1.3.2,
Adafruit_NeoPixel 1.15.4, ButtonMatrix 1.0.3, ESP32-audioI2S 3.4.6,
Keypad 3.1.1.
