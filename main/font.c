#include <stddef.h>
#include "font.h"

uint32_t utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    if (!s || !*s) return 0;

    int extra;
    uint32_t cp;
    if (s[0] < 0x80) {
        extra = 0;
        cp = s[0];
    } else if ((s[0] & 0xE0) == 0xC0) {
        extra = 1;
        cp = s[0] & 0x1F;
    } else if ((s[0] & 0xF0) == 0xE0) {
        extra = 2;
        cp = s[0] & 0x0F;
    } else if ((s[0] & 0xF8) == 0xF0) {
        extra = 3;
        cp = s[0] & 0x07;
    } else {
        *p += 1; // stray continuation byte or invalid lead
        return FONT_MISSING;
    }
    for (int i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) { // truncated sequence (also stops at the NUL)
            *p += i;
            return FONT_MISSING;
        }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *p += 1 + extra;
    return cp;
}

static const font_glyph_t *find(const font_t *font, uint32_t cp)
{
    int lo = 0;
    int hi = font->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (font->glyphs[mid].cp == cp) return &font->glyphs[mid];
        if (font->glyphs[mid].cp < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

const font_glyph_t *font_glyph(const font_t *font, uint32_t cp)
{
    const font_glyph_t *g = find(font, cp);
    return g ? g : find(font, FONT_MISSING); // FONT_MISSING is always in the table
}

int font_text_width(const font_t *font, const char *text)
{
    int w = 0;
    uint32_t cp;
    while ((cp = utf8_next(&text)) != 0) w += font_glyph(font, cp)->advance;
    return w;
}
