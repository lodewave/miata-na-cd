// ---------------------------------------------------------------------------
// MP3 Player — Local SD Card Playback
//
// Handles SD card reading, MP3 decoding (via libhelix), and I2S audio output
// directly on the primary ESP32-S3. No external ESP32 needed for MP3.
//
// Features:
//   - Finds .mp3 files (any case) in the root and in folders up to
//     MP3_MAX_DEPTH levels deep, up to MP3_MAX_FILES (rules: mp3_files.h).
//     Hidden "._" files that macOS leaves on SD cards are skipped
//   - Shuffle playback across all folders, new order on every pass
//   - ID3v2 (2.2–2.4) and ID3v1 tag parsing (artist, title)
//   - Falls back to "SD CARD" / the filename if no tags
//   - Play/pause/next/prev controls
//   - Auto-advance to next track on completion (unreadable files are skipped)
//
// Hardware:
//   I2S1 (schematic name): BCK=6, LRCK=15, DATA=7 → TMUX1574 → PCM5102A
//               (no MCLK)
//   DAC mute:   GPIO5 → XSMT, HIGH = unmuted. Set HIGH here at boot;
//               setI2SSourceMuted() (actions.ino) pulses it LOW around
//               I2S source changes.
//   I2S switch: GPIO8 (TMUX1574 select). LOW = I2S1 (this ESP32-S3, MP3),
//               HIGH = I2S2 (BT ESP32). Set LOW here at boot; switchInput()
//               drives it HIGH only for the Bluetooth input.
//   SD Card:    SCLK=39, MOSI=40, MISO=41, CS=42 (SPI3/HSPI)
// ---------------------------------------------------------------------------

// SD Card SPI bus (separate from display SPI). Shared with the function test.
SPIClass sdSPI(HSPI);

// I2S1 output. Shared with the function test (tone test).
I2SStream mp3_i2s;

// ---------------------------------------------------------------------------
// MP3 volume — fixed linear gain between the decoder and I2S
//
// Gain = 10^(dBFS / 20) from the "MP3 Volume" setting (0.5 dB steps), held
// as a 16.16 fixed-point multiplier. Replaces AudioTools' VolumeStream, which
// rounds the volume to 0.01 — too coarse for 0.5 dB steps at low levels.
// Sample-rate changes from the decoder (e.g. a 48 kHz MP3) are passed on to
// I2S. Assumes 16-bit samples (the Helix decoder's output).
// ---------------------------------------------------------------------------

class Mp3GainOutput : public AudioOutput {
 public:
  explicit Mp3GainOutput(AudioStream& out) : out_(out) {}

  void setGain(float gain) { gainQ16_ = (int32_t)lroundf(gain * 65536.0f); }

  size_t write(const uint8_t* data, size_t len) override {
    size_t samples = len / 2;
    size_t done = 0;
    while (done < samples) {
      size_t n = samples - done;
      if (n > BUF_SAMPLES) n = BUF_SAMPLES;
      memcpy(buf_, data + done * 2, n * 2);        // data may be unaligned
      for (size_t i = 0; i < n; i++) {
        buf_[i] = (int16_t)(((int32_t)buf_[i] * gainQ16_) >> 16);
      }
      out_.write((const uint8_t*)buf_, n * 2);     // blocks while I2S is full
      done += n;
    }
    return len;
  }

  int availableForWrite() override { return out_.availableForWrite(); }

  void setAudioInfo(AudioInfo info) override {
    AudioOutput::setAudioInfo(info);
    out_.setAudioInfo(info);                       // e.g. 48 kHz MP3 → I2S
  }

 private:
  static constexpr size_t BUF_SAMPLES = 256;
  AudioStream& out_;
  int32_t gainQ16_ = 65536;                        // 1.0
  int16_t buf_[BUF_SAMPLES];
};

static Mp3GainOutput mp3Gain(mp3_i2s);

// Re-read the "MP3 Volume" setting (called by the settings menu)
void mp3ApplyVolume() {
  mp3Gain.setGain(mp3VolumeGain());
}

static EncodedAudioStream* mp3Decoder = nullptr;
static StreamCopy* mp3Copier = nullptr;
static File mp3File;

static String mp3Files[MP3_MAX_FILES];
static int mp3Count = 0;
static uint16_t shuffleOrder[MP3_MAX_FILES];
static int shuffleIdx = 0;

static bool mp3Playing_local = false;
static bool mp3Initialized = false;    // SD card OK and at least one MP3
static uint32_t trackStartMs = 0;
static bool mp3DisplayDirty = false;   // new track info; shown by mp3Loop()

// Current track metadata for display
String mp3Artist = "SD CARD";
String mp3Title  = "";

// ---------------------------------------------------------------------------
// I2S1
// ---------------------------------------------------------------------------

// Start I2S1 at 44.1 kHz / 16-bit stereo. Also used by the function test,
// which restarts it after an MP3 may have changed the rate.
// DMA buffer: 8 × 2048 bytes = 4096 stereo frames ≈ 93 ms of audio at
// 44.1 kHz (library default is 6 × 512 ≈ 17 ms). The main loop also scans
// the keypad over I2C and redraws the display, so it needs headroom to
// avoid dropouts. Costs 16 KB of RAM.
bool i2s1Begin() {
  auto cfg = mp3_i2s.defaultConfig(TX_MODE);
  cfg.pin_bck         = I2S_BCK;
  cfg.pin_ws          = I2S_WS;
  cfg.pin_data        = I2S_DATA_OUT;
  cfg.sample_rate     = 44100;
  cfg.bits_per_sample = 16;
  cfg.channels        = 2;
  cfg.buffer_count    = 8;
  cfg.buffer_size     = 2048;
  bool ok = mp3_i2s.begin(cfg);
  if (!ok) Serial.println("ERROR: I2S1 start failed");
  return ok;
}

// ---------------------------------------------------------------------------
// Shuffle
// ---------------------------------------------------------------------------

static void generateShuffleOrder() {
  for (int i = 0; i < mp3Count; i++) shuffleOrder[i] = i;
  for (int i = mp3Count - 1; i > 0; i--) {       // Fisher–Yates
    int j = random(0, i + 1);
    uint16_t tmp = shuffleOrder[i];
    shuffleOrder[i] = shuffleOrder[j];
    shuffleOrder[j] = tmp;
  }
}

// ---------------------------------------------------------------------------
// File Helpers
// ---------------------------------------------------------------------------

// File name without folders and extension: "/Album/01 Song.mp3" → "01 Song"
static String titleFromPath(const String &path) {
  String title = path.substring(path.lastIndexOf('/') + 1);
  int dotPos = title.lastIndexOf('.');
  if (dotPos > 0) title = title.substring(0, dotPos);
  return title;
}

// ---------------------------------------------------------------------------
// Card walk — the file rules in mp3_files.h
// ---------------------------------------------------------------------------

static bool endsWithMP3(const String &name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".mp3");
}

static bool isSkippedFolder(const String &name) {
  return name.startsWith(".") ||                           // .Trashes, .Spotlight-V100…
         name.equalsIgnoreCase("System Volume Information") ||
         name.equalsIgnoreCase("$RECYCLE.BIN");
}

// Each entry is closed before going into a folder, so at most depth + 1
// folders are open at once (SD_MAX_OPEN_FILES).
static bool walkFolder(const String &folder, int depth, Mp3WalkFn fn, void* ctx) {
  File dir = SD.open(folder);
  if (!dir || !dir.isDirectory()) return true;

  bool go = true;
  while (go) {
    File entry = dir.openNextFile();
    if (!entry) break;
    String   name  = entry.name();
    String   path  = entry.path();
    bool     isDir = entry.isDirectory();
    uint32_t size  = isDir ? 0 : entry.size();
    entry.close();

    if (isDir) {
      if (isSkippedFolder(name)) {
        go = fn(path, MP3_SKIPPED_FOLDER, 0, depth, ctx);
      } else if (depth >= MP3_MAX_DEPTH) {
        go = fn(path, MP3_TOO_DEEP, 0, depth, ctx);
      } else {
        go = fn(path, MP3_FOLDER, 0, depth, ctx) && walkFolder(path, depth + 1, fn, ctx);
      }
    } else if (endsWithMP3(name)) {
      go = fn(path, name.startsWith(".") ? MP3_SKIPPED_FILE : MP3_TRACK, size, depth, ctx);
    } else {
      go = fn(path, MP3_OTHER_FILE, size, depth, ctx);
    }
  }
  dir.close();
  return go;
}

void mp3WalkCard(Mp3WalkFn fn, void* ctx) {
  walkFolder("/", 0, fn, ctx);
}

// Player's walk callback: keep tracks until the list is full
static bool addTrack(const String &path, Mp3Entry kind, uint32_t, int, void*) {
  if (kind != MP3_TRACK) return true;
  if (mp3Count >= MP3_MAX_FILES) {
    Serial.printf("MP3: more than %d files - the rest are skipped\n", MP3_MAX_FILES);
    return false;
  }
  mp3Files[mp3Count++] = path;
  return true;
}

static void scanMP3Files() {
  mp3Count = 0;
  uint32_t t0 = millis();
  mp3WalkCard(addTrack, nullptr);
  Serial.printf("MP3: scan took %lu ms, free heap %lu\n",
                (unsigned long)(millis() - t0), (unsigned long)ESP.getFreeHeap());
}

// ---------------------------------------------------------------------------
// ID3 Tag Parser
//
// Reads artist and title. Tries ID3v2 first (at file start), then ID3v1
// (last 128 bytes). Moves the file position; openTrack() seeks back to 0.
// Text is kept as single bytes: ASCII / Latin-1 pass through, other UTF-16
// characters become '?' (the display font is ASCII only).
// ---------------------------------------------------------------------------

#define ID3_TEXT_MAX 255   // longer frames are cut

static uint32_t be32(const uint8_t* b)     { return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; }
static uint32_t be24(const uint8_t* b)     { return ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2]; }
static uint32_t syncsafe(const uint8_t* b) { return ((uint32_t)(b[0] & 0x7F) << 21) | ((uint32_t)(b[1] & 0x7F) << 14) |
                                                    ((uint32_t)(b[2] & 0x7F) << 7)  |  (b[3] & 0x7F); }

// ID3v2 text frame body → string. Encoding byte: 0 = ISO-8859-1, 1 = UTF-16
// with BOM, 2 = UTF-16BE, 3 = UTF-8. Stops at the first NUL.
static String decodeID3Text(uint8_t encoding, const uint8_t* buf, int len) {
  String out;
  if (encoding == 1 || encoding == 2) {
    bool bigEndian = (encoding == 2);
    int i = 0;
    if (len >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) { bigEndian = false; i = 2; }
    if (len >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) { bigEndian = true;  i = 2; }
    for (; i + 1 < len; i += 2) {
      uint16_t ch = bigEndian ? (buf[i] << 8 | buf[i + 1]) : (buf[i + 1] << 8 | buf[i]);
      if (ch == 0) break;
      out += (ch < 0x100) ? (char)ch : '?';
    }
  } else {
    for (int i = 0; i < len && buf[i] != 0; i++) out += (char)buf[i];
  }
  return out;
}

// Read a text frame body (file positioned just after the frame header)
static bool readTextFrame(File &f, uint32_t frameSize, String &out) {
  if (frameSize < 2) return false;
  int encoding = f.read();
  uint8_t buf[ID3_TEXT_MAX];
  int got = f.read(buf, min<uint32_t>(frameSize - 1, sizeof(buf)));
  if (encoding < 0 || got <= 0) return false;
  out = decodeID3Text((uint8_t)encoding, buf, got);
  return true;
}

static bool parseID3v2(File &f, String &artist, String &title) {
  uint8_t h[10];
  f.seek(0);
  if (f.read(h, 10) != 10 || memcmp(h, "ID3", 3) != 0) return false;

  uint8_t  version = h[3];                       // 2, 3 or 4
  uint32_t tagSize = syncsafe(h + 6);
  uint32_t tagEnd  = 10 + tagSize;
  bool     v22     = (version < 3);              // 3-byte IDs and sizes
  int      hdrLen  = v22 ? 6 : 10;
  int      idLen   = v22 ? 3 : 4;
  const char* titleId  = v22 ? "TT2" : "TIT2";
  const char* artistId = v22 ? "TP1" : "TPE1";

  uint32_t pos = 10;
  if (!v22 && (h[5] & 0x40)) {                   // extended header: skip it
    uint8_t e[4];
    if (f.read(e, 4) != 4) return false;
    pos += (version == 4) ? syncsafe(e) : be32(e) + 4;
  }

  bool foundArtist = false, foundTitle = false;
  while (pos + hdrLen <= tagEnd && !(foundArtist && foundTitle)) {
    uint8_t fh[10];
    f.seek(pos);
    if (f.read(fh, hdrLen) != hdrLen || fh[0] == 0) break;   // end, or padding

    uint32_t size = v22 ? be24(fh + 3) : (version == 4 ? syncsafe(fh + 4) : be32(fh + 4));
    if (size == 0 || size > tagSize) break;

    if      (memcmp(fh, titleId,  idLen) == 0) foundTitle  |= readTextFrame(f, size, title);
    else if (memcmp(fh, artistId, idLen) == 0) foundArtist |= readTextFrame(f, size, artist);
    pos += hdrLen + size;
  }
  return foundArtist || foundTitle;
}

// Fixed-length ID3v1 field, trailing spaces / NULs trimmed
static String readFixedString(File &f, int len) {
  char buf[31] = { 0 };
  if (len > 30) len = 30;
  f.read((uint8_t*)buf, len);
  int end = len - 1;
  while (end >= 0 && (buf[end] == ' ' || buf[end] == '\0')) buf[end--] = '\0';
  return String(buf);
}

static bool parseID3v1(File &f, String &artist, String &title) {
  if (f.size() < 128) return false;
  uint8_t tag[3];
  f.seek(f.size() - 128);
  if (f.read(tag, 3) != 3 || memcmp(tag, "TAG", 3) != 0) return false;
  title  = readFixedString(f, 30);               // Title, then Artist (30 each)
  artist = readFixedString(f, 30);
  return title.length() > 0 || artist.length() > 0;
}

// Artist / title for the display, with fallbacks for missing tags
static void readTrackInfo(File &f, const String &path, String &artist, String &title) {
  artist = "";
  title  = "";
  if (!parseID3v2(f, artist, title)) parseID3v1(f, artist, title);
  if (artist.length() == 0) artist = "SD CARD";
  if (title.length()  == 0) title  = titleFromPath(path);
}

// ---------------------------------------------------------------------------
// Track Playback
// ---------------------------------------------------------------------------

// Open a track, read its tags and reset the decoder. play=false loads it
// paused (used at boot so the display has artist/title before first play).
// If the file can't be opened while playing, the next mp3Loop() copy fails
// and moves on to the next track.
static void openTrack(int fileIdx, bool play) {
  if (mp3File) mp3File.close();

  const String &path = mp3Files[fileIdx];
  mp3File = SD.open(path);
  if (!mp3File) {
    Serial.println("MP3: Failed to open: " + path);
    return;
  }

  readTrackInfo(mp3File, path, mp3Artist, mp3Title);
  Serial.printf("MP3: %s: %s - %s\n", play ? "Playing" : "Loaded",
                mp3Artist.c_str(), mp3Title.c_str());

  mp3File.seek(0);                               // decoder starts at the top
  mp3Decoder->begin();
  mp3Copier->begin(*mp3Decoder, mp3File);

  trackStartMs     = millis();
  mp3Playing_local = play;
  mp3DisplayDirty  = true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Drive the DAC control lines to a known state. Called first in initMP3()
// so they are set even if the SD card is missing.
static void initAudioPins() {
  // pinMode() MUST come before digitalWrite(): on Arduino-ESP32 core 3.x,
  // digitalWrite() on a pin not yet set up by pinMode() is silently ignored
  // (it only logs "IO n is not set as GPIO"), so the pin would stay LOW.
  pinMode(I2S_SWITCH, OUTPUT);
  digitalWrite(I2S_SWITCH, I2S_SWITCH_MP3);
  pinMode(DAC_MUTE, OUTPUT);
  digitalWrite(DAC_MUTE, DAC_MUTE_LEVEL);
  // Read back the actual pin levels (OUTPUT mode keeps the input enabled)
  Serial.printf("MP3: I2S switch GPIO%d = %s (MP3 player), DAC XSMT GPIO%d = %s\n",
                I2S_SWITCH, digitalRead(I2S_SWITCH) ? "HIGH" : "LOW",
                DAC_MUTE, digitalRead(DAC_MUTE) ? "HIGH (unmuted)" : "LOW (muted)");
}

void initMP3() {
  initAudioPins();

  Serial.println("MP3: Initializing SD card...");

  // Initialize SD card on separate SPI bus (HSPI/SPI3; display is on FSPI).
  // SS = -1: the SD library drives CS (GPIO42) itself as a plain GPIO.
  // Passing SD_CS here would attach it to the SPI peripheral first, and
  // core 3.x would then have to detach it again when SD.begin() claims it.
  sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, -1);
  if (!SD.begin(SD_CS, sdSPI, 4000000, "/sd", SD_MAX_OPEN_FILES)) {
    Serial.println("MP3: SD card FAILED! MP3 playback unavailable.");
    return;
  }
  Serial.println("MP3: SD card OK");

  scanMP3Files();
  Serial.printf("MP3: Found %d files\n", mp3Count);
  if (mp3Count == 0) {
    Serial.println("MP3: No MP3 files found!");
    return;
  }

  // Shuffle seeded from the hardware RNG, so the order differs every boot
  randomSeed(esp_random());
  generateShuffleOrder();
  shuffleIdx = 0;

  Serial.println("MP3: Init I2S...");
  i2s1Begin();

  // Volume from the settings menu ("MP3 Volume", default -12.0 dBFS)
  mp3ApplyVolume();
  Serial.printf("MP3: Volume %.1f dBFS (gain %.3f, peak %d / 32767)\n",
                mp3VolumeDb(), mp3VolumeGain(), mp3VolumePeak());

  // Decoder and copier: file → decoder → mp3Gain → I2S
  mp3Decoder = new EncodedAudioStream(&mp3Gain, new MP3DecoderHelix());
  mp3Decoder->begin();
  mp3Copier = new StreamCopy(1024);

  // Load the first track (opens file, reads ID3 tags so the display has
  // artist/title) but don't start playing until MP3 input is selected
  mp3Initialized = true;
  openTrack(shuffleOrder[shuffleIdx], false);

  Serial.println("MP3: Ready!");
}

void mp3Loop() {
  // Feed the decoder; at the end of a track, on to the next one
  if (mp3Initialized && mp3Playing_local && !mp3Copier->copy()) mp3NextTrack();

  // New track info → redraw if the MP3 screen is showing (after the splash
  // showMP3Display() picks it up anyway)
  if (mp3DisplayDirty) {
    mp3DisplayDirty = false;
    if (currentInput == INPUT_MP3 && !splashActive) showMP3Display();   // actions.ino
  }
}

// Start or continue playback: reload the current track if nothing is open
// or it has played to the end, otherwise carry on where it paused
void mp3Resume() {
  if (!mp3Initialized) return;
  if (!mp3File || mp3File.available() == 0) {
    openTrack(shuffleOrder[shuffleIdx], true);
  } else {
    mp3Playing_local = true;
    trackStartMs = millis();
  }
}

void mp3Pause() {
  mp3Playing_local = false;
}

// Play button
void mp3PlayPause() {
  if (!mp3Initialized) return;
  if (mp3Playing_local) {
    mp3Pause();
    Serial.println("MP3: Paused");
  } else {
    mp3Resume();
    Serial.println("MP3: Resumed");
  }
}

void mp3NextTrack() {
  if (!mp3Initialized) return;
  if (++shuffleIdx >= mp3Count) {                // end of this pass: new order
    generateShuffleOrder();
    shuffleIdx = 0;
  }
  openTrack(shuffleOrder[shuffleIdx], true);
}

// Within TRACK_RESTART_THRESHOLD_MS of the start: previous track.
// Later: restart the current one.
void mp3PrevTrack() {
  if (!mp3Initialized) return;
  if (millis() - trackStartMs < TRACK_RESTART_THRESHOLD_MS) {
    if (--shuffleIdx < 0) shuffleIdx = mp3Count - 1;
  }
  openTrack(shuffleOrder[shuffleIdx], true);
}

bool mp3IsPlaying() {
  return mp3Playing_local;
}
