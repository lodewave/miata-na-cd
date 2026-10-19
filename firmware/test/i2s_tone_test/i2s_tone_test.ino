/*
  I2S Tone Test — new PCBA (primary ESP32-S3-WROOM-1-N8 → PCM5102A)

  Plays test tones through the same audio path the MP3 player uses, so the
  DAC, I2S switch, mute line and relays can be checked without an SD card.
  Serial 115200.

  START WITH THE AMP VOLUME LOW. Tones are at about -12 dBFS.

  Sequence (repeats):
    LEFT only   440 Hz  2 s
    RIGHT only  660 Hz  2 s
    BOTH        550 Hz  2 s
    silence            1 s

  Setup done before the tones start:
    GPIO8  I2S switch  = LOW   (this ESP32-S3 feeds the DAC)
    GPIO5  DAC XSMT    = HIGH  (unmuted)
    MCP23017 GPB1 Radio_RELAY = HIGH, GPB0 DAC_RELAY = LOW  (DAC → output)
      If the MCP23017 isn't found, the tones still play but the relays stay
      in the Radio position — probe the DAC output directly in that case.

  I2S (same library + settings as primary_esp32s3/mp3player.ino):
    GPIO6 BCK   GPIO15 LRCK   GPIO7 DATA   no MCLK   44.1 kHz 16-bit stereo

  Board: ESP32S3 Dev Module (8MB flash, PSRAM disabled)
*/

#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include "AudioTools.h"

// I2S → PCM5102A
#define I2S_BCK      6
#define I2S_WS       15
#define I2S_DATA_OUT 7

// DAC control
#define DAC_MUTE       5     // XSMT, HIGH = unmute
#define I2S_SWITCH     8     // LOW = primary ESP32-S3
#define I2S_SWITCH_MP3 LOW

// MCP23017 relays
#define I2C_SDA 1
#define I2C_SCL 2
#define MCP23017_ADDR   0x20
#define DAC_RELAY_PIN   8    // GPB0 — LOW = DAC (HIGH = AUX-IN)
#define RADIO_RELAY_PIN 9    // GPB1 — HIGH = DAC/AUX path (LOW = radio)
#define RELAY_SWITCH_DELAY 50

#define SAMPLE_RATE 44100
#define AMPLITUDE   8000     // ≈ -12 dBFS of 32767
#define FRAMES_PER_WRITE 256

I2SStream i2s;
Adafruit_MCP23X17 mcp;

struct Segment {
  const char* name;
  float freq;
  bool left;
  bool right;
  uint32_t ms;
};

const Segment SEQUENCE[] = {
  { "LEFT only  - 440 Hz", 440.0f, true,  false, 2000 },
  { "RIGHT only - 660 Hz", 660.0f, false, true,  2000 },
  { "BOTH       - 550 Hz", 550.0f, true,  true,  2000 },
  { "silence",               0.0f, false, false, 1000 },
};
const int NUM_SEGMENTS = sizeof(SEQUENCE) / sizeof(SEQUENCE[0]);

int16_t frameBuf[FRAMES_PER_WRITE * 2];  // interleaved L, R
float phase = 0.0f;

void setupRelays() {
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!mcp.begin_I2C(MCP23017_ADDR)) {
    Serial.println("  WARNING: MCP23017 not found - relays NOT switched (still on Radio).");
    return;
  }
  // Latch levels before enabling outputs, then switch in the same order as
  // actions.ino setRelays(): Radio_RELAY first, then DAC_RELAY.
  mcp.digitalWrite(RADIO_RELAY_PIN, LOW);
  mcp.digitalWrite(DAC_RELAY_PIN, LOW);
  mcp.pinMode(RADIO_RELAY_PIN, OUTPUT);
  mcp.pinMode(DAC_RELAY_PIN, OUTPUT);
  mcp.digitalWrite(RADIO_RELAY_PIN, HIGH);
  delay(RELAY_SWITCH_DELAY);
  mcp.digitalWrite(DAC_RELAY_PIN, LOW);
  Serial.println("  Relays: Radio_RELAY (GPB1) = HIGH, DAC_RELAY (GPB0) = LOW -> DAC to output");
}

void playSegment(const Segment& seg) {
  Serial.printf("  %s\n", seg.name);
  const float step = 2.0f * PI * seg.freq / SAMPLE_RATE;
  uint32_t framesLeft = (uint32_t)SAMPLE_RATE * seg.ms / 1000;

  while (framesLeft > 0) {
    uint32_t n = framesLeft < FRAMES_PER_WRITE ? framesLeft : FRAMES_PER_WRITE;
    for (uint32_t i = 0; i < n; i++) {
      int16_t s = (seg.freq > 0) ? (int16_t)(AMPLITUDE * sinf(phase)) : 0;
      frameBuf[2 * i]     = seg.left  ? s : 0;
      frameBuf[2 * i + 1] = seg.right ? s : 0;
      phase += step;
      if (phase >= 2.0f * PI) phase -= 2.0f * PI;
    }
    i2s.write((uint8_t*)frameBuf, n * 2 * sizeof(int16_t));  // blocks on DMA
    framesLeft -= n;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n\n=================================");
  Serial.println("I2S Tone Test (ESP32-S3 -> PCM5102A)");
  Serial.println("=================================");
  Serial.println("Start with the amp volume LOW.\n");

  // DAC control lines
  // DAC control lines. pinMode() MUST come before digitalWrite(): on core
  // 3.x, digitalWrite() on a pin not yet set up by pinMode() is ignored.
  pinMode(I2S_SWITCH, OUTPUT);
  digitalWrite(I2S_SWITCH, I2S_SWITCH_MP3);
  pinMode(DAC_MUTE, OUTPUT);
  digitalWrite(DAC_MUTE, HIGH);
  // Read back the actual pin levels (OUTPUT mode keeps the input enabled)
  Serial.printf("STEP 1: I2S switch GPIO%d = %s, DAC XSMT GPIO%d = %s\n",
                I2S_SWITCH, digitalRead(I2S_SWITCH) ? "HIGH" : "LOW",
                DAC_MUTE, digitalRead(DAC_MUTE) ? "HIGH (unmuted)" : "LOW (muted!)");

  Serial.println("\nSTEP 2: Relays to DAC path");
  setupRelays();

  Serial.printf("\nSTEP 3: I2S start (BCK=%d LRCK=%d DATA=%d, %d Hz 16-bit stereo)\n",
                I2S_BCK, I2S_WS, I2S_DATA_OUT, SAMPLE_RATE);
  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.pin_bck         = I2S_BCK;
  cfg.pin_ws          = I2S_WS;
  cfg.pin_data        = I2S_DATA_OUT;
  cfg.sample_rate     = SAMPLE_RATE;
  cfg.bits_per_sample = 16;
  cfg.channels        = 2;
  cfg.buffer_count    = 8;     // same as mp3player.ino
  cfg.buffer_size     = 2048;
  if (!i2s.begin(cfg)) {
    Serial.println("  ERROR: i2s.begin() failed. Halting.");
    while (true) delay(1000);
  }
  Serial.println("  OK");

  Serial.println("\nSTEP 4: Tone sequence (repeats)");
  Serial.println("  No sound at all  -> probe GPIO7/15/6 for clocks/data, check XSMT (GPIO5) is HIGH,");
  Serial.println("                      I2S switch (GPIO8) LOW, relays clicked, amp on.");
  Serial.println("  Left/right swapped -> check the DAC output wiring to the relays.\n");
}

void loop() {
  static uint32_t cycle = 0;
  Serial.printf("Cycle %lu\n", (unsigned long)++cycle);
  for (int i = 0; i < NUM_SEGMENTS; i++) {
    playSegment(SEQUENCE[i]);
  }
}
