/*
  SD Card Test — new PCBA (primary ESP32-S3-WROOM-1-N8)

  Checks the SD card reader before adding MP3 playback. Serial 115200.

    Step 1 — Mount the card at 4 MHz (same as primary_esp32s3/mp3player.ino)
    Step 2 — Card type, size, used space
    Step 3 — List the root directory, mark the files the MP3 player will
             pick up, and flag files it would mis-handle
    Step 4 — Read-speed test on the first MP3 at 4, 10 and 20 MHz
             (MP3 at 320 kbps needs ~40 KB/s; aim for well above that)

  Board: ESP32S3 Dev Module (8MB flash, PSRAM disabled)

  SD pins (ESP32-S3), SPI3 / HSPI:
    GPIO42 : CS     GPIO41 : MISO     GPIO40 : MOSI     GPIO39 : CLK

  Card must be FAT32 (or FAT16). exFAT cards (most >32 GB) won't mount.
  The MP3 player only scans the ROOT folder — subfolders are ignored.
*/

#include <SPI.h>
#include <SD.h>

#define SD_CS    42
#define SD_MISO  41
#define SD_MOSI  40
#define SD_SCLK  39

#define MP3_MAX_FILES 100          // same limit as primary_esp32s3/config.h
#define SPEED_TEST_BYTES (256 * 1024)
#define SPEED_TEST_CHUNK 4096

SPIClass sdSPI(HSPI);              // HSPI = SPI3 on ESP32-S3 (display uses FSPI)

String firstMp3 = "";

bool mountAt(uint32_t hz) {
  SD.end();
  // SS = -1: the SD library drives CS itself as a GPIO
  return SD.begin(SD_CS, sdSPI, hz);
}

bool isPlayerMp3(const String& name) {
  // Mirrors scanMP3Files() in mp3player.ino
  return name.endsWith(".mp3") || name.endsWith(".MP3");
}

void listRoot() {
  File root = SD.open("/");
  if (!root) {
    Serial.println("  ERROR: could not open root directory");
    return;
  }

  int mp3Count = 0, otherCount = 0, dirCount = 0, warnCount = 0;
  while (File entry = root.openNextFile()) {
    String name = String(entry.name());
    if (entry.isDirectory()) {
      Serial.printf("  [DIR ] %s/  (ignored - player only scans root)\n", name.c_str());
      dirCount++;
    } else if (name.startsWith("._")) {
      // macOS resource-fork files: end in .mp3 but aren't audio
      Serial.printf("  [WARN] %-40s %8lu B  macOS metadata file - player would try to play it\n",
                    name.c_str(), (unsigned long)entry.size());
      warnCount++;
      if (isPlayerMp3(name)) mp3Count++;
    } else if (isPlayerMp3(name)) {
      mp3Count++;
      Serial.printf("  [MP3 ] %-40s %8lu B%s\n", name.c_str(), (unsigned long)entry.size(),
                    mp3Count > MP3_MAX_FILES ? "  (over MP3_MAX_FILES - skipped)" : "");
      if (firstMp3.length() == 0) firstMp3 = "/" + name;
    } else {
      String lower = name;
      lower.toLowerCase();
      if (lower.endsWith(".mp3")) {
        Serial.printf("  [WARN] %-40s %8lu B  mixed-case .mp3 extension - player skips it\n",
                      name.c_str(), (unsigned long)entry.size());
        warnCount++;
      } else {
        Serial.printf("  [    ] %-40s %8lu B\n", name.c_str(), (unsigned long)entry.size());
        otherCount++;
      }
    }
    entry.close();
  }
  root.close();

  Serial.printf("\n  Summary: %d MP3 file(s) the player will load, %d other file(s), "
                "%d folder(s), %d warning(s)\n", mp3Count, otherCount, dirCount, warnCount);
  if (mp3Count == 0) {
    Serial.println("  -> No MP3s in the root folder. Copy some .mp3 files to the top level of the card.");
  }
}

void speedTest(uint32_t hz) {
  Serial.printf("\n  --- %lu MHz ---\n", (unsigned long)(hz / 1000000));
  if (!mountAt(hz)) {
    Serial.println("  mount FAILED at this speed");
    return;
  }
  File f = SD.open(firstMp3);
  if (!f) {
    Serial.printf("  could not open %s\n", firstMp3.c_str());
    return;
  }

  static uint8_t buf[SPEED_TEST_CHUNK];
  uint32_t total = 0;
  uint32_t t0 = millis();
  while (total < SPEED_TEST_BYTES) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    total += n;
  }
  uint32_t ms = millis() - t0;
  f.close();

  if (ms == 0) ms = 1;
  float kbps = (total / 1024.0f) / (ms / 1000.0f);
  Serial.printf("  read %lu bytes in %lu ms = %.0f KB/s  (%s)\n",
                (unsigned long)total, (unsigned long)ms, kbps,
                kbps > 200 ? "plenty for MP3" : (kbps > 60 ? "OK for MP3" : "marginal"));
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n\n=================================");
  Serial.println("SD Card Test (ESP32-S3, new PCBA)");
  Serial.println("=================================\n");

  // --- Step 1: mount ---
  Serial.printf("STEP 1: Mount SD on HSPI (CLK=%d MISO=%d MOSI=%d CS=%d) at 4 MHz ...\n",
                SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  Serial.flush();
  sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, -1);
  if (!mountAt(4000000)) {
    Serial.println("  ERROR: SD.begin() failed.");
    Serial.println("\nTroubleshooting:");
    Serial.println("  1. Is a card inserted? Is it FAT32 (not exFAT/NTFS)?");
    Serial.println("  2. Check wiring: CS=42, MISO=41, MOSI=40, CLK=39, 3.3V, GND");
    Serial.println("  3. MISO needs a pull-up (10k to 3.3V) on many bare card sockets");
    Serial.println("  4. Try another card - some cards don't support SPI mode well");
    Serial.println("\nHalting.");
    while (true) delay(1000);
  }
  Serial.println("  OK - card mounted");

  // --- Step 2: card info ---
  Serial.println("\nSTEP 2: Card info");
  uint8_t type = SD.cardType();
  const char* typeName = type == CARD_MMC ? "MMC" : type == CARD_SD ? "SDSC" :
                         type == CARD_SDHC ? "SDHC/SDXC" : "UNKNOWN";
  Serial.printf("  Type: %s\n", typeName);
  Serial.printf("  Card size:  %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
  Serial.printf("  FS total:   %llu MB\n", SD.totalBytes() / (1024ULL * 1024ULL));
  Serial.printf("  FS used:    %llu MB\n", SD.usedBytes() / (1024ULL * 1024ULL));

  // --- Step 3: list root ---
  Serial.println("\nSTEP 3: Root directory");
  listRoot();

  // --- Step 4: speed ---
  if (firstMp3.length() > 0) {
    Serial.printf("\nSTEP 4: Read speed on %s (first %d KB)\n", firstMp3.c_str(),
                  SPEED_TEST_BYTES / 1024);
    const uint32_t freqs[] = { 4000000, 10000000, 20000000 };
    for (uint32_t hz : freqs) speedTest(hz);
  } else {
    Serial.println("\nSTEP 4: skipped (no MP3 to read)");
  }

  SD.end();
  Serial.println("\nDone.");
}

void loop() {
  delay(1000);
}
