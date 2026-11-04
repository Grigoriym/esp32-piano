/* The WS2812 strip (8 LEDs, data on TX2 = GPIO17) through the RMT. */
#pragma once

#include "esp_err.h"
#include "leds.h"

esp_err_t strip_init(void);

/* Sends the colours; returns at once (the RMT sends in the background, ~0.3 ms). */
void strip_show(const leds_t colours);
