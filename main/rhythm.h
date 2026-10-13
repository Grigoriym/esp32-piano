#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "midi_out.h"

/* SELECT button: a drum beat for each style button, on the drum channel.
 * SELECT starts the beat of the last style picked, or stops it; a style
 * button while it plays switches the beat at the next bar line, so it stays
 * in time. Pure: the caller passes the time.
 *
 * A beat is one bar of steps, a lane per drum: 'x' a hit, 'X' an accented
 * hit, '.' nothing. Each hit stops when the next step starts. */
#define RHYTHM_STYLES   8
#define RHYTHM_LANES    4
#define RHYTHM_VELOCITY 70 /* under the keys and drum pads (100), so they are heard over it */
#define RHYTHM_ACCENT   90

typedef struct {
    const char *name;
    uint8_t bpm;
    uint8_t steps_per_beat;
    uint8_t steps; /* one bar */
    struct {
        uint8_t note; /* General MIDI drum note, 0: lane unused */
        const char *hits;
    } lane[RHYTHM_LANES];
} rhythm_style_t;

typedef struct {
    bool on;
    size_t style;      /* the beat playing, RHYTHM_STYLES order (style buttons EF0-EF7) */
    size_t next_style; /* takes over at the next bar line */
    size_t step;       /* next step, 0..steps-1 */
    int64_t next_us;   /* when it starts */
    uint8_t sounding;  /* bit per lane: hit not stopped yet */
} rhythm_t;

const rhythm_style_t *rhythm_style(size_t style);

/* One step's length in microseconds. */
int64_t rhythm_step_us(const rhythm_style_t *s);

/* Starts style's beat, or stops the beat when it's playing. */
void rhythm_toggle(rhythm_t *r, size_t style, int64_t now_us, midi_send_fn send, void *ctx);

/* A style button: the beat switches at the next bar line (if it plays). */
void rhythm_select(rhythm_t *r, size_t style);

/* Call often (every scan): sends what is due by now_us. */
void rhythm_tick(rhythm_t *r, int64_t now_us, midi_send_fn send, void *ctx);
