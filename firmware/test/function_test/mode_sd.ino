// ---------------------------------------------------------------------------
// TEST 4 — SD CARD (sd_card_test, results shown on the display)
//
//   SD CARD:   MOUNTED                     [Play] rescan
//   TYPE:      SDHC/SDXC                   card 30436 MB
//   USED:      1234 of 30420 MB
//   MP3 FILES: 12                              root only
//   WARNINGS:  0                         3 other, 1 dir
//   ─────────────────────────────────────────────────────
//   READ KB/s: 4M 410  10M 980  20M 1650
//   FIRST MP3: song_name.mp3
//
// On entry the test waits on a "Press PLAY to start" screen and doesn't touch
// the card. The first Play switches to the results screen and runs the scan
// (~2 s with the speed test); later Play presses rescan. Full directory
// listing and details go to Serial.
// Warnings = files the MP3 player would mis-handle: macOS "._" files ending
// in .mp3, mixed-case extensions (.Mp3), or more than MP3_MAX_FILES MP3s.
// Card is unmounted when leaving the test.
// ---------------------------------------------------------------------------

#define SD_DIVIDER        5
#define SD_MP3_MAX_FILES  100            // same as primary_esp32s3/config.h
#define SD_SPEED_BYTES    (256 * 1024)
#define SD_SPEED_CHUNK    4096
#define SD_MP3_OK_KBPS    60             // comfortable margin over 320 kbps MP3

static int16_t sdValueX;

static bool sdIsPlayerMp3(const String& name) {
  return name.endsWith(".mp3") || name.endsWith(".MP3");   // mirrors mp3player.ino
}

static bool sdMount(uint32_t hz) {
  SD.end();
  return SD.begin(SD_CS, sdSPI, hz);
}

// Value cell for a row, cleared to the right edge first
static void sdRow(int row, const String& value, uint16_t color, const String& hint) {
  int16_t y = rowY(row, SD_DIVIDER);
  tft.fillRect(sdValueX, y, DISP_W - sdValueX, ROW_H, COLOR_BG);
  drawValue(value, sdValueX, y, color, 0);
  if (hint.length()) drawHint(hint, y);
}

// Shorten text with ".." until it fits in maxW pixels
static String sdFit(String s, int16_t maxW) {
  if (tft.textWidth(s, FONT) <= maxW) return s;
  while (s.length() > 1 && tft.textWidth(s + "..", FONT) > maxW) s.remove(s.length() - 1);
  return s + "..";
}

static uint32_t sdSpeedTest(uint32_t hz, const String& path) {
  if (!sdMount(hz)) {
    Serial.printf("  %2lu MHz: mount FAILED\n", (unsigned long)(hz / 1000000));
    return 0;
  }
  File f = SD.open(path);
  if (!f) return 0;
  static uint8_t buf[SD_SPEED_CHUNK];
  uint32_t total = 0, t0 = millis();
  while (total < SD_SPEED_BYTES) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    total += n;
  }
  uint32_t ms = max<uint32_t>(1, millis() - t0);
  f.close();
  uint32_t kbps = (uint32_t)((total / 1024.0f) / (ms / 1000.0f));
  Serial.printf("  %2lu MHz: %lu bytes in %lu ms = %lu KB/s\n", (unsigned long)(hz / 1000000),
                (unsigned long)total, (unsigned long)ms, (unsigned long)kbps);
  return kbps;
}

static void sdRunScan() {
  // Clear old results
  for (int r = 0; r < MAX_ROWS; r++) {
    int16_t y = rowY(r, SD_DIVIDER);
    tft.fillRect(sdValueX, y, DISP_W - sdValueX, ROW_H, COLOR_BG);
  }
  sdRow(0, "SCANNING...", COLOR_TEXT, "");
  Serial.println("Mounting SD at 4 MHz (CS 42, MISO 41, MOSI 40, CLK 39)...");

  if (!sdMount(4000000)) {
    Serial.println("  SD.begin() failed - card inserted? FAT32? wiring? MISO pull-up?");
    sdRow(0, "NOT FOUND", COLOR_LOW, "[Play] retry");
    sdRow(1, "Card in? FAT32, not exFAT?", COLOR_TEXT, "");
    sdRow(2, "CS42 MISO41 MOSI40 CLK39", COLOR_TEXT, "");
    sdRow(3, "MISO pull-up? 3.3V/GND?", COLOR_TEXT, "");
    sdRow(4, "Try another card", COLOR_TEXT, "");
    sdRow(5, "-", COLOR_HINT, "");
    sdRow(6, "-", COLOR_HINT, "");
    return;
  }

  // Card info
  uint8_t type = SD.cardType();
  const char* typeName = type == CARD_MMC ? "MMC" : type == CARD_SD ? "SDSC" :
                         type == CARD_SDHC ? "SDHC/SDXC" : "UNKNOWN";
  uint32_t cardMB  = SD.cardSize()  / (1024ULL * 1024ULL);
  uint32_t totalMB = SD.totalBytes() / (1024ULL * 1024ULL);
  uint32_t usedMB  = SD.usedBytes()  / (1024ULL * 1024ULL);
  Serial.printf("  Type %s, card %lu MB, FS %lu MB, used %lu MB\n", typeName,
                (unsigned long)cardMB, (unsigned long)totalMB, (unsigned long)usedMB);

  sdRow(0, "MOUNTED", COLOR_HIGH, "[Play] rescan");
  sdRow(1, typeName, COLOR_TEXT, "card " + String(cardMB) + " MB");
  sdRow(2, String(usedMB) + " of " + String(totalMB) + " MB", COLOR_TEXT, "");

  // Root listing
  int mp3 = 0, other = 0, dirs = 0, warn = 0;
  String firstMp3 = "";
  Serial.println("  Root directory:");
  File root = SD.open("/");
  if (root) {
    while (File e = root.openNextFile()) {
      String name = String(e.name());
      String lower = name;
      lower.toLowerCase();
      if (e.isDirectory()) {
        Serial.printf("    [DIR ] %s/  (ignored - player only scans root)\n", name.c_str());
        dirs++;
      } else if (name.startsWith("._")) {
        Serial.printf("    [WARN] %s  macOS metadata file\n", name.c_str());
        warn++;
      } else if (sdIsPlayerMp3(name)) {
        mp3++;
        bool over = mp3 > SD_MP3_MAX_FILES;
        if (over) warn++;
        Serial.printf("    [MP3 ] %-40s %8lu B%s\n", name.c_str(), (unsigned long)e.size(),
                      over ? "  (over MP3_MAX_FILES - skipped)" : "");
        if (firstMp3.length() == 0) firstMp3 = "/" + name;
      } else if (lower.endsWith(".mp3")) {
        Serial.printf("    [WARN] %s  mixed-case .mp3 - player skips it\n", name.c_str());
        warn++;
      } else {
        Serial.printf("    [    ] %-40s %8lu B\n", name.c_str(), (unsigned long)e.size());
        other++;
      }
      e.close();
    }
    root.close();
  }
  Serial.printf("  %d MP3, %d other, %d folders, %d warnings\n", mp3, other, dirs, warn);

  sdRow(3, String(mp3), mp3 > 0 ? COLOR_HIGH : COLOR_LOW, "root only");
  sdRow(4, String(warn), warn == 0 ? COLOR_HIGH : COLOR_LOW,
        String(other) + " other, " + String(dirs) + " dir");

  if (firstMp3.length() == 0) {
    sdRow(5, "no MP3 to test", COLOR_LOW, "");
    sdRow(6, "copy .mp3 files to root", COLOR_LOW, "");
    return;
  }

  // Read speed
  sdRow(5, "testing...", COLOR_TEXT, "");
  Serial.printf("  Read speed on %s (first %d KB):\n", firstMp3.c_str(), SD_SPEED_BYTES / 1024);
  const uint32_t freqs[] = { 4000000, 10000000, 20000000 };
  String speeds = "";
  bool allOK = true;
  for (uint32_t hz : freqs) {
    uint32_t kbps = sdSpeedTest(hz, firstMp3);
    if (kbps < SD_MP3_OK_KBPS) allOK = false;
    speeds += String(hz / 1000000) + "M " + String(kbps) + "  ";
  }
  speeds.trim();
  sdRow(5, speeds, allOK ? COLOR_HIGH : COLOR_LOW, "");
  sdRow(6, sdFit(firstMp3.substring(1), DISP_W - SIDE_MARGIN - sdValueX), COLOR_TEXT, "");
}

static bool sdStarted = false;

// Waiting screen shown on entry, before Play is pressed
static void sdDrawWaiting() {
  const int NO_DIV = MAX_ROWS;   // no divider on this screen
  drawLabel("SD CARD TEST", SIDE_MARGIN, rowY(0, NO_DIV));
  drawHint("CS42 MISO41 MOSI40 CLK39", rowY(0, NO_DIV));
  drawValue("Press PLAY to start the test", SIDE_MARGIN, rowY(2, NO_DIV), COLOR_HIGH, 0);
  drawLabel("Mounts the card, lists the root folder", SIDE_MARGIN, rowY(4, NO_DIV));
  drawLabel("and measures read speed (~2 s).", SIDE_MARGIN, rowY(5, NO_DIV));
  drawHint("Insert a FAT32 card first", rowY(6, NO_DIV));
}

// Results screen (labels + divider); values are filled in by sdRunScan()
static void sdDrawResultsLayout() {
  tft.fillScreen(COLOR_BG);
  const char* labels[] = { "SD CARD", "TYPE", "USED", "MP3 FILES", "WARNINGS", "READ KB/s", "FIRST MP3" };
  sdValueX = labelColumn(labels, 7);
  for (int r = 0; r < 7; r++) {
    drawLabel((String(labels[r]) + ":").c_str(), SIDE_MARGIN, rowY(r, SD_DIVIDER));
  }
  drawDivider(SD_DIVIDER);
}

void sdEnter() {
  sdStarted = false;
  sdDrawWaiting();
  Serial.println("Press PLAY to start the SD card test.");
}

void sdExit() {
  SD.end();
}

void sdLoop() {}

void sdKey(byte r, byte c) {
  if (r != 1 || c != 0) return;        // Play only
  if (!sdStarted) {                    // first press: start the test
    sdStarted = true;
    sdDrawResultsLayout();
  }
  sdRunScan();                         // later presses: rescan
}

void sdPin() {}
