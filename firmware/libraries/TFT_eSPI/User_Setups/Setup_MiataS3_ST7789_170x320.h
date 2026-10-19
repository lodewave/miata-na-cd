// TFT_eSPI User Setup — Miata NA primary ESP32-S3 (ESP32-S3-WROOM-1)
// ST7789 1.9" 170×320 on FSPI (SPI2)
//
// Install:
//   1. Copy this file to  ~/Arduino/libraries/TFT_eSPI/User_Setups/
//   2. In ~/Arduino/libraries/TFT_eSPI/User_Setup_Select.h, make this the
//      only active setup include:
//        #include <User_Setups/Setup_MiataS3_ST7789_170x320.h>
//
// Pins must match primary_esp32s3/config.h (PCBA net names SPI1_*).

#define USER_SETUP_ID 900

#define ST7789_DRIVER
#define TFT_RGB_ORDER TFT_BGR   // Colour order Blue-Green-Red
#define TFT_WIDTH  170
#define TFT_HEIGHT 320
#define TFT_INVERSION_ON

// ESP32-S3 pins
#define TFT_MOSI 14   // SPI1_MOSI
#define TFT_SCLK 13   // SPI1_SCLK
#define TFT_CS   12   // SPI1_CS
#define TFT_DC   11   // SPI1_DC
#define TFT_RST  10   // SPI1_RST
// Backlight (SPI1_BL, GPIO9): TFT_BL and TFT_BACKLIGHT_ON are deliberately
// NOT defined. TFT_eSPI's ST7789 init (TFT_Drivers/ST7789_Init.h) ends with
// digitalWrite(TFT_BL, HIGH) whenever TFT_BL is defined — that lit the
// backlight over the panel's uninitialised RAM (white flash ~1 s after
// reset). The firmware drives GPIO9 itself as LCD_BL_PIN (board.h): LOW
// from the first line of setup(), PWM from lighting.ino once a screen is
// drawn. config.h stops the build if either macro is defined.

// MISO is not wired (the panel is write-only), but it MUST be defined as a
// pin that differs from MOSI. If TFT_MISO is left undefined (or -1),
// TFT_eSPI on the S3 silently sets TFT_MISO = TFT_MOSI and calls
// spi.begin(SCLK, 14, 14). Arduino-ESP32 core 3.x then sees GPIO14 being
// re-attached, detaches MISO, and stops the whole SPI bus — tft.init()
// hangs forever on its first byte.
// GPIO33 is used as a dummy input: on the ESP32-S3-WROOM-1-N8 it is not
// brought out to any module pad, so it can't conflict with the PCBA.
#define TFT_MISO 33

// Required on ESP32-S3 with Arduino-ESP32 core 3.x: the core redefined FSPI
// as 0, so TFT_eSPI's default would point at the flash SPI registers and
// crash/hang in tft.init(). USE_FSPI_PORT selects the correct SPI2 registers.
#define USE_FSPI_PORT

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// 40 MHz for bring-up. SCLK (GPIO13) and MOSI (GPIO14) are not the S3's
// FSPI IO_MUX pins (FSPICLK=GPIO12, FSPID=GPIO11), so they go through the
// GPIO matrix. 80 MHz may work once the display is confirmed; the original
// tft.init() hang was caused by the MISO issue above, not the clock.
#define SPI_FREQUENCY 40000000

// No touch controller on this display — silences the TOUCH_CS build warning
#define DISABLE_ALL_LIBRARY_WARNINGS
