#include "function_test.h"

namespace ftest {

// ---------------------------------------------------------------------------
// TEST 5 — I2S TONE
//
//   NOW PLAYING: LEFT ONLY 440 Hz                       2 s
//   LEFT CH:     440 Hz              PLAY = sequence / stop
//   RIGHT CH:    OFF                RPT = tone L / R / both
//   VOLUME:      -12 dBFS              TrackUp/Down +/-1 dB
//   AMPLITUDE:   8231 / 32767
//   ─────────────────────────────────────────────────────────
//   DAC MUTE:    HIGH                         unmuted  [FF]
//   AMP MUTE:    LOW                       not muted  [REV]
//
// START WITH THE AMP VOLUME LOW.
//
// The test starts STOPPED, volume -12 dBFS.
//   Play        — stopped: start the sequence
//                 (LEFT 440 Hz 2 s → RIGHT 660 Hz 2 s → BOTH 550 Hz 2 s →
//                 silence 1 s, repeating)
//                 playing (sequence or continuous): stop
//   RPT         — continuous 440 Hz tone; each press moves it
//                 LEFT → RIGHT → BOTH → LEFT …  (from stopped or the
//                 sequence it starts on LEFT)
//   TrackUp     — volume +1 dB   TrackDown — volume -1 dB  (-60 … 0 dBFS)
//
// Volume is in dBFS (dB relative to full scale). AMPLITUDE is the peak
// sample value of the sine wave: 32767 × 10^(dBFS/20), so 0 dBFS = 32767
// (full scale), -6 dBFS = 16422, -12 dBFS = 8231 (about the old fixed level
// of 8000). Changes take effect within ~0.1 s (the DMA buffer length).
// The sequence cycle / step is logged to Serial.
//
//   FF          — toggle DAC MUTE (GPIO5 → PCM5102A XSMT): HIGH = unmuted,
//                 LOW = muted. Starts HIGH.
//   REV         — toggle AMP MUTE (MCP23017 GPB2): HIGH = amp muted,
//                 LOW = not muted. Starts LOW.
//
// On entry the relays switch to the DAC path (Radio_RELAY HIGH, DAC_RELAY
// LOW) and the I2S switch / relay state is logged to Serial. On exit the
// tone stops, GPB0–3 go LOW (relays back to Radio, AMP MUTE LOW) and
// DAC MUTE returns HIGH. The bottom two rows are read back from the pins.
//
// Audio is generated in chunks from loop() so the keypad keeps working.
// Each call writes ~23 ms; the write blocks while the ~93 ms DMA buffer is
// full, which paces the loop and keeps the buffer topped up.
// ---------------------------------------------------------------------------

#define TONE_DIVIDER      5
#define TONE_SAMPLE_RATE  44100
#define TONE_FULL_SCALE   32767
#define TONE_CHUNK_FRAMES 1024          // ≈ 23 ms per toneLoop() call
#define TONE_DB_START     -12           // dBFS
#define TONE_DB_STEP      1             // dB per TrackUp / TrackDown press
#define TONE_DB_MIN       -60
#define TONE_DB_MAX       0
#define TONE_CONT_FREQ    440.0f

// Playback states
#define TONE_STOPPED      0
#define TONE_SEQUENCE     1
#define TONE_CONTINUOUS   2

struct ToneSegment {
  const char* name;
  float    freq;
  bool     left, right;
  uint32_t ms;
};

static const ToneSegment TONE_SEQ[] = {
  { "LEFT ONLY 440 Hz",  440.0f, true,  false, 2000 },
  { "RIGHT ONLY 660 Hz", 660.0f, false, true,  2000 },
  { "BOTH 550 Hz",       550.0f, true,  true,  2000 },
  { "SILENCE",             0.0f, false, false, 1000 },
};
static const int TONE_NUM_SEG = sizeof(TONE_SEQ) / sizeof(TONE_SEQ[0]);

// Continuous-tone channel choices, cycled by RPT
static const char* const TONE_CONT_NAMES[] = { "TONE LEFT 440 Hz", "TONE RIGHT 440 Hz", "TONE BOTH 440 Hz" };
static const bool TONE_CONT_LEFT[]  = { true,  false, true };
static const bool TONE_CONT_RIGHT[] = { false, true,  true };

static int16_t  toneBuf[TONE_CHUNK_FRAMES * 2];   // interleaved L, R
static int      toneState = TONE_STOPPED;
static int      toneDb = TONE_DB_START;           // volume, dBFS
static float    tonePhase;
static int      toneSeg;
static uint32_t toneFramesLeft;
static uint32_t toneCycle;
static int      toneContCh;                       // 0 = L, 1 = R, 2 = both
static int16_t  toneValueX;

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void toneRow(int row, const String& value, uint16_t color, const String& hint) {
  int16_t y = rowY(row, TONE_DIVIDER);
  tft.fillRect(toneValueX, y, DISP_W - toneValueX, ROW_H, COLOR_BG);
  drawValue(value, toneValueX, y, color, 0);
  if (hint.length()) drawHint(hint, y);
}

// Peak sample value for a dBFS level: 32767 × 10^(dB/20), rounded
static int16_t toneAmplitude(int db) {
  return (int16_t)lroundf(TONE_FULL_SCALE * powf(10.0f, db / 20.0f));
}

// Rows 0–2: what's playing. Row 1/2 hints carry the Play / RPT help.
static void toneDrawPlaying(const String& name, uint16_t nameColor, const String& nameHint,
                            float freq, bool left, bool right) {
  String hz = String((int)freq) + " Hz";
  bool l = left && freq > 0, r = right && freq > 0;
  toneRow(0, name, nameColor, nameHint);
  toneRow(1, l ? hz : "OFF", l ? COLOR_HIGH : COLOR_LOW, "PLAY = sequence / stop");
  toneRow(2, r ? hz : "OFF", r ? COLOR_HIGH : COLOR_LOW, "RPT = tone L / R / both");
}

static void toneDrawStopped() {
  toneDrawPlaying("STOPPED", COLOR_LOW, "", 0, false, false);
}

static void toneDrawSegment() {
  const ToneSegment& s = TONE_SEQ[toneSeg];
  toneDrawPlaying(s.name, s.freq > 0 ? COLOR_HIGH : COLOR_LOW, String(s.ms / 1000) + " s",
                  s.freq, s.left, s.right);
  Serial.printf("  Sequence cycle %lu, step %d/%d: %s\n", (unsigned long)toneCycle,
                toneSeg + 1, TONE_NUM_SEG, s.name);
}

static void toneDrawContinuous() {
  toneDrawPlaying(TONE_CONT_NAMES[toneContCh], COLOR_HIGH, "continuous",
                  TONE_CONT_FREQ, TONE_CONT_LEFT[toneContCh], TONE_CONT_RIGHT[toneContCh]);
  Serial.printf("  Continuous: %s\n", TONE_CONT_NAMES[toneContCh]);
}

// Rows 3–4: volume in dBFS and the resulting peak sample value
static void toneDrawVolume() {
  toneRow(3, String(toneDb) + " dBFS", toneDb == TONE_DB_MAX ? COLOR_LOW : COLOR_HIGH,
          "TrackUp/Down +/-1 dB");
  toneRow(4, String(toneAmplitude(toneDb)) + " / " + String(TONE_FULL_SCALE), COLOR_TEXT, "");
}

// Rows 5–6: DAC MUTE (GPIO5 → PCM5102A XSMT) and AMP MUTE (MCP23017 GPB2).
// Both levels are read back from the pins. HIGH green / LOW red, as in
// headunit_io_test; the grey hint says what the level means.
static void toneDrawMutes() {
  bool xsmt    = digitalRead(DAC_MUTE);                       // OUTPUT reads back its level
  bool ampMute = mcp.readGPIOB() & (1 << (PIN_AMP_MUTE - 8));

  int16_t y5 = rowY(5, TONE_DIVIDER), y6 = rowY(6, TONE_DIVIDER);
  drawLevel(xsmt, toneValueX, y5);
  drawHint(xsmt ? "unmuted  [FF]" : "MUTED  [FF]", y5, tft.textWidth("unmuted  [FF]", FONT));
  drawLevel(ampMute, toneValueX, y6);
  drawHint(ampMute ? "MUTED  [REV]" : "not muted  [REV]", y6, tft.textWidth("not muted  [REV]", FONT));
}

// Startup routing check — Serial only (no room on screen)
static void toneLogRouting() {
  bool sw    = digitalRead(I2S_SWITCH);
  uint8_t b  = mcp.readGPIOB();
  bool radio = b & (1 << (RADIO_RELAY_PIN - 8));
  bool dac   = b & (1 << (DAC_RELAY_PIN - 8));
  Serial.printf("  I2S switch GPIO8 = %s (%s), relays %s (RADIO=%s DAC=%s)\n",
                sw ? "HIGH" : "LOW", sw ? "I2S2/BT - WRONG" : "I2S1",
                (radio && !dac) ? "DAC path" : "NOT on DAC path",
                radio ? "H" : "L", dac ? "H" : "L");
}

static void toneToggleDacMute() {
  bool level = !digitalRead(DAC_MUTE);
  digitalWrite(DAC_MUTE, level ? HIGH : LOW);
  toneDrawMutes();
  Serial.printf("  DAC MUTE (XSMT GPIO5) = %s (%s)\n", level ? "HIGH" : "LOW",
                level ? "DAC unmuted" : "DAC muted");
}

static void toneToggleAmpMute() {
  bool level = !(mcp.readGPIOB() & (1 << (PIN_AMP_MUTE - 8)));
  mcp.digitalWrite(PIN_AMP_MUTE, level ? HIGH : LOW);
  toneDrawMutes();
  Serial.printf("  AMP MUTE (GPB2) = %s (%s)\n", level ? "HIGH" : "LOW",
                level ? "amp muted" : "amp not muted");
}

// ---------------------------------------------------------------------------
// Playback control
// ---------------------------------------------------------------------------

static void toneStartSegment(int seg) {
  toneSeg = seg;
  toneFramesLeft = (uint32_t)TONE_SAMPLE_RATE * TONE_SEQ[seg].ms / 1000;
  toneDrawSegment();
}

// Write ~116 ms of silence so the ~93 ms DMA buffer no longer holds tone
static void toneFlushSilence() {
  memset(toneBuf, 0, sizeof(toneBuf));
  for (int i = 0; i < 5; i++) mp3_i2s.write((uint8_t*)toneBuf, sizeof(toneBuf));
}

static void toneStartSequence() {
  toneState = TONE_SEQUENCE;
  tonePhase = 0.0f;
  toneCycle = 1;
  Serial.println("  Sequence STARTED");
  toneStartSegment(0);
}

static void toneStartContinuous(int ch) {
  if (toneState != TONE_CONTINUOUS) tonePhase = 0.0f;
  toneState  = TONE_CONTINUOUS;
  toneContCh = ch;
  toneDrawContinuous();
}

static void toneStop() {
  toneState = TONE_STOPPED;
  toneFlushSilence();
  toneDrawStopped();
  Serial.println("  Tone STOPPED");
}

static void toneSetVolume(int db) {
  int before = toneDb;
  toneDb = constrain(db, TONE_DB_MIN, TONE_DB_MAX);
  if (toneDb == before) {
    Serial.printf("  Volume already %d dBFS (limit)\n", toneDb);
    return;
  }
  toneDrawVolume();
  Serial.printf("  Volume %d dBFS (amplitude %d / %d)\n", toneDb, toneAmplitude(toneDb),
                TONE_FULL_SCALE);
}

// ---------------------------------------------------------------------------
// Mode interface
// ---------------------------------------------------------------------------

void toneEnter() {
  Serial.println("START WITH THE AMP VOLUME LOW.");
  Serial.println("Play = sequence / stop, RPT = 440 Hz tone L/R/both, TrackUp/Down = volume +/-1 dB");
  Serial.println("FF = DAC MUTE (XSMT) toggle, REV = AMP MUTE toggle");
  setRelays(HIGH, LOW);   // Radio_RELAY HIGH, DAC_RELAY LOW → DAC to output

  // Start states: DAC MUTE HIGH (unmuted), AMP MUTE LOW (not muted)
  digitalWrite(DAC_MUTE, HIGH);
  mcp.digitalWrite(PIN_AMP_MUTE, LOW);

  const char* labels[] = { "NOW PLAYING", "LEFT CH", "RIGHT CH", "VOLUME", "AMPLITUDE",
                           "DAC MUTE", "AMP MUTE" };
  toneValueX = labelColumn(labels, 7);
  for (int r = 0; r < 7; r++) {
    drawLabel((String(labels[r]) + ":").c_str(), SIDE_MARGIN, rowY(r, TONE_DIVIDER));
  }
  drawDivider(TONE_DIVIDER);
  toneDrawMutes();
  toneLogRouting();

  toneState  = TONE_STOPPED;      // start with the tone off
  toneDb     = TONE_DB_START;
  toneDrawStopped();
  toneDrawVolume();
}

void toneExit() {
  if (toneState != TONE_STOPPED) {
    toneState = TONE_STOPPED;
    toneFlushSilence();     // stop the tone before the relays move
  }
  headunitOutputsLow();     // relays back to Radio, AMP MUTE LOW
  digitalWrite(DAC_MUTE, HIGH);   // leave the DAC unmuted for the other tests
}

void toneLoop() {
  if (toneState == TONE_STOPPED) {
    delay(10);              // main loop skips its own delay in this test
    return;
  }

  float freq;
  bool left, right;
  uint32_t n = TONE_CHUNK_FRAMES;
  if (toneState == TONE_SEQUENCE) {
    const ToneSegment& s = TONE_SEQ[toneSeg];
    freq = s.freq; left = s.left; right = s.right;
    n = min<uint32_t>(toneFramesLeft, TONE_CHUNK_FRAMES);
  } else {
    freq = TONE_CONT_FREQ;
    left = TONE_CONT_LEFT[toneContCh];
    right = TONE_CONT_RIGHT[toneContCh];
  }

  const float amp  = toneAmplitude(toneDb);   // same value shown on screen
  const float step = 2.0f * PI * freq / TONE_SAMPLE_RATE;
  for (uint32_t i = 0; i < n; i++) {
    int16_t v = (freq > 0) ? (int16_t)(amp * sinf(tonePhase)) : 0;
    toneBuf[2 * i]     = left  ? v : 0;
    toneBuf[2 * i + 1] = right ? v : 0;
    tonePhase += step;
    if (tonePhase >= 2.0f * PI) tonePhase -= 2.0f * PI;
  }
  mp3_i2s.write((uint8_t*)toneBuf, n * 2 * sizeof(int16_t));   // blocks while buffer is full

  if (toneState == TONE_SEQUENCE) {
    toneFramesLeft -= n;
    if (toneFramesLeft == 0) {
      int next = (toneSeg + 1) % TONE_NUM_SEG;
      if (next == 0) toneCycle++;
      toneStartSegment(next);
    }
  }
}

void toneKey(byte r, byte c) {
  if (r == 1 && c == 0) {                     // Play
    if (toneState == TONE_STOPPED) toneStartSequence();
    else                           toneStop();
  } else if (r == 3 && c == 0) {              // RPT
    if (toneState == TONE_CONTINUOUS) toneStartContinuous((toneContCh + 1) % 3);
    else                              toneStartContinuous(0);
  } else if (r == 0 && c == 1) {              // TrackUp
    toneSetVolume(toneDb + TONE_DB_STEP);
  } else if (r == 1 && c == 1) {              // TrackDown
    toneSetVolume(toneDb - TONE_DB_STEP);
  } else if (r == 2 && c == 1) {              // FF
    toneToggleDacMute();
  } else if (r == 3 && c == 1) {              // REV
    toneToggleAmpMute();
  }
}

void tonePin() {}

}  // namespace ftest
