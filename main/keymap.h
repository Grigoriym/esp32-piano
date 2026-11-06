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
 * SELECT (rhythm.h: the drum beat of the last style button, k->style).
 *
 * EDM mode (DISCO, MARCH, SLOW ROCK or ROCK, until another style button or RESET): the keys play only
 * the notes of A natural minor, 7 keys an octave, so no key sounds wrong, and
 * the drum channel plays EDM_KIT (every other style: kit 0). */
#define KEY_CH         0
#define DRUM_CH        9
#define LOOP_CH        2  /* the looper's keys (DEMO_CH is 1), its first layer */
#define LOOP_CHANNELS  4  /* one per looper layer: LOOP_CH..LOOP_CH+3 */
#define BASE_NOTE      53 /* key 1 = F3: the black keys start with a group of three */
#define VELOCITY       100
#define EDM_VELOCITY   55 /* the saw lead is loud: the beat should lead */
#define VOLUME_STEP    16
#define DEFAULT_VOLUME 127 /* max: what RESET and boot give */
#define MAX_OCTAVE     2
#define DEFAULT_STYLE  7  /* BLUES: SELECT's beat before a style button is pressed (not an EDM one) */
#define EDM_STYLE      3  /* DISCO: EDM mode, the keys play A natural minor */
#define EDM_STYLE_2    4  /* MARCH: EDM mode too, with a breakbeat */
#define EDM_STYLE_3    0  /* SLOW ROCK: EDM mode too, with a techno beat */
#define EDM_STYLE_4    1  /* ROCK: EDM mode too, dubstep: the growl bass, a half-time beat */
#define EDM_KIT        30 /* drum program in EDM mode: tools/gen_edm_kit_sf2.py on the PC */
#define SCALE_ROOT     33 /* EDM mode: key 1 = A1, then one key per scale step (key 8 = A2) */

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
    KEYMAP_METRO,  /* FROG pressed: the metronome (metronome.h) */
} keymap_action_t;

void keymap_init(keymap_t *k);

/* "key 14", "DEMO", "chick T1"; "?" for an unused crossing. */
void keymap_name(size_t r, size_t c, char *buf, size_t len);

/* "Piano", "Trumpet": the instrument a program number stands for; "?" if none. */
const char *keymap_program_name(uint8_t program);

/* DISCO and MARCH: the keys in A minor, softer, the EDM drum kit. */
bool keymap_is_edm(size_t style);

/* The note key (0..31) plays right now, octave included. */
uint8_t keymap_key_note(const keymap_t *k, size_t key);

/* A press or release at (r, c): sends its MIDI. */
keymap_action_t keymap_event(keymap_t *k, size_t r, size_t c, bool down, midi_send_fn send, void *ctx);
