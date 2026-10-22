#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "matrix.h"
#include "midi_out.h"

/* What each crossing of the grid is and what it plays (docs/MATRIX.md,
 * docs/cheatsheet.html). Pure: no ESP-IDF.
 *
 * Keys (rows AB0-AB3, key = row * 8 + col + 1) play notes on channel 1, key 1
 * = F3, TEMPO+/- shift them by an octave. The 8 style buttons pick an
 * instrument, the animals and chicks play drums on channel 10, VOLUME+/- send
 * CC7, RESET sends all notes off and restores the defaults. DEMO is left to
 * the caller (KEYMAP_DEMO), and so are RECORD and REPLAY (looper.h) and
 * SELECT (rhythm.h: the drum beat of the last style button, k->style). */
#define KEY_CH         0
#define DRUM_CH        9
#define LOOP_CH        2  /* the looper's keys (DEMO_CH is 1) */
#define BASE_NOTE      53 /* key 1 = F3: the black keys start with a group of three */
#define VELOCITY       100
#define VOLUME_STEP    16
#define DEFAULT_VOLUME 100
#define MAX_OCTAVE     2
#define DEFAULT_STYLE  1 /* ROCK: SELECT's beat before a style button is pressed */

typedef struct {
    int octave;                   /* -MAX_OCTAVE..MAX_OCTAVE */
    int volume;                   /* 0..127 */
    uint8_t program;              /* the keys' instrument */
    size_t style;                 /* last style button, EF0-EF7 */
    uint8_t key_note[4 * N_COLS]; /* note sent for each held key, so its note off matches */
} keymap_t;

typedef enum {
    KEYMAP_NONE,
    KEYMAP_DEMO,   /* DEMO pressed */
    KEYMAP_RESET,  /* RESET pressed (its MIDI is already sent) */
    KEYMAP_RECORD, /* RECORD pressed */
    KEYMAP_REPLAY, /* REPLAY pressed */
    KEYMAP_SELECT, /* SELECT pressed */
    KEYMAP_STYLE,  /* a style button pressed (its program change is already sent) */
} keymap_action_t;

void keymap_init(keymap_t *k);

/* "key 14", "DEMO", "chick T1"; "?" for an unused crossing. */
void keymap_name(size_t r, size_t c, char *buf, size_t len);

/* "Piano", "Trumpet": the instrument a program number stands for; "?" if none. */
const char *keymap_program_name(uint8_t program);

/* A press or release at (r, c): sends its MIDI. */
keymap_action_t keymap_event(keymap_t *k, size_t r, size_t c, bool down, midi_send_fn send, void *ctx);
