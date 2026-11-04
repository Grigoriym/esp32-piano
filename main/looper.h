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
 * RECORD while the loop plays adds a layer on top, recorded as the loop goes
 * round, until RECORD again; up to LOOPER_LAYERS. Each layer's keys play on
 * their own channel (LOOP_CH + layer) with the instrument they were recorded
 * with, drums stay on DRUM_CH. looper_undo (the knob's press) drops the layer
 * being added, or else the last one; the last one left: the whole loop.
 *
 * REPLAY stops the loop or starts it again from the top (at the next bar line
 * while the beat runs); while recording it closes the loop like RECORD, while
 * adding a layer it keeps the layer and stops. A note still held at the end of
 * the loop stops there. */
#define LOOPER_MAX_EVENTS 1024
#define LOOPER_LAYERS     LOOP_CHANNELS

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
    size_t dub_events;                   /* in the layer being added */
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
    uint8_t gain[LOOPER_LAYERS];         /* each layer's level, 0-127 (pot 2: main/mixer.h) */
} looper_t;

void looper_init(looper_t *l);

/* The beat's bar lines: one at origin_us, every bar_us; bar_us 0: no beat. */
void looper_set_grid(looper_t *l, int64_t origin_us, int64_t bar_us);

void looper_record(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx);
void looper_replay(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx);

/* Drops the layer being added, or the last one (the knob's press). */
void looper_undo(looper_t *l, midi_send_fn send, void *ctx);

/* Stops whatever it does and forgets the loop (RESET). */
void looper_clear(looper_t *l, midi_send_fn send, void *ctx);

/* Every message keymap sent: notes are stored while recording or adding a
 * layer. program: the keys' instrument right now. */
void looper_capture(looper_t *l, int64_t now_us, const uint8_t *msg, size_t len, uint8_t program);

/* Call often (every scan): sends what is due by now_us. Each message's own time
 * is in l->due_us while it is sent. */
void looper_tick(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx);

/* Layers in the loop, the one being added included. */
size_t looper_layers(const looper_t *l);
