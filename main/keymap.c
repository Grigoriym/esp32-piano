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
    81, /* SLOW ROCK: saw lead, EDM mode with a techno beat (rhythm.c: TECHNO) */
    38, /* ROCK: growl bass, EDM mode, dubstep (tools/gen_growl_sf2.py takes over this program on the PC) */
    88, /* NEW AGE: Siamese cat (same way) */
    81, /* DISCO: saw lead, EDM mode (A minor keys, rhythm.c: EDM beat) */
    81, /* MARCH: saw lead, EDM mode with a breakbeat (rhythm.c: BREAKS) */
    21, /* WALTZ: accordion, the Balkan sound (rhythm.c: BALKAN beat) */
    12, /* SAMBA: marimba */
    4,  /* BLUES: electric piano */
};
/* The keys' instrument for the screen: program 0 (after RESET) or a style button's. */
const char *keymap_program_name(uint8_t program)
{
    static const char *const NAMES[N_COLS] = {
        "Saw lead, Am", "Growl bass, Am", "Siamese cat", "Saw lead, Am",
        "Saw lead, Am", "Accordion",      "Marimba",     "Electric piano",
    };
    if (program == 0) return "Piano";
    for (size_t c = 0; c < N_COLS; c++) {
        if (STYLE_PROGRAM[c] == program) return NAMES[c];
    }
    return "?";
}

/* The chicks' effects (fx.h's order: T1-T4, then B1-B4) by column: they played drums until 2026-11-06. */
static const uint8_t CHICK_FX[N_COLS] = {7, 4, 2, 1, 5, 0, 6, 3}; /* B4 B1 T3 T2 B2 T1 B3 T4 */

/* General MIDI drum notes for the animals, AB7 x EF0-EF7 (AB6: the chicks, no drums); 0 = none. */
static const uint8_t DRUM_NOTE[2][N_COLS] = {
    /* AB6 chicks */ {0},
    /* AB7 */ {36, 0, 48, 46, 0, 0, 0, 0}, /* dog, (frog: the metronome), red bird, blue bird */
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
    for (uint8_t ch = LOOP_CH; ch < LOOP_CH + LOOP_CHANNELS; ch++)
        midi3(send, ctx, 0xb0 | ch, 7, (uint8_t)k->volume, what);
}

static void reset(keymap_t *k, midi_send_fn send, void *ctx)
{
    midi3(send, ctx, 0xb0 | KEY_CH, 123, 0, "all notes off");
    midi3(send, ctx, 0xb0 | DRUM_CH, 123, 0, "all notes off (drums)");
    for (uint8_t ch = LOOP_CH; ch < LOOP_CH + LOOP_CHANNELS; ch++)
        midi3(send, ctx, 0xb0 | ch, 123, 0, "all notes off (loop)");
    k->octave = 0;
    k->volume = DEFAULT_VOLUME;
    k->program = 0;
    k->style = DEFAULT_STYLE;
    send_volume(k, send, ctx);
    uint8_t program[2] = {0xc0 | KEY_CH, 0};
    send(program, 2, "program 0", ctx);
    uint8_t kit[2] = {0xc0 | DRUM_CH, 0};
    send(kit, 2, "drum kit 0", ctx);
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

bool keymap_is_edm(size_t style)
{
    return style == EDM_STYLE || style == EDM_STYLE_2 || style == EDM_STYLE_3 || style == EDM_STYLE_4;
}

uint8_t keymap_key_note(const keymap_t *k, size_t key)
{
    if (keymap_is_edm(k->style)) {
        static const uint8_t MINOR[7] = {0, 2, 3, 5, 7, 8, 10};
        return (uint8_t)(SCALE_ROOT + 12 * (k->octave + (int)key / 7) + MINOR[key % 7]);
    }
    return (uint8_t)(BASE_NOTE + 12 * k->octave + (int)key);
}

/* A style button: the keys' instrument, and the drum kit (EDM_KIT in EDM mode). */
static void style_button(keymap_t *k, size_t c, midi_send_fn send, void *ctx)
{
    char what[32];
    snprintf(what, sizeof(what), "program %u", STYLE_PROGRAM[c]);
    k->program = STYLE_PROGRAM[c];
    k->style = c;
    uint8_t msg[2] = {0xc0 | KEY_CH, k->program};
    send(msg, 2, what, ctx);
    uint8_t kit = keymap_is_edm(c) ? EDM_KIT : 0;
    snprintf(what, sizeof(what), "drum kit %u", kit);
    uint8_t kit_msg[2] = {0xc0 | DRUM_CH, kit};
    send(kit_msg, 2, what, ctx);
}

/* Buttons the caller handles, AB5 and AB7 x EF0-EF7. */
static const keymap_action_t ACTION[2][N_COLS] = {
    /* AB5 */ {[5] = KEYMAP_RECORD, [6] = KEYMAP_REPLAY},
    /* AB7 */ {[1] = KEYMAP_METRO, [4] = KEYMAP_SELECT, [5] = KEYMAP_DEMO}, /* FROG was a low tom */
};

keymap_action_t keymap_event(keymap_t *k, size_t r, size_t c, bool down, midi_send_fn send, void *ctx)
{
    char what[32];
    if (r < 4) {
        size_t key = r * N_COLS + c;
        if (down) k->key_note[key] = keymap_key_note(k, key);
        snprintf(what, sizeof(what), "note %s %u", down ? "on" : "off", k->key_note[key]);
        uint8_t velocity = keymap_is_edm(k->style) ? EDM_VELOCITY : VELOCITY;
        midi3(send, ctx, (down ? 0x90 : 0x80) | KEY_CH, k->key_note[key], down ? velocity : 0, what);
        return KEYMAP_NONE;
    }
    if (r == 6) {
        k->fx = CHICK_FX[c];
        k->fx_down = down;
        return KEYMAP_FX;
    }
    if (r >= 6 && DRUM_NOTE[r - 6][c]) {
        uint8_t note = DRUM_NOTE[r - 6][c];
        snprintf(what, sizeof(what), "drum %s %u", down ? "on" : "off", note);
        midi3(send, ctx, (down ? 0x90 : 0x80) | DRUM_CH, note, down ? VELOCITY : 0, what);
        return KEYMAP_NONE;
    }
    if (!down) return KEYMAP_NONE;
    if (r == 4) {
        style_button(k, c, send, ctx);
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
    } else if (r == 5 || r == 7) {
        return ACTION[r == 5 ? 0 : 1][c];
    }
    return KEYMAP_NONE;
}
