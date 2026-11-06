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
    if (s->metro_on && s->metro_bpm) {
        /* The metronome, on every screen: its tempo and a dot flashing with the
         * click, red on beat 1 (the user, 2026-11-06). Takes DEMO's place. */
        char click[24];
        snprintf(click, sizeof(click), "Click %d bpm", s->metro_bpm);
        dot(c, LEFT + 5, 22, s->metro_flash == 2 ? UI_RED : s->metro_flash ? ACCENT : LINE);
        canvas_text(c, &FONT_TEXT, LEFT + 16, 12, click, INK);
    } else if (s->demo) {
        canvas_text(c, &FONT_TEXT, LEFT, 12, "DEMO playing", ACCENT);
    } else if (s->metro_bpm) {
        /* Off: the tempo it would click at, greyed, so the knob can be set first (the user, 2026-11-06). */
        char click[24];
        snprintf(click, sizeof(click), "Click %d bpm", s->metro_bpm);
        dot(c, LEFT + 5, 22, LINE);
        canvas_text(c, &FONT_TEXT, LEFT + 16, 12, click, MUTED);
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

static void loop(canvas_t *c, ui_loop_t l, int layers)
{
    static const char *const TEXT[] = {"empty",     "stopped", "waiting for a note",
                                       "recording", "playing", "adding"};
    char text[24];
    if (l == UI_LOOP_ADDING) {
        snprintf(text, sizeof(text), "adding layer %d", layers);
    } else if ((l == UI_LOOP_PLAYING || l == UI_LOOP_STOPPED) && layers > 1) {
        snprintf(text, sizeof(text), "%s, %d layers", TEXT[l], layers);
    } else {
        snprintf(text, sizeof(text), "%s", TEXT[l]);
    }
    canvas_text(c, &FONT_TEXT, LEFT, 226, "Loop", MUTED);
    bool rec = l == UI_LOOP_RECORDING || l == UI_LOOP_ADDING;
    uint16_t color = rec ? UI_RED : l == UI_LOOP_PLAYING ? ACCENT : l == UI_LOOP_EMPTY ? MUTED : INK;
    canvas_text_right(c, &FONT_TEXT, RIGHT, 226, text, color);
    if (rec) dot(c, RIGHT - font_text_width(&FONT_TEXT, text) - 10, 236, UI_RED);
}

static void beat_line(canvas_t *c, const ui_state_t *s, int y)
{
    canvas_text(c, &FONT_TEXT, LEFT, y, "Beat", MUTED);
    char beat[40];
    if (s->beat) {
        snprintf(beat, sizeof(beat), "%s %d bpm", s->beat, s->beat_bpm);
        canvas_text_right(c, &FONT_TEXT, RIGHT, y, beat, ACCENT);
    } else { /* the metronome is in the header */
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
    uint8_t notes[UI_KEYS];
    size_t n = 0;
    for (int k = 0; k < UI_KEYS; k++) {
        if (s->keys & (1ul << k)) notes[n++] = s->notes[k] ? s->notes[k] : (uint8_t)(65 + k); /* 65: an F */
    }
    chord_name_notes(notes, n, name, sizeof(name));
    const font_t *font = font_text_width(&FONT_BIG, name) <= RIGHT - LEFT ? &FONT_BIG : &FONT_TITLE;
    canvas_text_center(c, font, 56, name, INK);
    key_strip(c, s);
    if (s->held) canvas_text_center(c, &FONT_TEXT, 164, "held back", UI_RED);

    canvas_text(c, &FONT_TEXT, LEFT, 188, "Sound", MUTED);
    canvas_text_right(c, &FONT_TEXT, RIGHT, 188, s->sound ? s->sound : "?", INK);
    beat_line(c, s, 207);
    loop(c, s->loop, s->loop_layers);
}

#define G_LABEL_W 46 /* lane names left of the steps */
#define G_ORANGE  RGB(255, 150, 60)

/* One row of the bar: a box per step, lit where bits has it. */
static void step_row(canvas_t *c, const ui_state_t *s, int y, const char *label, uint32_t bits,
                     uint16_t color)
{
    canvas_text(c, &FONT_TEXT, LEFT, y - 4, label, MUTED);
    int x0 = LEFT + G_LABEL_W;
    int pitch = (RIGHT - x0) / s->g_steps;
    for (int i = 0; i < s->g_steps; i++) {
        int x = x0 + i * pitch;
        bool beat = i % s->g_steps_per_beat == 0;
        if (bits & (1u << i)) {
            canvas_fill(c, x, y, pitch - 2, 14, color);
        } else {
            canvas_fill(c, x + pitch / 2 - 2, y + 5, 4, 4, beat ? MUTED : LINE);
        }
    }
}

static void you_row(canvas_t *c, const ui_state_t *s, int y)
{
    canvas_text(c, &FONT_TEXT, LEFT, y - 4, "you", MUTED);
    int x0 = LEFT + G_LABEL_W;
    int pitch = (RIGHT - x0) / s->g_steps;
    for (int i = 0; i < s->g_steps; i++) {
        int cx = x0 + i * pitch + (pitch - 2) / 2;
        uint32_t bit = 1u << i;
        if (s->g_on_time & bit) {
            canvas_ellipse(c, (float)cx, (float)(y + 7), 5.0f, 5.0f, UI_GREEN, 0, CANVAS_H);
        } else if (s->g_off & bit) {
            canvas_ellipse(c, (float)cx, (float)(y + 7), 5.0f, 5.0f, G_ORANGE, 0, CANVAS_H);
        } else {
            canvas_fill(c, cx - 2, y + 5, 4, 4, LINE);
        }
    }
}

/* early |----+----| late, the last press's mark, its ms. */
static void timing_meter(canvas_t *c, const ui_state_t *s, int y)
{
    const int x0 = LEFT + 44;
    const int x1 = RIGHT - 70;
    const int mid = (x0 + x1) / 2;
    const int range_ms = 60; /* the ends of the scale */
    canvas_text(c, &FONT_TEXT, LEFT, y - 6, "early", MUTED);
    canvas_text(c, &FONT_TEXT, x1 + 6, y - 6, "late", MUTED);
    canvas_fill(c, x0, y + 3, x1 - x0, 2, LINE);
    int band = (x1 - x0) * 30 / (2 * range_ms); /* the on-time zone */
    canvas_fill(c, mid - band, y + 1, 2 * band, 6, RGB(40, 90, 70));
    canvas_fill(c, mid - 1, y - 3, 2, 14, MUTED);
    if (!s->g_has_last) return;
    int ms = s->g_last_ms < -range_ms ? -range_ms : s->g_last_ms > range_ms ? range_ms : s->g_last_ms;
    int x = mid + (x1 - x0) / 2 * ms / range_ms;
    bool ok = s->g_last_ms >= -30 && s->g_last_ms <= 30;
    canvas_ellipse(c, (float)x, (float)(y + 4), 6.0f, 6.0f, ok ? UI_GREEN : G_ORANGE, 0, CANVAS_H);
    char num[12];
    snprintf(num, sizeof(num), "%+d ms", s->g_last_ms);
    canvas_text_center(c, &FONT_TEXT, y + 22, num, ok ? UI_GREEN : G_ORANGE);
}

static void groove_screen(canvas_t *c, const ui_state_t *s)
{
    if (s->g_steps <= 0) {
        canvas_text_center(c, &FONT_TEXT, 120, "No beat playing", MUTED);
        canvas_text_center(c, &FONT_TEXT, 145, "SELECT starts it", MUTED);
        return;
    }
    int beats = s->g_steps / s->g_steps_per_beat;
    int now_beat = s->g_step / s->g_steps_per_beat;
    int gap = (RIGHT - LEFT) / beats;
    for (int b = 0; b < beats; b++) {
        int cx = LEFT + gap / 2 + b * gap;
        uint16_t color = b == now_beat ? (b == 0 ? UI_RED : ACCENT) : LINE;
        float r = b == now_beat ? 11.0f : 8.0f;
        canvas_ellipse(c, (float)cx, 72.0f, r, r, color, 0, CANVAS_H);
    }
    /* The step playing: a column behind the rows. */
    int x0 = LEFT + G_LABEL_W;
    int pitch = (RIGHT - x0) / s->g_steps;
    canvas_fill(c, x0 + s->g_step * pitch - 1, 102, pitch, 98, RGB(36, 42, 74));
    step_row(c, s, 110, s->g_lane_name[0] ? s->g_lane_name[0] : "", s->g_lane[0], ACCENT);
    step_row(c, s, 140, s->g_lane_name[1] ? s->g_lane_name[1] : "", s->g_lane[1], RGB(120, 160, 255));
    you_row(c, s, 175);
    canvas_fill(c, x0 + s->g_step * pitch + pitch / 2 - 5, 202, 9, 4, INK); /* the pointer */
    timing_meter(c, s, 230);
}

void ui_render(canvas_t *c, const ui_state_t *s)
{
    canvas_fill(c, 0, 0, CANVAS_W, CANVAS_H, UI_BG);
    header(c, s);
    if (s->screen == UI_SCREEN_PLAY) {
        play_screen(c, s);
        return;
    }
    if (s->screen == UI_SCREEN_GROOVE) {
        groove_screen(c, s);
        return;
    }

    canvas_text(c, &FONT_TEXT, LEFT, 52, "Sound", MUTED);
    canvas_text(c, &FONT_TITLE, LEFT, 72, s->sound ? s->sound : "?", INK);

    volume(c, s->volume);
    octave(c, s->octave);

    beat_line(c, s, 188);
    loop(c, s->loop, s->loop_layers);
}
