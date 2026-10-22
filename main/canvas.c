#include <math.h>
#include "canvas.h"

uint16_t canvas_get(const canvas_t *c, int x, int y)
{
    if (x < 0 || x >= CANVAS_W || y < c->y0 || y >= c->y0 + c->h) return 0;
    return c->px[(y - c->y0) * CANVAS_W + x];
}

void canvas_fill(canvas_t *c, int x, int y, int w, int h, uint16_t color)
{
    int x0 = x < 0 ? 0 : x;
    int x1 = x + w > CANVAS_W ? CANVAS_W : x + w;
    int y0 = y < c->y0 ? c->y0 : y;
    int y1 = y + h > c->y0 + c->h ? c->y0 + c->h : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = &c->px[(yy - c->y0) * CANVAS_W];
        for (int xx = x0; xx < x1; xx++) row[xx] = color;
    }
}

void canvas_image_be(canvas_t *c, int x, int y, int w, int h, const uint8_t *be565)
{
    int x0 = x < 0 ? 0 : x;
    int x1 = x + w > CANVAS_W ? CANVAS_W : x + w;
    int y0 = y < c->y0 ? c->y0 : y;
    int y1 = y + h > c->y0 + c->h ? c->y0 + c->h : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = &c->px[(yy - c->y0) * CANVAS_W];
        const uint8_t *src = &be565[((yy - y) * w + (x0 - x)) * 2];
        for (int xx = x0; xx < x1; xx++, src += 2) row[xx] = (uint16_t)((src[0] << 8) | src[1]);
    }
}

// fg over bg at coverage a (0-15), per RGB565 channel.
static uint16_t blend(uint16_t bg, uint16_t fg, int a)
{
    int r = (bg >> 11) + (((fg >> 11) - (bg >> 11)) * a + 7) / 15;
    int g = ((bg >> 5) & 0x3F) + ((((fg >> 5) & 0x3F) - ((bg >> 5) & 0x3F)) * a + 7) / 15;
    int b = (bg & 0x1F) + (((fg & 0x1F) - (bg & 0x1F)) * a + 7) / 15;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void canvas_ellipse(canvas_t *c, float cx, float cy, float rx, float ry, uint16_t color, int clip_y0,
                    int clip_y1)
{
    int y0 = (int)floorf(cy - ry);
    int y1 = (int)ceilf(cy + ry);
    int x0 = (int)floorf(cx - rx);
    int x1 = (int)ceilf(cx + rx);
    if (y0 < clip_y0) y0 = clip_y0;
    if (y0 < c->y0) y0 = c->y0;
    if (y1 > clip_y1) y1 = clip_y1;
    if (y1 > c->y0 + c->h) y1 = c->y0 + c->h;
    if (x0 < 0) x0 = 0;
    if (x1 > CANVAS_W) x1 = CANVAS_W;
    for (int y = y0; y < y1; y++) {
        uint16_t *row = &c->px[(y - c->y0) * CANVAS_W];
        float v = ((float)y + 0.5f - cy) / ry;
        for (int x = x0; x < x1; x++) {
            float u = ((float)x + 0.5f - cx) / rx;
            // Distance to the edge in px, roughly: f / |grad f| for f = u^2 + v^2 - 1.
            float f = u * u + v * v - 1;
            float g = 2 * sqrtf(u * u / (rx * rx) + v * v / (ry * ry));
            float cover = g > 0 ? 0.5f - f / g : 1;
            if (cover <= 0) continue;
            int a = cover >= 1 ? 15 : (int)(cover * 15 + 0.5f);
            if (a) row[x] = a == 15 ? color : blend(row[x], color, a);
        }
    }
}

static void draw_glyph(canvas_t *c, const font_t *font, const font_glyph_t *g, int pen_x, int y,
                       uint16_t color, int clip_x0, int clip_x1)
{
    const uint8_t *rows = &font->bitmap[g->offset];
    int stride = (g->w + 1) / 2;
    for (int r = 0; r < g->h; r++) {
        int py = y + g->y + r;
        if (py < c->y0 || py >= c->y0 + c->h) continue;
        uint16_t *line = &c->px[(py - c->y0) * CANVAS_W];
        for (int col = 0; col < g->w; col++) {
            int px = pen_x + g->x + col;
            if (px < clip_x0 || px >= clip_x1 || px < 0 || px >= CANVAS_W) continue;
            uint8_t byte = rows[r * stride + col / 2];
            int a = col % 2 ? byte & 0x0F : byte >> 4;
            if (a) line[px] = a == 15 ? color : blend(line[px], color, a);
        }
    }
}

int canvas_text_clipped(canvas_t *c, const font_t *font, int x, int y, const char *text, uint16_t color,
                        int clip_x0, int clip_x1)
{
    if (y + font->line_h <= c->y0 || y >= c->y0 + c->h)
        return font_text_width(font, text); // not in this band
    int start = x;
    uint32_t cp;
    while ((cp = utf8_next(&text)) != 0) {
        const font_glyph_t *g = font_glyph(font, cp);
        if (x + g->x + g->w > clip_x0 && x + g->x < clip_x1)
            draw_glyph(c, font, g, x, y, color, clip_x0, clip_x1);
        x += g->advance;
    }
    return x - start;
}

int canvas_text(canvas_t *c, const font_t *font, int x, int y, const char *text, uint16_t color)
{
    return canvas_text_clipped(c, font, x, y, text, color, 0, CANVAS_W);
}

void canvas_text_right(canvas_t *c, const font_t *font, int x_right, int y, const char *text, uint16_t color)
{
    canvas_text(c, font, x_right - font_text_width(font, text), y, text, color);
}

void canvas_text_center(canvas_t *c, const font_t *font, int y, const char *text, uint16_t color)
{
    canvas_text(c, font, (CANVAS_W - font_text_width(font, text)) / 2, y, text, color);
}
