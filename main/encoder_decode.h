#pragma once

// Pure KY-040 decode logic, no ESP-IDF dependencies, so it also compiles on
// the PC for the unit tests in test/. encoder.c feeds it pin levels.

#include <stdbool.h>
#include <stdint.h>

// Rotation: pin state = (CLK << 1) | DT.
typedef struct {
    uint8_t state; // last pin state
    int steps;     // quadrature steps since the last detent
} enc_rotation_t;

// Starts decoding from the current pin state.
void enc_rotation_init(enc_rotation_t *r, uint8_t pin_state);

// Feeds a new pin state. Returns +1 (one detent clockwise), -1 (one detent
// counter-clockwise) or 0 (no complete detent yet, bounce, no change).
int enc_rotation_update(enc_rotation_t *r, uint8_t pin_state);

// Button: polled every poll_ms, active low.
typedef struct {
    int stable;   // debounced level
    int last_raw; // last polled level
    int same_ms;  // how long last_raw has held
} enc_button_t;

// Starts released (level 1).
void enc_button_init(enc_button_t *b);

// Feeds one poll. Returns true once per press, after the level has held low
// for debounce_ms.
bool enc_button_update(enc_button_t *b, int raw, int poll_ms, int debounce_ms);
