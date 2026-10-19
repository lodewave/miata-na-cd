// ---------------------------------------------------------------------------
// Button Actions & Input Switching
//
// Application-level behaviour for button presses, plus the relay/input-state
// machine. Called by keypad.ino after debouncing — no hardware scanning here.
// (For the physical button-to-pin map, see keypad.ino.)
//
// Matrix buttons (active in Bluetooth and MP3 modes):
//   (0,1): TrackUp   — next track
//   (1,0): Play      — play/pause toggle
//   (1,1): TrackDown — previous track / restart
//   (2,0): Scan      — TODO
//   (2,1): FF        — TODO
//   (3,0): RPT       — TODO
//   (3,1): REV       — TODO
//
// Direct buttons:
//   PIN   — hold 1 s: open the settings menu; press in the menu: save & exit
//   EJECT — cycles input state (Radio → BT → MP3 → AUX → Radio…), skipping
//           BT / MP3 / AUX when they're disabled in the settings menu
//
// Headunit signals:
//   RADIO_SENSE LOW 100 ms → MODULE_ACTIVE LOW → 100 ms → Radio input
//                            (radioSenseLoop)
//   MODULE_ACTIVE         → LOW on Radio, HIGH on any other input
//   AMP_MUTE              → HIGH while relays switch (switchRelaysMuted)
//   DAC_MUTE (GPIO5)      → LOW 10 ms around I2S source changes
//
// While the settings menu or the function test is open, all buttons are
// routed there instead (see onKeyPress / onDirectButton / onPinLongPress).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Input State Switching
// ---------------------------------------------------------------------------

// Raw relay write: both relays, RELAY_SWITCH_DELAY apart, no amp muting.
// Used by the function test, which controls AMP_MUTE itself. The normal
// input switching uses switchRelaysMuted() below.
void setRelays(uint8_t radioRelay, uint8_t dacRelay) {
  if (!mcp_initialized) return;  // MCP23017 missing — nothing to drive
  mcp.digitalWrite(RADIO_RELAY_PIN, radioRelay);
  delay(RELAY_SWITCH_DELAY);
  mcp.digitalWrite(DAC_RELAY_PIN, dacRelay);
}

// Switch the input relays with the amp muted, to avoid pops:
//   1. AMP_MUTE HIGH and MODULE_ACTIVE to its new level — in ONE Port B
//      write, so they change at the same instant. Wait AMP_MUTE_BEFORE_MS
//      (100 ms)
//   2. switch only the relay(s) that change — RELAY_SWITCH_DELAY (20 ms)
//      between them if both change (Radio_RELAY first, then DAC_RELAY)
//   3. wait AMP_MUTE_AFTER_MS (200 ms), AMP_MUTE LOW
// If neither relay changes (e.g. BT ↔ MP3) there's no mute window;
// MODULE_ACTIVE is still set if it differs (in practice it never does —
// every change to or from Radio moves Radio_RELAY).
// Current pin states are read back from the MCP23017.
// Blocks for 300 ms (one relay) or 320 ms (both) when a relay switches.
void switchRelaysMuted(uint8_t radioRelay, uint8_t dacRelay, uint8_t moduleActive) {
  if (!mcp_initialized) return;

  const uint8_t radioBit  = 1 << (RADIO_RELAY_PIN   - 8);
  const uint8_t dacBit    = 1 << (DAC_RELAY_PIN     - 8);
  const uint8_t muteBit   = 1 << (PIN_AMP_MUTE      - 8);
  const uint8_t activeBit = 1 << (PIN_MODULE_ACTIVE - 8);

  uint8_t portB  = mcp.readGPIOB();
  bool radioMove = bool(portB & radioBit)  != (radioRelay   == HIGH);
  bool dacMove   = bool(portB & dacBit)    != (dacRelay     == HIGH);
  bool actMove   = bool(portB & activeBit) != (moduleActive == HIGH);

  if (!radioMove && !dacMove) {
    if (actMove) mcp.digitalWrite(PIN_MODULE_ACTIVE, moduleActive);
    return;
  }

  Serial.printf("RELAYS: AMP MUTE on, MODULE ACTIVE %s, switching%s%s\n",
                moduleActive ? "HIGH" : "LOW",
                radioMove ? (radioRelay ? " RADIO->HIGH" : " RADIO->LOW") : "",
                dacMove   ? (dacRelay   ? " DAC->HIGH"   : " DAC->LOW")   : "");

  // One write for both: bits for input pins (GPB4–6) are ignored by the
  // MCP23017, and the relay bits are written back unchanged.
  uint8_t out = (portB | muteBit) & ~activeBit;
  if (moduleActive == HIGH) out |= activeBit;
  mcp.writeGPIOB(out);
  delay(AMP_MUTE_BEFORE_MS);

  if (radioMove) mcp.digitalWrite(RADIO_RELAY_PIN, radioRelay);
  if (radioMove && dacMove) delay(RELAY_SWITCH_DELAY);
  if (dacMove)   mcp.digitalWrite(DAC_RELAY_PIN, dacRelay);

  delay(AMP_MUTE_AFTER_MS);
  mcp.digitalWrite(PIN_AMP_MUTE, LOW);
  Serial.println("RELAYS: AMP MUTE off");
}

// Change the TMUX1574 I2S source with the DAC muted:
//   DAC_MUTE LOW → 5 ms → I2S_SWITCH → 5 ms → DAC_MUTE HIGH
// Does nothing if I2S_SWITCH is already at that level.
void setI2SSourceMuted(uint8_t level) {
  if (digitalRead(I2S_SWITCH) == level) return;   // OUTPUT pins read back
  digitalWrite(DAC_MUTE, LOW);
  delay(DAC_MUTE_SWITCH_MS);
  digitalWrite(I2S_SWITCH, level);
  delay(DAC_MUTE_SWITCH_MS);
  digitalWrite(DAC_MUTE, DAC_MUTE_LEVEL);
  Serial.printf("I2S: source %s (DAC muted %d ms)\n",
                level == I2S_SWITCH_BT ? "I2S2 (BT)" : "I2S1 (MP3)",
                2 * DAC_MUTE_SWITCH_MS);
}

void switchInput(InputState newState) {
  InputState prevState = currentInput;
  currentInput = newState;
  bool changed = (prevState != currentInput);

  // --- Handle leaving previous state (before re-routing audio) ---

  // Leaving Bluetooth: AVRCP pause → phone. Leaving MP3: pause the player
  // (resumes from the same point).
  if (changed && prevState == INPUT_BT)  btSendPause();
  if (changed && prevState == INPUT_MP3) mp3Pause();

  // --- Route audio ---

  // TMUX1574 I2S source: I2S2 (BT ESP32) only for Bluetooth, otherwise
  // I2S1 (this ESP32-S3). Only matters on the DAC path (BT / MP3).
  // DAC muted for 10 ms around the change.
  setI2SSourceMuted(currentInput == INPUT_BT ? I2S_SWITCH_BT : I2S_SWITCH_MP3);

  // Relays (RADIO_RELAY, DAC_RELAY) with the amp muted if either changes.
  // MODULE_ACTIVE (LOW on Radio, HIGH otherwise) changes at the same moment
  // the amp is muted. Leaving Radio sets it HIGH, and the headunit then
  // releases RADIO_SENSE (HIGH), which re-arms radioSenseLoop().
  uint8_t moduleActive = (currentInput == INPUT_RADIO) ? LOW : HIGH;
  switch (currentInput) {
    case INPUT_RADIO: switchRelaysMuted(LOW,  LOW,  moduleActive); break;  // Radio
    case INPUT_BT:    switchRelaysMuted(HIGH, LOW,  moduleActive); break;  // DAC (Bluetooth)
    case INPUT_MP3:   switchRelaysMuted(HIGH, LOW,  moduleActive); break;  // DAC (MP3)
    case INPUT_AUX:   switchRelaysMuted(HIGH, HIGH, moduleActive); break;  // AUX-IN
  }

  // --- Handle entering new state ---

  // BT and MP3 show a splash first. Any other input ends a splash that was
  // still running, so it can't expire later and redraw over this screen.
  if (currentInput == INPUT_BT || currentInput == INPUT_MP3) startSplash();
  else                                                        splashActive = false;

  // Resume playback
  if (changed && currentInput == INPUT_BT && btConnected) btSendPlay();
  if (currentInput == INPUT_MP3) mp3Resume();

  updateDisplayForState();
}

// Radio is always available; BT, MP3 and AUX can be turned off in the
// settings menu.
bool inputEnabled(InputState s) {
  switch (s) {
    case INPUT_BT:  return settings.btEnable;
    case INPUT_AUX: return settings.auxEnable;
    case INPUT_MP3: return settings.mp3Enable;
    default:        return true;
  }
}

// EJECT: next enabled input (Radio → BT → MP3 → AUX → Radio, skipping
// disabled ones)
void cycleInput() {
  InputState next = currentInput;
  for (int i = 0; i < NUM_INPUT_STATES; i++) {
    next = (InputState)((next + 1) % NUM_INPUT_STATES);
    if (inputEnabled(next)) break;
  }
  switchInput(next);
}

// ---------------------------------------------------------------------------
// RADIO_SENSE (GPB5) → Radio input
//
// When RADIO_SENSE reads LOW continuously for RADIO_SENSE_HOLD_MS (100 ms):
//   1. MODULE_ACTIVE LOW straight away, so it doesn't keep the radio off
//   2. wait RADIO_SENSE_MODULE_MS (100 ms)
//   3. switch to the Radio input (relays with the amp muted; MODULE_ACTIVE
//      is already LOW, so switchRelaysMuted() leaves it)
// Fires once per LOW period: it re-arms only after RADIO_SENSE goes HIGH
// again, so you can still EJECT away from Radio while it stays LOW.
// Polled every 10 ms. (Reverse logic: a LOW read is a HIGH signal from the
// headunit — see board.h.)
// ---------------------------------------------------------------------------

void radioSenseLoop() {
  if (!mcp_initialized) return;

  static uint32_t lastPollMs = 0;
  static uint32_t lowSinceMs = 0;   // 0 = not LOW
  static bool     armed      = true;

  uint32_t now = millis();
  if (now - lastPollMs < RADIO_SENSE_POLL_MS) return;
  lastPollMs = now;

  bool low = !(mcp.readGPIOB() & (1 << (PIN_RADIO_SENSE - 8)));
  if (!low) {
    lowSinceMs = 0;
    armed = true;
    return;
  }
  if (lowSinceMs == 0) lowSinceMs = now ? now : 1;

  if (armed && (now - lowSinceMs) >= RADIO_SENSE_HOLD_MS) {
    armed = false;
    if (currentInput != INPUT_RADIO) {
      Serial.printf("RADIO SENSE: LOW for %d ms - MODULE ACTIVE LOW, Radio in %d ms\n",
                    RADIO_SENSE_HOLD_MS, RADIO_SENSE_MODULE_MS);
      mcp.digitalWrite(PIN_MODULE_ACTIVE, LOW);
      delay(RADIO_SENSE_MODULE_MS);
      switchInput(INPUT_RADIO);
    }
  }
}

// ---------------------------------------------------------------------------
// Splash Screen
// ---------------------------------------------------------------------------

void startSplash() {
  splashActive = true;
  splashEndMs  = millis() + SPLASH_DURATION_MS;
}

// Metadata that arrived during the splash is already in btArtist / btTitle
// (or mp3Artist / mp3Title), so the screen drawn here is up to date.
void checkSplashExpired() {
  if (!splashActive || (int32_t)(millis() - splashEndMs) < 0) return;   // wrap-safe
  splashActive = false;
  if (currentInput == INPUT_BT) btSendStatus();   // re-sync with the BT ESP32
  updateDisplayForState();
}

// ---------------------------------------------------------------------------
// Display for Input State
// ---------------------------------------------------------------------------

void updateDisplayForState() {
  switch (currentInput) {
    case INPUT_RADIO:
      showStaticDisplay("RADIO", "INPUT");
      break;
    case INPUT_BT:
      if (splashActive) {
        showStaticDisplay("BLUETOOTH", "INPUT");
      } else {
        showBluetoothDisplay();
      }
      break;
    case INPUT_AUX:
      showStaticDisplay("AUXILIARY", "INPUT");
      break;
    case INPUT_MP3:
      if (splashActive) {
        showStaticDisplay("MP3", "PLAYER");
      } else {
        showMP3Display();
      }
      break;
  }
}

// showStaticDisplay() lives in display.ino (renders via the sprite frame)

// The scroll lines are shared with the MP3 screen, so always reload them
// from the BT metadata here: otherwise the last MP3 artist/title would show
// (or scroll) on the Bluetooth screens.
void showBluetoothDisplay() {
  if (btConnected) {
    prepareScroll(artistLine, btArtist);
    prepareScroll(titleLine, btTitle);
    renderAll();
  } else {
    prepareScroll(artistLine, "");
    prepareScroll(titleLine, "");
    showSplash("BT WAIT");   // clears + draws + pushes in one go
  }
}

void showMP3Display() {
  // Show artist/title with same layout as Bluetooth
  prepareScroll(artistLine, mp3Artist);
  prepareScroll(titleLine, mp3Title);
  renderAll();
}

// ---------------------------------------------------------------------------
// Matrix Button Handler
// ---------------------------------------------------------------------------

void onKeyPress(byte row, byte col) {
  // Settings menu and function test take all matrix buttons
  if (uiMode == UI_MENU)          { menuKey(row, col);        return; }
  if (uiMode == UI_FUNCTION_TEST) { ftestMatrixKey(row, col); return; }

  // Bluetooth (AVRCP via the BT ESP32) and MP3 (local) share the layout.
  // Scan, FF, RPT and REV are not used yet.
  bool bt = (currentInput == INPUT_BT);
  if (!bt && currentInput != INPUT_MP3) return;   // Radio / AUX: no controls

  switch (keyId(row, col)) {
    case KEY_TRACK_UP:   bt ? btSendNext()      : mp3NextTrack();  break;
    case KEY_PLAY:       bt ? btSendPlayPause() : mp3PlayPause();  break;
    case KEY_TRACK_DOWN: bt ? btSendPrev()      : mp3PrevTrack();  break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
// Direct Button Handler
// ---------------------------------------------------------------------------

// UI mode when the current PIN press started. A long press only counts in
// the same mode — so the press that closes the menu can't reopen it when
// it reaches 1 s.
static UiMode pinPressMode = UI_NORMAL;

void onDirectButton(byte pin) {
  if (pin == BTN_PIN) {
    pinPressMode = uiMode;
    if (uiMode == UI_MENU)               menuClose();   // PIN press = save & exit
    else if (uiMode == UI_FUNCTION_TEST) ftestPin();
    // UI_NORMAL: a short press does nothing; holding 1 s opens the menu
  }
  else if (pin == BTN_EJECT) {
    if (uiMode == UI_NORMAL)             cycleInput();
    else if (uiMode == UI_FUNCTION_TEST) ftestEject();  // next test
    // UI_MENU: ignored
  }
}

// PIN held for PIN_LONG_PRESS_MS (keypad.ino)
void onPinLongPress() {
  if (uiMode != pinPressMode) return;
  if (uiMode == UI_NORMAL)             menuOpen();
  else if (uiMode == UI_FUNCTION_TEST) ftestExit();     // restarts the ESP32
}
