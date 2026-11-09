/*
 * The 32x8 MAX7219 dot matrix (four chained 8x8 modules, Hailege 4-in-1), standing up:
 * 8 across, 32 tall. DIN D25, CLK D26, CS D27 (bit-banged, as esp32-hw-checks' check 4
 * that passed), VCC on VIN. Shows fireworks.c in its own task on core 1 (the screen's
 * core; the key scan runs on core 0). The MAX7219s hold the picture: a frame is sent
 * only when it changed.
 *
 * Which way up: DOTS_IN_AT_TOP (the end with the IN header) and DOTS_MIRROR (left/right)
 * are guesses until the user has seen rockets rise (2026-11-09).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t dots_init(void);

/* A note on (MIDI note number), sounding at at_us (esp_timer time). Any task. */
void dots_note(uint8_t note, int64_t at_us);

/* Beat `beat` (0: the bar line) of `beats` a bar. Any task. */
void dots_beat(int beat, int beats);
