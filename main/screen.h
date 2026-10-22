#pragma once

#include "ui.h"

/* The TFT (display.h) showing ui.h's screen. Its own task redraws when the
 * state changes, so the key scan never waits for the screen. */

void screen_init(void);

/* The state to show; cheap, call it every scan. */
void screen_set(const ui_state_t *s);
