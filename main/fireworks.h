/*
 * Fireworks on the 32x8 dot matrix, standing up (8 across, 32 tall). Pure (no ESP-IDF),
 * host-tested; dots.c runs it and shows the picture. The user's pick, 2026-11-09, scene D
 * of docs/matrix-random.html.
 *
 * A note launches a rocket from the bottom, in its pitch's column (FW_NOTE_LOW.. in 8 bands
 * of FW_NOTE_SPAN / 8 semitones); a higher note flies higher. On the next beat (fw_beat) it
 * bursts into sparks that fall. Without a beat or the metronome a rocket bursts FW_WAIT_US
 * after it stopped climbing. Every FW_REROLL_BARS bar lines the gravity and the burst size
 * are rolled again (docs/matrix-random.html's "random settings").
 *
 * Notes are timed like the MIDI they come from: a sequenced note (beat, loop, DEMO) is sent
 * ahead and launches at its own time.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FW_W           8
#define FW_H           32
#define FW_ROCKETS     5
#define FW_SPARKS      96
#define FW_PENDING     16
#define FW_NOTE_LOW    33 /* A1, key 1 in EDM mode */
#define FW_NOTE_SPAN   56
#define FW_CLIMB       45.0f /* rows a second */
#define FW_WAIT_US     600000
#define FW_REROLL_BARS 4

typedef struct {
    float x, y, top;
    int64_t topped_us; /* when it stopped climbing; 0: still climbing */
} fw_rocket_t;

typedef struct {
    float x, y, vx, vy, life;
} fw_spark_t;

typedef struct {
    int64_t at_us;
    uint8_t note;
} fw_pending_t;

typedef struct {
    uint32_t rng;
    float gravity; /* rows/s^2 */
    int burst;     /* sparks a rocket bursts into */
    fw_rocket_t rocket[FW_ROCKETS];
    int n_rockets;
    fw_spark_t spark[FW_SPARKS];
    int n_sparks;
    fw_pending_t pending[FW_PENDING];
    int n_pending;
    int bars;
    int64_t last_us;
} fw_t;

void fw_init(fw_t *f, uint32_t seed);

/* New gravity and burst size. */
void fw_reroll(fw_t *f);

/* A note on (MIDI note number), sounding at at_us. */
void fw_note(fw_t *f, uint8_t note, int64_t at_us);

/* A beat: the rockets up burst. bar_line: the first beat of a bar (counts for the reroll). */
void fw_beat(fw_t *f, bool bar_line, int64_t now_us);

/* Moves everything to now_us. */
void fw_tick(fw_t *f, int64_t now_us);

/* The picture: out[y] is row y (0 = top), bit x (0 = left). */
void fw_draw(const fw_t *f, uint8_t out[FW_H]);

/* The column a note's rocket starts in. */
int fw_column(uint8_t note);
