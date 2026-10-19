// ---------------------------------------------------------------------------
// board.h — PCBA pin assignments and display window geometry
//
// Pins only, no library includes, so both the main firmware (via config.h)
// and the function test (src/function_test/) share one definition.
//
// Module: ESP32-S3-WROOM-1-N8 (8MB quad SPI flash, no PSRAM)
//   - GPIO 0, 3, 45, 46 — Strapping pins (avoid)
//   - GPIO 19, 20 — USB D-/D+ (native USB)
//   - GPIO 26-32 — Internal SPI flash (not brought out)
//   - GPIO 33 — reserved as the TFT_eSPI dummy MISO (not brought out on N8)
//   - GPIO 35-37 — Brought out and usable (only lost on PSRAM variants)
// Available: GPIO 1-2, 4-18, 21, 35-42, 47-48 (19-20 reserved for USB)
// ---------------------------------------------------------------------------
#pragma once

// ---------------------------------------------------------------------------
// ESP32-S3 GPIO
// ---------------------------------------------------------------------------

// I2S1 (schematic name) — MP3 audio from this ESP32-S3 → TMUX1574 → PCM5102A.
// No MCLK: the PCM5102A generates its own from BCK.
// (I2S2 is the BT ESP32's output — the other TMUX1574 input.)
#define I2S_BCK      6    // I2S1 BCK
#define I2S_WS       15   // I2S1 LRCK
#define I2S_DATA_OUT 7    // I2S1 DATA → PCM5102A DIN

// UART1 — BT ESP32 (ESP32-WROOM-32E)
//   ESP32-S3 GPIO18 (TX) → BT ESP32 GPIO16 (RX)
//   ESP32-S3 GPIO17 (RX) ← BT ESP32 GPIO17 (TX)
#define BT_UART_TX   18
#define BT_UART_RX   17
#define BT_UART_BAUD 115200

// UART0 — Programming / Serial Monitor (USB-UART adapter)
#define PROG_TXD  43   // U0TXD
#define PROG_RXD  44   // U0RXD

// USB (native USB-OTG)
#define USB_DN   19
#define USB_DP   20

// TFT display (ST7789 170×320), FSPI — PCBA nets SPI1_*:
//   GPIO14 SPI1_MOSI  GPIO13 SPI1_SCLK  GPIO12 SPI1_CS
//   GPIO11 SPI1_DC    GPIO10 SPI1_RST   GPIO9  SPI1_BL (backlight, PWM)
// The TFT_* macros come from the TFT_eSPI setup file
// Setup_MiataS3_ST7789_170x320.h; config.h checks they match.

// SD card, HSPI (SPI3)
#define SD_MOSI  40
#define SD_MISO  41
#define SD_SCLK  39
#define SD_CS    42

// I2C1 — MCP23017
//   MCP23017 pin 13 (SDA) ↔ GPIO1, pin 12 (SCL) ↔ GPIO2
#define I2C_SDA       1
#define I2C_SCL       2
#define I2C_CLOCK_HZ  400000  // MCP23017 is the only device on the bus

// MCP23017 interrupts (active LOW) — wired, not used yet (keypad is polled)
//   MCP23017 pin 20 (INTA) → GPIO48, pin 19 (INTB) → GPIO21
#define MCP_INTA 48
#define MCP_INTB 21

// Backlights (LEDC PWM, 5 kHz, 12-bit)
#define BUTTON_BL_PIN   4    // LED1_PWM — keypad button backlight
#define LED1_PWM        BUTTON_BL_PIN
#define BL_PWM_FREQ_HZ  5000
#define BL_PWM_BITS     12
#define BL_PWM_MAX      ((1 << BL_PWM_BITS) - 1)   // 4095 = 100%
#define LCD_BL_PIN      9    // SPI1_BL — display backlight. Deliberately NOT
                             // TFT_BL: TFT_eSPI's ST7789 init would switch
                             // it on during tft.init() (white flash)

// DAC control
#define DAC_MUTE        5    // → PCM5102A XSMT, HIGH = unmute, LOW = mute
#define DAC_MUTE_LEVEL  HIGH // normal (unmuted) level; pulsed LOW for 10 ms
                             // around I2S source changes (setI2SSourceMuted)
#define I2S_SWITCH      8    // TMUX1574 select
                             //   LOW  = I2S1, this ESP32-S3 (MP3 player)
                             //   HIGH = I2S2, BT ESP32
#define I2S_SWITCH_MP3  LOW  // Radio / AUX / MP3 inputs, and at boot
#define I2S_SWITCH_BT   HIGH // Bluetooth input only

// ---------------------------------------------------------------------------
// MCP23017 (I2C 0x20, A0=A1=A2=GND)
// Pin numbers: GPA0–GPA7 = 0–7, GPB0–GPB7 = 8–15
// ---------------------------------------------------------------------------

#define MCP23017_ADDR 0x20

// Port A — keypad (2 columns × 4 rows, diodes anode-to-column) + buttons
#define KEYPAD_ROW_3 0  // GPA0  output, scanned LOW
#define KEYPAD_ROW_2 1  // GPA1
#define KEYPAD_ROW_1 2  // GPA2
#define KEYPAD_ROW_0 3  // GPA3
#define BTN_EJECT    4  // GPA4  input pullup, active LOW (wired to GND)
#define KEYPAD_COL_1 5  // GPA5  input pullup
#define KEYPAD_COL_0 6  // GPA6  input pullup
#define BTN_PIN      7  // GPA7  input pullup, active LOW
                        // NOTE: datasheet Rev D (2022) lists GPA7/GPB7 as
                        // output-only. Works here; move PIN if I2C glitches.

// Port B — audio relays, headunit control outputs, headunit sense inputs
#define DAC_RELAY_PIN      8   // GPB0 out  LOW = DAC, HIGH = AUX-IN
#define RADIO_RELAY_PIN    9   // GPB1 out  LOW = radio, HIGH = DAC/AUX path
#define PIN_AMP_MUTE      10   // GPB2 out  HIGH = amp muted (during relay switching)
#define PIN_MODULE_ACTIVE 11   // GPB3 out  LOW on Radio input, HIGH on any other input
// Headunit sense inputs — REVERSE LOGIC. The PCBA input stage inverts these
// signals: a HIGH read by the ESP32 (via the MCP23017) is a LOW signal from
// the headunit, and a LOW read is a HIGH signal from the headunit.
// All firmware logic and all "HIGH"/"LOW" in comments and on screen refer to
// the level READ by the ESP32, not the headunit's signal level.
//                                              read   headunit
#define PIN_ILLUM_SENSE   12   // GPB4 in, pullup  LOW  = HIGH (dash illumination on)
#define PIN_RADIO_SENSE   13   // GPB5 in, pullup  read LOW for 100 ms → Radio input
#define PIN_HU_LIGHTSW    14   // GPB6 in, pullup  HIGH = LOW

// ---------------------------------------------------------------------------
// Display window — the usable 320×118 area of the 170×320 panel
// (rotation 3, landscape), starting DISP_PAD_H pixels down
// ---------------------------------------------------------------------------

#define DISP_W      320
#define DISP_H      118
#define DISP_PAD_H  26
#define SIDE_MARGIN 5
