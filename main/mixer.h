/*
 * The three pots: a small mixer. Pure (no ESP-IDF), host-tested.
 *
 * Pot 1 sets the beat's loudness, pot 2 the newest loop layer's, pot 3 the
 * live keys'. Each scales the note-on velocities of its part (0-127, 127 =
 * as played); VOLUME +/- stays the master volume on top.
 *
 * Pot 2 follows the newest layer. A layer just started takes the pot's level.
 * When pot 2 moves to an older layer (an undo), that layer keeps its level and
 * the pot picks it up only once it reaches it, so nothing jumps.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "looper.h"

#define MIXER_POTS  3
#define MIXER_RAW   4095 /* the ADC's full scale */
#define MIXER_LEVEL 127
#define MIXER_TURN  3 /* levels a pot must move to count as turned (the LED bar): one-level hops are noise */

typedef enum { MIXER_BEAT, MIXER_LAYER, MIXER_KEYS } mixer_pot_t;

typedef struct {
    int32_t filt[MIXER_POTS]; /* smoothed reading, x16 */
    bool primed[MIXER_POTS];
    uint8_t pot[MIXER_POTS];      /* where each pot is, 0-127 */
    int64_t moved_us[MIXER_POTS]; /* when it was last turned; 0: not since boot */
    uint8_t rest[MIXER_POTS];     /* level at the last turn: moved_us needs MIXER_TURN from it */
    uint8_t beat, keys;
    uint8_t layer[LOOPER_LAYERS];
    int target;    /* the layer pot 2 sets; -1: none */
    bool attached; /* pot 2 sets the target's level (false: still to pick it up) */
} mixer_t;

void mixer_init(mixer_t *m);

/* One reading of a pot (0..MIXER_RAW). Returns true when its level changed. */
bool mixer_feed(mixer_t *m, mixer_pot_t pot, int raw, int64_t now_us);

/* How many layers the loop has now (the one being recorded included): pot 2
 * goes to the newest. Call every scan. */
void mixer_set_layers(mixer_t *m, size_t layers);

/* A velocity scaled by a level; a note never rounds down to silence unless the level is 0. */
uint8_t mixer_scale(uint8_t velocity, uint8_t level);

/* msg into out with a note on's velocity scaled (other messages copied). Returns
 * the length to send: 0 for a note on at level 0, which is left out. */
size_t mixer_apply(const uint8_t *msg, size_t len, uint8_t level, uint8_t *out);
