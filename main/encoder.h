#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* KY-040 knob on D18 (CLK) / D19 (DT) / D23 (SW), docs/wiring.html. */

typedef enum {
    KNOB_CW,  /* one detent clockwise */
    KNOB_CCW, /* one detent counter-clockwise */
    KNOB_PRESS,
} knob_event_t;

esp_err_t encoder_init(void);

/* The next event, without waiting. False: none (or encoder_init() failed). */
bool encoder_poll(knob_event_t *ev);
