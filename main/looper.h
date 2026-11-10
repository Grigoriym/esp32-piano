#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keymap.h"
#include "midi_out.h"

/* RECORD / REPLAY / the knob's press: a looper with layers. Pure: the caller
 * passes the time.
 *
 * RECORD (no loop playing) arms it and forgets any old loop; recording starts
 * with the first note (keys and drums, what keymap sends). RECORD again closes
 * the loop, which keeps playing. While the beat runs (looper_set_grid) the loop
 * is in time with it: it starts at the bar line of the first note (a note up to
 * a 16th early counts as on it) and its length is the time from the first note
 * to RECORD, rounded to whole bars. Notes past that length that come before the
 * first note's place finish a phrase begun mid-bar and wrap to the start; the
 * others (the next pass, played before a late RECORD) are dropped. Without the beat the loop is as long as played.
 *
 * RECORD while the loop plays adds a layer on top, until RECORD again; up to
 * LOOPER_LAYERS. It is heard once closed. Its passes count from the loop's start
 * nearest the first RECORD; recorded over more than one pass (the second RECORD
 * rounded to whole passes), the loop grows to that many passes and the older
 * layers repeat in it: a part played once, then a pass of silence, is a layer that
 * plays every other pass (the user, 2026-11-06). Notes before that start (a
 * pickup) go to the new loop's end; notes past its end (a late RECORD) are dropped. Each layer's keys play on
 * their own channel (LOOP_CH + layer) with the instrument they were recorded
 * with, drums stay on DRUM_CH. looper_undo (the knob's press) drops the layer
 * being added, or else the last one; the last one left: the whole loop.
 *
 * REPLAY stops the loop or starts it again from the top (at the next bar line
 * while the beat runs); while recording it closes the loop like RECORD, while
 * adding a layer it keeps the layer and stops. A note still held at the end of
 * the loop stops there.
 *
 * looper_mute (the knob's press on the Layers screen, 2026-11-08) silences a layer or brings it
 * back, at the next bar line while the beat or the metronome runs, else at once: the layer stays in
 * the loop, only its notes aren't sent. Muting stops its notes still sounding (its drums ring out).
 *
 * Pot moves (keys fader, filter, reverb: looper_pot) made while recording a layer are
 * part of it (the user, 2026-11-07: a reverb turned during a take was lost): stored as
 * CC LOOPER_POT_CC + the pot, played back as what that pot sends, on the layer's channel.
 * The first move in a take also stores where the pot was at the take's start, so the
 * layer goes back to it each time round. */
#define LOOPER_POT_CC     102 /* + mixer_pot_t: an undefined CC, never sent as is */
#define LOOPER_POTS       3   /* keys fader, filter, reverb: mixer_pot_t 2, 3, 4 */
#define LOOPER_MAX_EVENTS 1024
#define LOOPER_LAYERS     LOOP_CHANNELS
#define LOOPER_DUB_EVENTS 512 /* the layer being added, until it is closed */
#define LOOPER_MAX_PASSES 8   /* how far a layer can grow the loop */

typedef enum {
    LOOPER_IDLE, /* empty, or a loop that is stopped */
    LOOPER_ARMED,
    LOOPER_RECORDING, /* the first layer */
    LOOPER_PLAYING,   /* adding a layer too when dubbing */
} looper_state_t;

typedef struct {
    uint32_t t_us; /* since the loop's start */
    uint8_t msg[3];
    uint8_t layer;
} looper_event_t;

typedef struct {
    looper_state_t state;
    bool dubbing;                         /* PLAYING and recording layer n_layers */
    looper_event_t ev[LOOPER_MAX_EVENTS]; /* sorted by t_us */
    size_t n;
    size_t n_layers;
    size_t dub_events; /* in the layer being added */
    struct {
        int32_t t_us; /* from dub_base_us; <0: before it */
        uint8_t msg[3];
    } dub[LOOPER_DUB_EVENTS];
    int64_t dub_base_us;                 /* the pass start nearest the RECORD that began the layer */
    uint32_t length_us;                  /* 0: no loop */
    uint8_t program[LOOPER_LAYERS];      /* each layer's keys' instrument */
    uint8_t program_due;                 /* bit per layer: program change still to send */
    int64_t start_us;                    /* recording: the loop's start; playing: this pass's start */
    size_t pos;                          /* playing: next event */
    int64_t played_us;                   /* playing: how far into the pass looper_tick has sent */
    int64_t grid_origin_us, grid_bar_us; /* the beat's bars; bar 0: no beat */
    int64_t due_us;                      /* when what looper_tick sends right now should sound */
    uint8_t rec_held[2][16];             /* bit per note, keys and drums: held while recording */
    uint8_t sounding[16][16];            /* bit per channel and note: loop notes on */
    uint8_t pluck[LOOPER_LAYERS]; /* each layer's frozen pluck (main.c), for its recorded filter moves */
    uint8_t gain[LOOPER_LAYERS];  /* each layer's level, 0-127 (127: no pot sets it since 2026-11-06) */
    uint8_t pot_now[LOOPER_POTS]; /* where each recorded pot is (kept by looper_init) */
    uint8_t pot_known;            /* bit per pot: pot_now holds a reading */
    uint8_t pot_taken;            /* bit per pot: its value at the take's start is stored */
    uint8_t muted;                /* bit per layer: its notes aren't sent */
    uint8_t mute_flip;            /* bit per layer: muted flips at mute_at_us */
    int64_t mute_at_us;
} looper_t;

void looper_init(looper_t *l);

/* The beat's bar lines: one at origin_us, every bar_us; bar_us 0: no beat. */
void looper_set_grid(looper_t *l, int64_t origin_us, int64_t bar_us);

void looper_record(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx);
void looper_replay(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx);

/* Drops the layer being added, or the last one (the knob's press). */
void looper_undo(looper_t *l, midi_send_fn send, void *ctx);

/* Mutes layer (0..n_layers-1) or brings it back, at the next bar line after now_us (the
 * time being sent, the lead included) or at once without a beat; pressed again before
 * that, it stays as it was. */
void looper_mute(looper_t *l, size_t layer, int64_t now_us, midi_send_fn send, void *ctx);

/* Stops whatever it does and forgets the loop (RESET). */
void looper_clear(looper_t *l, midi_send_fn send, void *ctx);

/* Every message keymap sent: notes are stored while recording or adding a
 * layer. program: the keys' instrument right now. */
void looper_capture(looper_t *l, int64_t now_us, const uint8_t *msg, size_t len, uint8_t program);

/* A pot's new level, as it is sent (pot: mixer_pot_t; only the keys fader, filter and
 * reverb are kept). Stored while recording or adding a layer. */
void looper_pot(looper_t *l, int64_t now_us, int pot, uint8_t level);

/* Call often (every scan): sends what is due by now_us. Each message's own time
 * is in l->due_us while it is sent. */
void looper_tick(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx);

/* Layers in the loop, the one being added included. */
size_t looper_layers(const looper_t *l);
