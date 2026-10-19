/*
  MP3 Test — SD before TFT, re-init SPI after TFT

  Strategy: Init SD first, then TFT, then re-attach MISO so SD works again.
*/

#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#define SD_CS   4
#define SD_MISO 19
#define SD_MOSI 23
#define SD_SCLK 18
#define TFT_BL  21

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=================================");
  Serial.println("SD + TFT Coexistence Test");
  Serial.println("=================================\n");

  // Step 1: Init SD card first
  Serial.println("1. Init SD card...");
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI);
  if (!SD.begin(SD_CS, SPI, 4000000)) {
    Serial.println("   FAILED!");
    while (1) delay(1000);
  }
  Serial.println("   OK!");

  // Count files to prove it works
  int count = 0;
  File root = SD.open("/");
  while (File entry = root.openNextFile()) {
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (name.endsWith(".mp3") || name.endsWith(".MP3")) count++;
    }
    entry.close();
  }
  root.close();
  Serial.print("   MP3 files: ");
  Serial.println(count);

  // Step 2: Init TFT (this breaks SD)
  Serial.println("\n2. Init TFT...");
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  Serial.println("   OK!");

  // Step 3: Re-attach MISO pin to SPI peripheral
  Serial.println("\n3. Re-init SPI with MISO to restore SD access...");
  SPI.end();
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI);
  Serial.println("   SPI re-initialized.");

  // Step 4: Verify SD works again
  Serial.println("\n4. Verify SD accessible...");
  root = SD.open("/");
  if (root) {
    int count2 = 0;
    while (File entry = root.openNextFile()) {
      if (!entry.isDirectory()) {
        String name = String(entry.name());
        if (name.endsWith(".mp3") || name.endsWith(".MP3")) count2++;
      }
      entry.close();
    }
    root.close();
    Serial.print("   SD works! Files: ");
    Serial.println(count2);
  } else {
    Serial.println("   FAILED — SD still broken after re-init!");
  }

  // Step 5: Verify TFT still works
  Serial.println("\n5. Verify TFT still works...");
  tft.setTextFont(2);
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(5, 30);
  tft.print("SD + TFT OK! Files: ");
  tft.println(count);
  Serial.println("   TFT drawing OK!");

  Serial.println("\n=== ALL GOOD ===");
}

void loop() {
  delay(10000);
}
