/*
  ST7789 Raw SPI Test — new PCBA (primary ESP32-S3-WROOM-1-N8)

  Does NOT use TFT_eSPI. Separates "wiring/panel problem" from
  "TFT_eSPI library problem" after display_test.ino hung in tft.init().

    PART A — Drive the ST7789 with the plain Arduino SPI library (FSPI/SPI2,
             10 MHz) and a minimal init sequence, then fill RED/GREEN/BLUE/
             WHITE/BLACK.
             Colours appear  -> wiring + panel are good; problem is TFT_eSPI.
             No colours      -> check MOSI/SCLK/CS/DC/RST wiring and panel.

    PART B — Repeat the exact register-level write TFT_eSPI uses on the S3
             (SPI_UPDATE then SPI_USR on the SPI2 registers), with timeouts.
             Reports which step would hang inside tft.init().

  Board: ESP32S3 Dev Module (8MB flash, PSRAM disabled). Serial 115200.

  Display pins (ESP32-S3 : PCBA net):
    GPIO9  : SPI1_BL     GPIO10 : SPI1_RST    GPIO11 : SPI1_DC
    GPIO12 : SPI1_CS     GPIO13 : SPI1_SCLK   GPIO14 : SPI1_MOSI
*/

#include <SPI.h>
#include "soc/spi_reg.h"
#include "soc/soc.h"

#define PIN_BL    9
#define PIN_RST  10
#define PIN_DC   11
#define PIN_CS   12
#define PIN_SCLK 13
#define PIN_MOSI 14

#define SPI_HZ 10000000  // conservative; TFT_eSPI uses 80 MHz

// ST7789 controller RAM is 240×320. Filling all of it covers the 170×320
// panel regardless of its column offset.
#define RAM_W 240
#define RAM_H 320

SPIClass dispSPI(FSPI);  // FSPI = SPI2 on ESP32-S3 (core 3.x)
SPISettings dispSettings(SPI_HZ, MSBFIRST, SPI_MODE0);

// ---------------------------------------------------------------------------
// Low-level helpers (Arduino SPI)
// ---------------------------------------------------------------------------

void writeCmd(uint8_t c) {
  digitalWrite(PIN_DC, LOW);
  digitalWrite(PIN_CS, LOW);
  dispSPI.transfer(c);
  digitalWrite(PIN_CS, HIGH);
}

void writeData(uint8_t d) {
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  dispSPI.transfer(d);
  digitalWrite(PIN_CS, HIGH);
}

void cmdWithData(uint8_t c, const uint8_t* data, size_t len) {
  writeCmd(c);
  for (size_t i = 0; i < len; i++) writeData(data[i]);
}

void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  uint8_t caset[] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
  uint8_t raset[] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
  cmdWithData(0x2A, caset, 4);  // CASET
  cmdWithData(0x2B, raset, 4);  // RASET
  writeCmd(0x2C);               // RAMWR
}

void fillRAM(uint16_t color, const char* name) {
  Serial.printf("  fill %s ...", name);
  uint32_t t0 = millis();
  dispSPI.beginTransaction(dispSettings);
  setWindow(0, 0, RAM_W - 1, RAM_H - 1);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  for (uint32_t i = 0; i < (uint32_t)RAM_W * RAM_H; i++) {
    dispSPI.transfer16(color);
  }
  digitalWrite(PIN_CS, HIGH);
  dispSPI.endTransaction();
  Serial.printf(" done (%lu ms)\n", (unsigned long)(millis() - t0));
  delay(1000);
}

void st7789Init() {
  dispSPI.beginTransaction(dispSettings);
  writeCmd(0x01); delay(150);              // SWRESET
  writeCmd(0x11); delay(120);              // SLPOUT
  uint8_t colmod = 0x55;                   // 16-bit RGB565
  cmdWithData(0x3A, &colmod, 1);
  uint8_t madctl = 0x00;                   // default orientation, RGB
  cmdWithData(0x36, &madctl, 1);
  writeCmd(0x21);                          // INVON (these panels need inversion)
  writeCmd(0x13);                          // NORON
  writeCmd(0x29); delay(20);               // DISPON
  dispSPI.endTransaction();
}

// ---------------------------------------------------------------------------
// PART B — TFT_eSPI-style register write with timeouts
// ---------------------------------------------------------------------------

#define REG(r) (*(volatile uint32_t*)(r))

bool waitClear(uint32_t reg, uint32_t mask, const char* what) {
  uint32_t t0 = micros();
  while (REG(reg) & mask) {
    if (micros() - t0 > 100000) {  // 100 ms — a byte should take < 1 µs
      Serial.printf("    TIMEOUT waiting for %s to clear (CMD_REG=0x%08lX)\n",
                    what, (unsigned long)REG(reg));
      return false;
    }
  }
  Serial.printf("    %s cleared after %lu us\n", what, (unsigned long)(micros() - t0));
  return true;
}

bool registerLevelTest(uint32_t hz) {
  const int port = 2;  // what TFT_eSPI uses with USE_FSPI_PORT
  SPISettings settings(hz, MSBFIRST, SPI_MODE0);
  Serial.printf("\n  --- %lu MHz ---\n", (unsigned long)(hz / 1000000));
  uint32_t cmdReg  = SPI_CMD_REG(port);
  uint32_t userReg = SPI_USER_REG(port);
  uint32_t dlenReg = SPI_MS_DLEN_REG(port);
  uint32_t w0Reg   = SPI_W0_REG(port);

  Serial.printf("  SPI2 base = 0x%08lX (expected 0x%08lX)\n",
                (unsigned long)REG_SPI_BASE(port), (unsigned long)DR_REG_SPI2_BASE);
  Serial.printf("  CMD_REG  @0x%08lX = 0x%08lX\n", (unsigned long)cmdReg,  (unsigned long)REG(cmdReg));
  Serial.printf("  USER_REG @0x%08lX = 0x%08lX\n", (unsigned long)userReg, (unsigned long)REG(userReg));

  dispSPI.beginTransaction(settings);
  digitalWrite(PIN_DC, LOW);   // command
  digitalWrite(PIN_CS, LOW);

  REG(userReg) = SPI_USR_MOSI;     // TFT_eSPI SET_BUS_WRITE_MODE
  REG(dlenReg) = 8 - 1;            // 8 bits
  REG(w0Reg)   = 0x00;             // NOP command — harmless

  Serial.println("  Setting SPI_UPDATE ...");
  REG(cmdReg) = SPI_UPDATE;
  bool okUpdate = waitClear(cmdReg, SPI_UPDATE, "SPI_UPDATE");

  bool okUsr = false;
  if (okUpdate) {
    Serial.println("  Setting SPI_USR (start transfer) ...");
    REG(cmdReg) = SPI_USR;
    okUsr = waitClear(cmdReg, SPI_USR, "SPI_USR");
  }

  digitalWrite(PIN_CS, HIGH);
  dispSPI.endTransaction();

  bool ok = okUpdate && okUsr;
  Serial.printf("  RESULT @ %lu MHz: %s\n", (unsigned long)(hz / 1000000),
                ok ? "OK" : "HANGS (this is where tft.init() gets stuck)");
  return ok;
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n\n=================================");
  Serial.println("ST7789 Raw SPI Test (no TFT_eSPI)");
  Serial.println("=================================\n");

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);
  digitalWrite(PIN_DC, HIGH);
  pinMode(PIN_RST, OUTPUT);

  Serial.println("Hardware reset (RST low 20 ms) ...");
  digitalWrite(PIN_RST, HIGH); delay(5);
  digitalWrite(PIN_RST, LOW);  delay(20);
  digitalWrite(PIN_RST, HIGH); delay(150);

  Serial.printf("dispSPI.begin(SCLK=%d, MISO=-1, MOSI=%d) on FSPI ...\n", PIN_SCLK, PIN_MOSI);
  Serial.flush();
  dispSPI.begin(PIN_SCLK, -1, PIN_MOSI, -1);
  Serial.println("  OK");

  Serial.println("\nPART A: ST7789 init via Arduino SPI ...");
  Serial.flush();
  st7789Init();
  Serial.println("  init sent");
  fillRAM(0xF800, "RED");
  fillRAM(0x07E0, "GREEN");
  fillRAM(0x001F, "BLUE");
  fillRAM(0xFFFF, "WHITE");
  fillRAM(0x0000, "BLACK");
  Serial.println("  Did you see RED, GREEN, BLUE, WHITE, BLACK on the panel?");

  Serial.println("\nPART B: TFT_eSPI-style register write test (clock sweep) ...");
  Serial.flush();
  const uint32_t freqs[] = { 10000000, 27000000, 40000000, 80000000 };
  for (uint32_t hz : freqs) {
    registerLevelTest(hz);
    Serial.flush();
  }

  Serial.println("\nDone. Looping colour fills every 5 s (Part A method).\n");
}

void loop() {
  delay(5000);
  fillRAM(0xF800, "RED");
  fillRAM(0x07E0, "GREEN");
  fillRAM(0x001F, "BLUE");
}
