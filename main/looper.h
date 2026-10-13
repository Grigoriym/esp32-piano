#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "midi_out.h"

/* RECORD / REPLAY: a looper. Pure: the caller passes the time.
 *
 * RECORD arms it; recording starts with the first note (keys and drums, what
 * keymap sends). RECORD again closes the loop, which plays at once and over
 * and over. REPLAY stops it or starts it again from the top; while recording
 * it closes the loop like RECORD. RECORD while the loop plays records a new
 * one. Keys play back on LOOP_CH with the instrument they were recorded with,
 * so the live keys can switch instrument and play over them; drums stay on
 * DRUM_CH. A note still held at the end of the loop stops there. */
#define LOOPER_MAX_EVENTS 1024

typedef enum {
    LOOPER_IDLE, /* empty, or a loop that is stopped */
    LOOPER_ARMED,
    LOOPER_RECORDING,
    LOOPER_PLAYING,
} looper_state_t;

typedef struct {
    uint32_t t_us; /* since the loop's start */
    uint8_t msg[3];
} looper_event_t;

typedef struct {
    looper_state_t state;
    looper_event_t ev[LOOPER_MAX_EVENTS];
    size_t n;
    uint32_t length_us; /* 0: no loop */
    uint8_t program;    /* the keys' instrument when recording started */
    int64_t start_us;   /* recording: when the first note came; playing: this pass's start */
    size_t pos;         /* playing: next event */
    /* Bit per note, [0] keys, [1] drums. Recording: keys held; playing: notes sounding. */
    uint8_t held[2][16];
} looper_t;

void looper_init(looper_t *l);

void looper_record(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx);
void looper_replay(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx);

/* Stops whatever it does and forgets the loop (RESET). */
void looper_clear(looper_t *l, midi_send_fn send, void *ctx);

/* Every message keymap sent: notes are stored while armed or recording.
 * program: the keys' instrument right now. */
void looper_capture(looper_t *l, int64_t now_us, const uint8_t *msg, size_t len, uint8_t program);

/* Call often (every scan): sends what is due by now_us. */
void looper_tick(looper_t *l, int64_t now_us, midi_send_fn send, void *ctx);
