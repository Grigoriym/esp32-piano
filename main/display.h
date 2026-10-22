#pragma once

#include "esp_err.h"
#include "canvas.h"

// 1.69" 240x280 ST7789V2 IPS TFT on SPI (HSPI pins), backlight always on.

esp_err_t display_init(void);

// Fills one band of the screen (canvas.h). Called for every strip of a
// frame, top to bottom.
typedef void (*display_draw_fn)(canvas_t *c, void *ctx);

// Draws a frame strip by strip and sends only the strips whose pixels
// differ from what was sent last (all of them the first time).
esp_err_t display_frame(display_draw_fn draw, void *ctx);
