/*
 * The pots: a small mixer and two effects. Pure (no ESP-IDF), host-tested.
 *
 * Pot 1 sets the beat's loudness: it scales its note-on velocities (0-127, 127 =
 * as played). Pot 3 is the live keys' fader: CC11 (expression) on the keys'
 * channel, so a held note follows it too (2026-11-06, the user's ask; it also
 * works on ROCK's growl, which ignores velocity). Pot 2 is the metronome's
 * volume the same way (CC11 on METRO_CH; until 2026-11-06 it set the newest loop
 * layer's level, the user: "too much, I can rerecord"; loop layers play as
 * recorded). Pot 6 is the metronome's tempo (main/metronome.h): only its level here.
 * VOLUME +/- stays the master volume on top.
 *
 * Pot 4 is a filter (127 open, 0 dark), pot 5 the reverb (0 dry), on the live
 * keys and DEMO. A loop layer keeps pot 3's fader, the filter and the reverb it was recorded with
 * (frozen onto its channel when it's added; the user: "we shouldn't touch the
 * recordings", 2026-11-06). The drums stay as they are. FluidSynth 2.3
 * ignores CC74 (no default modulator, rendered 2026-11-06), so the filter is
 * an SF2 NRPN that offsets the cutoff generator on any SoundFont: 0 to
 * MIXER_FILTER_CENTS. The reverb is the same kind of NRPN on reverbEffectsSend
 * (generator 16), -MIXER_REVERB_SEND to +MIXER_REVERB_SEND tenths of a percent:
 * 0 dry, the middle the preset's own send, 127 full. CC91 couldn't make it dry
 * (FluidR3's saw lead has its own send) and added only 20% (rendered 2026-11-06).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keymap.h"
#include "looper.h"
#include "metronome.h"
#include "midi_out.h"

#define MIXER_POTS  6
#define MIXER_RAW   4095 /* the ADC's full scale */
#define MIXER_LEVEL 127
#define MIXER_TURN  3 /* levels a pot must move to count as turned (the LED bar): one-level hops are noise */
/* A pot left alone this long holds its level against a one-level hop: it needs MIXER_WAKE levels to
 * move again, then follows every level while turned. Pot 6 at 130 bpm flipped to 131 and back by
 * itself, and when other pots were turned (2026-11-06): one level is ~26 mV, the hysteresis held ~20. */
#define MIXER_SETTLE_US 500000
#define MIXER_WAKE      2
#define MIXER_FILTER_CENTS \
    (-4800) /* the cutoff at pot 4's 0; further down FluidR3's saw lead turns bright again (rendered) */
#define MIXER_REVERB_SEND 1000 /* 100%: FluidSynth clamps the sum to 0-100% */
#define MIXER_FX_LIVE     2    /* the pots' channels: 1 and 2 (0-1), the keys and DEMO */

typedef enum { MIXER_BEAT, MIXER_CLICK, MIXER_KEYS, MIXER_FILTER, MIXER_REVERB, MIXER_METRO } mixer_pot_t;

typedef struct {
    int32_t filt[MIXER_POTS]; /* smoothed reading, x16 */
    bool primed[MIXER_POTS];
    uint8_t pot[MIXER_POTS];        /* where each pot is, 0-127 */
    int64_t moved_us[MIXER_POTS];   /* when it was last turned; 0: not since boot */
    int64_t changed_us[MIXER_POTS]; /* when its level last changed */
    uint8_t rest[MIXER_POTS];       /* level at the last turn: moved_us needs MIXER_TURN from it */
    uint8_t beat, click, keys, filter, reverb, metro;
    uint8_t layer_filter[LOOPER_LAYERS], layer_reverb[LOOPER_LAYERS]; /* frozen when added */
    uint8_t layer_keys[LOOPER_LAYERS];                                /* pot 3's fader, frozen the same way */
} mixer_t;

void mixer_init(mixer_t *m);

/* One reading of a pot (0..MIXER_RAW). Returns true when its level changed. */
bool mixer_feed(mixer_t *m, mixer_pot_t pot, int raw, int64_t now_us);

/* A velocity scaled by a level; a note never rounds down to silence unless the level is 0. */
uint8_t mixer_scale(uint8_t velocity, uint8_t level);

/* msg into out with a note on's velocity scaled (other messages copied). Returns
 * the length to send: 0 for a note on at level 0, which is left out. */
size_t mixer_apply(const uint8_t *msg, size_t len, uint8_t level, uint8_t *out);

/* Sends pot 2's, 3's, 4's or 5's setting to the live channels (what: for the log, once). */
void mixer_send_fx(const mixer_t *m, mixer_pot_t pot, midi_send_fn send, void *ctx);

/* A loop layer just added: it keeps pot 3's fader, the filter and the reverb as they are now
 * (the fader: the user heard a layer recorded quiet come back at full, 2026-11-06). */
void mixer_freeze_layer(mixer_t *m, size_t layer, midi_send_fn send, void *ctx);

/* A layer's frozen fader, filter and reverb again (the PC link came up). */
void mixer_send_layer_fx(const mixer_t *m, size_t layer, midi_send_fn send, void *ctx);
