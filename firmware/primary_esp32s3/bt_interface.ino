// ---------------------------------------------------------------------------
// Bluetooth Interface — UART Communication with BT ESP32
//
// The secondary ESP32-WROOM handles Bluetooth A2DP audio reception and I2S
// output to the shared DAC. This file manages the UART link between the
// primary ESP32-S3 and the BT ESP32.
//
// UART Protocol (text-based, 115200 8N1):
//
//   Commands sent (Primary → BT ESP32):
//     "BT_PLAY\n"      — resume playback (AVRCP play)
//     "BT_PAUSE\n"     — pause playback (AVRCP pause)
//     "BT_PLAYPAUSE\n" — toggle play/pause (AVRCP)
//     "BT_NEXT\n"      — next track (AVRCP forward)
//     "BT_PREV\n"      — previous track (AVRCP backward)
//     "BT_STATUS\n"    — request connection status + current metadata
//     "BT_CONFIG:e,a\n" — BT Enable (e) and BT Auto-Reconnect (a), 0/1.
//                        Sent at boot, on BT_READY and when the settings
//                        menu closes. The BT ESP32 stores them in its own
//                        NVS (so they apply from its power-up) and ignores
//                        the message if nothing changed.
//
//   Responses received (BT ESP32 → Primary):
//     "BT_ARTIST:name\n"   — current artist from AVRC metadata
//     "BT_TITLE:name\n"    — current title from AVRC metadata
//     "BT_CONNECTED\n"     — phone/source connected
//     "BT_DISCONNECTED\n"  — phone/source disconnected
//     "BT_PLAYING\n"       — audio stream started (logged only)
//     "BT_PAUSED\n"        — audio stream suspended (logged only)
//     "BT_READY\n"         — BT ESP32 booted (power-up, or restart after a
//                            BT Enable change); nothing connected yet
//
// btArtist / btTitle / btConnected are the only BT state. The Bluetooth
// screens are always drawn from them by showBluetoothDisplay() (actions.ino),
// so metadata that arrives during the splash is simply picked up when the
// splash ends.
//
// Hardware (BT ESP32 = ESP32-WROOM-32E):
//   Primary ESP32-S3 GPIO18 (TX) → BT ESP32 GPIO16 (RX)
//   Primary ESP32-S3 GPIO17 (RX) ← BT ESP32 GPIO17 (TX)
//   Shared GND
//
// Audio: the BT ESP32 drives I2S2 into the TMUX1574; I2S_SWITCH (GPIO8)
// HIGH selects it while the Bluetooth input is active (see switchInput()).
// ---------------------------------------------------------------------------

#include <HardwareSerial.h>

#define BT_RX_LINE_MAX 512   // metadata lines; anything longer is cut

// UART1 for communication with BT ESP32
static HardwareSerial btSerial(1);
static String btRxBuffer = "";

// Current BT state for display
String btArtist = "";
String btTitle  = "";
bool btConnected = false;

// Set while processing UART lines; one redraw after the batch (btLoop)
static bool btDisplayDirty = false;

// ---------------------------------------------------------------------------
// UART Communication
// ---------------------------------------------------------------------------

static void btSendCommand(const char* cmd) {
  btSerial.println(cmd);
  Serial.printf("BT TX: %s\n", cmd);
}

// Phone gone (disconnect, or the BT ESP32 rebooted): clear its state
static void btClearConnection() {
  btConnected = false;
  btArtist = "";
  btTitle  = "";
  btDisplayDirty = true;
}

static void btProcessLine(const String &line) {
  Serial.println("BT RX: " + line);

  if (line.startsWith("BT_TITLE:")) {
    btTitle = line.substring(9);
    btDisplayDirty = true;
  }
  else if (line.startsWith("BT_ARTIST:")) {
    btArtist = line.substring(10);
    btDisplayDirty = true;
  }
  else if (line == "BT_CONNECTED") {
    btConnected = true;
    btDisplayDirty = true;        // "BT WAIT" → ARTIST / TITLE
  }
  else if (line == "BT_DISCONNECTED") {
    btClearConnection();
  }
  else if (line == "BT_READY") {
    Serial.println("BT: Secondary ESP32 ready");
    if (btConnected) btClearConnection();   // fresh boot: no phone yet
    btSendConfig();               // keep its copy of the BT settings in sync
  }
  // BT_PLAYING / BT_PAUSED: logged above, nothing to do
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void initBTInterface() {
  Serial.println("BT: Initializing UART1 to BT ESP32...");
  btSerial.begin(BT_UART_BAUD, SERIAL_8N1, BT_UART_RX, BT_UART_TX);
  Serial.printf("BT: UART1 ready (TX=GPIO%d, RX=GPIO%d)\n", BT_UART_TX, BT_UART_RX);

  // The BT ESP32 usually boots faster and its BT_READY is sent before this
  // UART is open, so send the settings now as well (BT_READY sends them
  // again if it comes later).
  btSendConfig();
}

void btLoop() {
  // Read incoming data from BT ESP32
  while (btSerial.available()) {
    char c = btSerial.read();
    if (c == '\n') {
      btRxBuffer.trim();
      if (btRxBuffer.length() > 0) btProcessLine(btRxBuffer);
      btRxBuffer = "";
    } else if (c != '\r' && btRxBuffer.length() < BT_RX_LINE_MAX) {
      btRxBuffer += c;
    }
  }

  // Single render after processing all pending UART data. During the splash
  // nothing is drawn; checkSplashExpired() shows the current state after it.
  if (btDisplayDirty) {
    btDisplayDirty = false;
    if (currentInput == INPUT_BT && !splashActive) {
      showBluetoothDisplay();    // actions.ino — ARTIST/TITLE or "BT WAIT"
    }
  }
}

void btSendPlay()      { btSendCommand("BT_PLAY"); }
void btSendPause()     { btSendCommand("BT_PAUSE"); }
void btSendNext()      { btSendCommand("BT_NEXT"); }
void btSendPrev()      { btSendCommand("BT_PREV"); }
void btSendPlayPause() { btSendCommand("BT_PLAYPAUSE"); }
void btSendStatus()    { btSendCommand("BT_STATUS"); }

void btSendConfig() {
  char cmd[16];
  snprintf(cmd, sizeof(cmd), "BT_CONFIG:%d,%d",
           settings.btEnable ? 1 : 0, settings.btAutoReconnect ? 1 : 0);
  btSendCommand(cmd);
}
