#include "keymap.h"

#include <stdio.h>

static const char *const BUTTONS[4][N_COLS] = {
    /* AB4 */ {"SLOW ROCK", "ROCK", "NEW AGE", "DISCO", "MARCH", "WALTZ", "SAMBA", "BLUES"},
    /* AB5 */ {"TEMPO+", "TEMPO-", "VOLUME+", "VOLUME-", "RESET", "RECORD", "REPLAY", NULL},
    /* AB6 */
    {"chick B4", "chick B1", "chick T3", "chick T2", "chick B2", "chick T1", "chick B3", "chick T4"},
    /* AB7 */ {"DOG", "FROG", "RED BIRD", "BLUE BIRD", "SELECT", "DEMO", NULL, NULL},
};
/* chick T1-T4: row next to the style buttons, from the BLUES end; B1-B4: row
 * next to the bottom edge, same direction. */

/* General MIDI programs for the style buttons, AB4 EF0-EF7 order. */
static const uint8_t STYLE_PROGRAM[N_COLS] = {
    19, /* SLOW ROCK: church organ */
    30, /* ROCK: distortion guitar */
    88, /* NEW AGE: new age pad */
    81, /* DISCO: saw lead */
    56, /* MARCH: trumpet */
    21, /* WALTZ: accordion */
    12, /* SAMBA: marimba */
    4,  /* BLUES: electric piano */
};
/* General MIDI drum notes for the animals and chicks, AB6/AB7 x EF0-EF7; 0 = none. */
static const uint8_t DRUM_NOTE[2][N_COLS] = {
    /* AB6 chicks */ {75, 38, 51, 49, 39, 42, 54, 56},
    /* AB7 */ {36, 45, 48, 46, 0, 0, 0, 0}, /* dog, frog, red bird, blue bird */
};

void keymap_init(keymap_t *k)
{
    *k = (keymap_t){.volume = DEFAULT_VOLUME, .style = DEFAULT_STYLE};
}

void keymap_name(size_t r, size_t c, char *buf, size_t len)
{
    if (r < 4) {
        snprintf(buf, len, "key %u", (unsigned)(r * N_COLS + c + 1));
    } else if (BUTTONS[r - 4][c]) {
        snprintf(buf, len, "%s", BUTTONS[r - 4][c]);
    } else {
        snprintf(buf, len, "?");
    }
}

static void midi3(midi_send_fn send, void *ctx, uint8_t status, uint8_t d1, uint8_t d2, const char *what)
{
    uint8_t msg[3] = {status, d1, d2};
    send(msg, 3, what, ctx);
}

static void send_volume(const keymap_t *k, midi_send_fn send, void *ctx)
{
    char what[32];
    snprintf(what, sizeof(what), "volume %d", k->volume);
    midi3(send, ctx, 0xb0 | KEY_CH, 7, (uint8_t)k->volume, what);
    midi3(send, ctx, 0xb0 | DRUM_CH, 7, (uint8_t)k->volume, what);
    midi3(send, ctx, 0xb0 | LOOP_CH, 7, (uint8_t)k->volume, what);
}

static void reset(keymap_t *k, midi_send_fn send, void *ctx)
{
    midi3(send, ctx, 0xb0 | KEY_CH, 123, 0, "all notes off");
    midi3(send, ctx, 0xb0 | DRUM_CH, 123, 0, "all notes off (drums)");
    midi3(send, ctx, 0xb0 | LOOP_CH, 123, 0, "all notes off (loop)");
    k->octave = 0;
    k->volume = DEFAULT_VOLUME;
    k->program = 0;
    k->style = DEFAULT_STYLE;
    send_volume(k, send, ctx);
    uint8_t program[2] = {0xc0 | KEY_CH, 0};
    send(program, 2, "program 0", ctx);
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

keymap_action_t keymap_event(keymap_t *k, size_t r, size_t c, bool down, midi_send_fn send, void *ctx)
{
    char what[32];
    if (r < 4) {
        size_t key = r * N_COLS + c;
        if (down) k->key_note[key] = (uint8_t)(BASE_NOTE + 12 * k->octave + (int)key);
        snprintf(what, sizeof(what), "note %s %u", down ? "on" : "off", k->key_note[key]);
        midi3(send, ctx, (down ? 0x90 : 0x80) | KEY_CH, k->key_note[key], down ? VELOCITY : 0, what);
        return KEYMAP_NONE;
    }
    if (r >= 6 && DRUM_NOTE[r - 6][c]) {
        uint8_t note = DRUM_NOTE[r - 6][c];
        snprintf(what, sizeof(what), "drum %s %u", down ? "on" : "off", note);
        midi3(send, ctx, (down ? 0x90 : 0x80) | DRUM_CH, note, down ? VELOCITY : 0, what);
        return KEYMAP_NONE;
    }
    if (!down) return KEYMAP_NONE;
    if (r == 4) {
        snprintf(what, sizeof(what), "program %u", STYLE_PROGRAM[c]);
        k->program = STYLE_PROGRAM[c];
        k->style = c;
        uint8_t msg[2] = {0xc0 | KEY_CH, k->program};
        send(msg, 2, what, ctx);
        return KEYMAP_STYLE;
    }
    if (r == 5 && c <= 1) { /* TEMPO+ / TEMPO- */
        k->octave = clamp(k->octave + (c == 0 ? 1 : -1), -MAX_OCTAVE, MAX_OCTAVE);
        snprintf(what, sizeof(what), "octave %+d (next notes)", k->octave);
        send(NULL, 0, what, ctx);
    } else if (r == 5 && c <= 3) { /* VOLUME+ / VOLUME- */
        k->volume = clamp(k->volume + (c == 2 ? VOLUME_STEP : -VOLUME_STEP), 0, 127);
        send_volume(k, send, ctx);
    } else if (r == 5 && c == 4) {
        reset(k, send, ctx);
        return KEYMAP_RESET;
    } else if (r == 5 && c == 5) {
        return KEYMAP_RECORD;
    } else if (r == 5 && c == 6) {
        return KEYMAP_REPLAY;
    } else if (r == 7 && c == 4) {
        return KEYMAP_SELECT;
    } else if (r == 7 && c == 5) {
        return KEYMAP_DEMO;
    }
    return KEYMAP_NONE;
}
