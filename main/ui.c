#include "ui.h"

#include <stdio.h>

#define INK    RGB(232, 234, 246)
#define MUTED  RGB(140, 148, 190)
#define LINE   RGB(47, 53, 88)
#define ACCENT RGB(245, 197, 24)

#define LEFT  18 /* the panel's corners are rounded: keep ~18 px from the edges */
#define RIGHT (CANVAS_W - 18)

static void dot(canvas_t *c, int cx, int cy, uint16_t color)
{
    canvas_ellipse(c, (float)cx, (float)cy, 5.0f, 5.0f, color, 0, CANVAS_H);
}

static void header(canvas_t *c, const ui_state_t *s)
{
    if (s->demo) {
        canvas_text(c, &FONT_TEXT, LEFT, 12, "DEMO playing", ACCENT);
    } else {
        canvas_text(c, &FONT_TEXT, LEFT, 12, "Chicco Piano", MUTED);
    }
    const char *link = s->pc ? "PC" : "no PC";
    int w = font_text_width(&FONT_TEXT, link);
    canvas_text_right(c, &FONT_TEXT, RIGHT, 12, link, s->pc ? INK : MUTED);
    dot(c, RIGHT - w - 10, 22, s->pc ? UI_GREEN : LINE);
    canvas_fill(c, LEFT, 40, RIGHT - LEFT, 1, LINE);
}

static void volume(canvas_t *c, int v)
{
    char num[8];
    snprintf(num, sizeof(num), "%d", v);
    canvas_text(c, &FONT_TEXT, LEFT, 112, "Volume", MUTED);
    canvas_fill(c, UI_VOL_X, UI_VOL_Y, UI_VOL_W, UI_VOL_H, LINE);
    canvas_fill(c, UI_VOL_X, UI_VOL_Y, UI_VOL_W * v / 127, UI_VOL_H, ACCENT);
    canvas_text_right(c, &FONT_TEXT, RIGHT, 112, num, INK);
}

static void octave(canvas_t *c, int o)
{
    const int box = 16;
    const int gap = 6;
    const int x0 = RIGHT - 5 * box - 4 * gap;
    canvas_text(c, &FONT_TEXT, LEFT, 150, "Octave", MUTED);
    for (int i = 0; i < 5; i++) {
        int x = x0 + i * (box + gap);
        if (i - 2 == o) {
            canvas_fill(c, x, 153, box, box, ACCENT);
        } else {
            canvas_fill(c, x, 153, box, box, LINE);
            canvas_fill(c, x + 2, 155, box - 4, box - 4, UI_BG);
        }
    }
}

static void loop(canvas_t *c, ui_loop_t l)
{
    static const char *const TEXT[] = {"empty", "stopped", "waiting for a note", "recording", "playing"};
    canvas_text(c, &FONT_TEXT, LEFT, 226, "Loop", MUTED);
    uint16_t color = l == UI_LOOP_RECORDING ? UI_RED
                     : l == UI_LOOP_PLAYING ? ACCENT
                     : l == UI_LOOP_EMPTY   ? MUTED
                                            : INK;
    canvas_text_right(c, &FONT_TEXT, RIGHT, 226, TEXT[l], color);
    if (l == UI_LOOP_RECORDING) {
        dot(c, RIGHT - font_text_width(&FONT_TEXT, TEXT[l]) - 10, 236, UI_RED);
    }
}

void ui_render(canvas_t *c, const ui_state_t *s)
{
    canvas_fill(c, 0, 0, CANVAS_W, CANVAS_H, UI_BG);
    header(c, s);

    canvas_text(c, &FONT_TEXT, LEFT, 52, "Sound", MUTED);
    canvas_text(c, &FONT_TITLE, LEFT, 72, s->sound ? s->sound : "?", INK);

    volume(c, s->volume);
    octave(c, s->octave);

    canvas_text(c, &FONT_TEXT, LEFT, 188, "Beat", MUTED);
    if (s->beat) {
        char beat[32];
        snprintf(beat, sizeof(beat), "%s %d bpm", s->beat, s->beat_bpm);
        canvas_text_right(c, &FONT_TEXT, RIGHT, 188, beat, ACCENT);
    } else {
        canvas_text_right(c, &FONT_TEXT, RIGHT, 188, "off", MUTED);
    }

    loop(c, s->loop);
}
