#include "ui.h"

#include <stdio.h>

#include "chord.h"

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

int ui_percent(int midi)
{
    return (midi * 100 + 63) / 127; /* rounded */
}

static void volume(canvas_t *c, int v)
{
    char num[8];
    snprintf(num, sizeof(num), "%d%%", ui_percent(v));
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

static void beat_line(canvas_t *c, const ui_state_t *s, int y)
{
    canvas_text(c, &FONT_TEXT, LEFT, y, "Beat", MUTED);
    if (s->beat) {
        char beat[32];
        snprintf(beat, sizeof(beat), "%s %d bpm", s->beat, s->beat_bpm);
        canvas_text_right(c, &FONT_TEXT, RIGHT, y, beat, ACCENT);
    } else {
        canvas_text_right(c, &FONT_TEXT, RIGHT, y, "off", MUTED);
    }
}

static void key_strip(canvas_t *c, const ui_state_t *s)
{
    for (int k = 0; k < UI_KEYS; k++) {
        /* Key 1 is an F: the same pitches as a piano's black keys come out short. */
        static const bool BLACK[12] = {false, true,  false, true,  false, false,
                                       true,  false, true,  false, true,  false};
        bool black = BLACK[(k + 5) % 12];
        uint32_t bit = 1ul << k;
        uint16_t color = s->keys & bit ? ACCENT : s->held & bit ? UI_RED : black ? LINE : MUTED;
        int x = LEFT + k * (RIGHT - LEFT) / UI_KEYS;
        canvas_fill(c, x, UI_STRIP_Y, 5, black ? UI_STRIP_H * 3 / 5 : UI_STRIP_H, color);
    }
}

static void play_screen(canvas_t *c, const ui_state_t *s)
{
    char name[16];
    chord_name(s->keys, name, sizeof(name));
    const font_t *font = font_text_width(&FONT_BIG, name) <= RIGHT - LEFT ? &FONT_BIG : &FONT_TITLE;
    canvas_text_center(c, font, 56, name, INK);
    key_strip(c, s);
    if (s->held) canvas_text_center(c, &FONT_TEXT, 164, "held back", UI_RED);

    canvas_text(c, &FONT_TEXT, LEFT, 188, "Sound", MUTED);
    canvas_text_right(c, &FONT_TEXT, RIGHT, 188, s->sound ? s->sound : "?", INK);
    beat_line(c, s, 207);
    loop(c, s->loop);
}

void ui_render(canvas_t *c, const ui_state_t *s)
{
    canvas_fill(c, 0, 0, CANVAS_W, CANVAS_H, UI_BG);
    header(c, s);
    if (s->screen == UI_SCREEN_PLAY) {
        play_screen(c, s);
        return;
    }

    canvas_text(c, &FONT_TEXT, LEFT, 52, "Sound", MUTED);
    canvas_text(c, &FONT_TITLE, LEFT, 72, s->sound ? s->sound : "?", INK);

    volume(c, s->volume);
    octave(c, s->octave);

    beat_line(c, s, 188);
    loop(c, s->loop);
}
