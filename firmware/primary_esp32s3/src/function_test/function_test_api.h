// ---------------------------------------------------------------------------
// function_test_api.h — the main firmware's view of the function test
//
// Entered from the settings menu ("Function Test"). The function test takes
// over the display, keypad, relays, LEDs, SD card and I2S. It exits by
// restarting the ESP32 (hold PIN for 1 s), so the main firmware comes back
// up in a clean state.
// ---------------------------------------------------------------------------
#pragma once
#include <stdint.h>

void ftestBegin();                           // take over the hardware, show test 1
void ftestLoop();                            // call from loop() while active
void ftestMatrixKey(uint8_t row, uint8_t col);
void ftestEject();                           // next test
void ftestPin();                             // PIN pressed
void ftestExit();                            // PIN held 1 s → restart
