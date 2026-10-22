#pragma once

// A band of rows of the 240x280 RGB565 screen. The TFT is drawn strip by
// strip (a full frame is 134 KB, too much to keep), so everything that
// draws takes screen coordinates and skips what falls outside the band.
// No ESP-IDF dependencies (host-testable; the tests use one band as tall
// as the screen).

#include <stdint.h>
#include "font.h"

#define CANVAS_W 240
#define CANVAS_H 280

// RGB565 from 8-bit channels.
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

typedef struct {
    uint16_t *px; // CANVAS_W * h pixels, native byte order
    int y0;       // screen row of px[0]
    int h;
} canvas_t;

// 0 outside the band.
uint16_t canvas_get(const canvas_t *c, int x, int y);

// Out-of-band and off-screen pixels are ignored by all drawing below.
void canvas_fill(canvas_t *c, int x, int y, int w, int h, uint16_t color);

// A w x h image of big-endian RGB565 pixels (as they come over the wire)
// with its top-left at (x, y).
void canvas_image_be(canvas_t *c, int x, int y, int w, int h, const uint8_t *be565);

// A filled, anti-aliased ellipse centred at (cx, cy) (sub-pixel, so it can
// move smoothly), drawn only in rows [clip_y0, clip_y1).
void canvas_ellipse(canvas_t *c, float cx, float cy, float rx, float ry, uint16_t color, int clip_y0,
                    int clip_y1);

// Draws UTF-8 text with the top of its line at y and its pen starting at
// x, blended over what's there, only in columns [clip_x0, clip_x1) (for
// scrolling text within a box). Returns the width of the whole text in px.
int canvas_text_clipped(canvas_t *c, const font_t *font, int x, int y, const char *text, uint16_t color,
                        int clip_x0, int clip_x1);

// canvas_text_clipped() over the full width.
int canvas_text(canvas_t *c, const font_t *font, int x, int y, const char *text, uint16_t color);

// Right edge of the text at x_right (exclusive).
void canvas_text_right(canvas_t *c, const font_t *font, int x_right, int y, const char *text, uint16_t color);

// Centered horizontally on the screen.
void canvas_text_center(canvas_t *c, const font_t *font, int y, const char *text, uint16_t color);
