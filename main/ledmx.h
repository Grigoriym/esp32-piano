/*
 * The 8x8 LED matrix (common anode) on two chained 74HC595s (docs/matrix-check-wiring.html):
 * DS on D25 into chip A (the columns, low = on), its Q7' into chip B (the rows through
 * 220R, high = on), SH_CP on D26, ST_CP on D27. Checked 2026-11-07 (esp32-hw-checks
 * ONLY_CHECK 3). A 1 kHz timer shows one column at a time.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t ledmx_init(void);

/* The frame: bit r of cols[c] = row r (0 = top), column c (0 = left). */
void ledmx_show(const uint8_t cols[8]);
