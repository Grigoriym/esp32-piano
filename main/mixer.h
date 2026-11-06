/*
 * The pots: a small mixer and two effects. Pure (no ESP-IDF), host-tested.
 *
 * Pot 1 sets the beat's loudness, pot 2 the newest loop layer's, pot 3 the
 * live keys'. Each scales the note-on velocities of its part (0-127, 127 =
 * as played); VOLUME +/- stays the master volume on top.
 *
 * Pot 4 is a filter (127 open, 0 dark), pot 5 the reverb (0 dry), on the live
 * keys and DEMO. A loop layer keeps the filter and reverb it was recorded with
 * (frozen onto its channel when it's added; the user: "we shouldn't touch the
 * recordings", 2026-11-06). The drums stay as they are. FluidSynth 2.3
 * ignores CC74 (no default modulator, rendered 2026-11-06), so the filter is
 * an SF2 NRPN that offsets the cutoff generator on any SoundFont: 0 to
 * MIXER_FILTER_CENTS. The reverb is the same kind of NRPN on reverbEffectsSend
 * (generator 16), -MIXER_REVERB_SEND to +MIXER_REVERB_SEND tenths of a percent:
 * 0 dry, the middle the preset's own send, 127 full. CC91 couldn't make it dry
 * (FluidR3's saw lead has its own send) and added only 20% (rendered 2026-11-06).
 *
 * Pot 2 follows the newest layer. A layer just started takes the pot's level.
 * When pot 2 moves to an older layer (an undo), that layer keeps its level and
 * the pot picks it up only once it reaches it, so nothing jumps.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keymap.h"
#include "looper.h"
#include "midi_out.h"

#define MIXER_POTS  5
#define MIXER_RAW   4095 /* the ADC's full scale */
#define MIXER_LEVEL 127
#define MIXER_TURN  3 /* levels a pot must move to count as turned (the LED bar): one-level hops are noise */
#define MIXER_FILTER_CENTS \
    (-4800) /* the cutoff at pot 4's 0; further down FluidR3's saw lead turns bright again (rendered) */
#define MIXER_REVERB_SEND 1000 /* 100%: FluidSynth clamps the sum to 0-100% */
#define MIXER_FX_LIVE     2    /* the pots' channels: 1 and 2 (0-1), the keys and DEMO */

typedef enum { MIXER_BEAT, MIXER_LAYER, MIXER_KEYS, MIXER_FILTER, MIXER_REVERB } mixer_pot_t;

typedef struct {
    int32_t filt[MIXER_POTS]; /* smoothed reading, x16 */
    bool primed[MIXER_POTS];
    uint8_t pot[MIXER_POTS];      /* where each pot is, 0-127 */
    int64_t moved_us[MIXER_POTS]; /* when it was last turned; 0: not since boot */
    uint8_t rest[MIXER_POTS];     /* level at the last turn: moved_us needs MIXER_TURN from it */
    uint8_t beat, keys, filter, reverb;
    uint8_t layer[LOOPER_LAYERS];
    uint8_t layer_filter[LOOPER_LAYERS], layer_reverb[LOOPER_LAYERS]; /* frozen when added */
    int target;                                                       /* the layer pot 2 sets; -1: none */
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

/* Sends pot 4's or pot 5's setting to the live channels (what: for the log, once). */
void mixer_send_fx(const mixer_t *m, mixer_pot_t pot, midi_send_fn send, void *ctx);

/* A loop layer just added: it keeps the filter and reverb as they are now. */
void mixer_freeze_layer(mixer_t *m, size_t layer, midi_send_fn send, void *ctx);

/* A layer's frozen filter and reverb again (the PC link came up). */
void mixer_send_layer_fx(const mixer_t *m, size_t layer, midi_send_fn send, void *ctx);
