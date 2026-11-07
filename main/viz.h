/*
 * What the 8x8 LED matrix shows: falling notes (the user's pick "A" of
 * docs/matrix-ideas.html, 2026-11-07). Pure (no ESP-IDF), host-tested; main.c
 * feeds it the notes it sends and ledmx.c shows the frame.
 *
 * Each note drops a dot from the top row, low notes left, high notes right
 * (VIZ_NOTE_LOW.. in 8 bands of VIZ_NOTE_SPAN / 8 semitones, the ends clamped:
 * in EDM mode about 4 keys a column). A held note leaves a streak as long as it
 * is held. Rows move down one every VIZ_ROW_US. Notes on the keys, DEMO and
 * loop channels (VIZ_CHANNELS); not the drums, the metronome or the chicks' copies.
 * Sequenced notes (loop, DEMO) are sent ahead of time: they drop at their own time.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VIZ_ROW_US    100000
#define VIZ_NOTE_LOW  33 /* A1, key 1 in EDM mode */
#define VIZ_NOTE_SPAN 56 /* up to E5/F5: key 32 in EDM mode lands in the last column */
#define VIZ_CHANNELS  6  /* MIDI channels 1-6: keys, DEMO, the four loop layers */
#define VIZ_QUEUE     64

typedef struct {
    int64_t at_us;
    uint8_t ch, note;
    bool on;
} viz_event_t;

typedef struct {
    uint8_t cols[8]; /* the frame: bit r of cols[c] = row r (0 = top), column c (0 = left) */
    uint8_t sounding[VIZ_CHANNELS][16]; /* notes on now, a bit each */
    uint8_t hit; /* columns with a note on since the last row: a short tap still drops */
    int64_t next_row_us;
    viz_event_t queue[VIZ_QUEUE]; /* notes waiting for their time */
    size_t queued;
} viz_t;

void viz_init(viz_t *v);

/* A MIDI message, sounding at at_us (now for live keys). Only note on/off and
 * all-notes-off (CC120/123) matter. */
void viz_midi(viz_t *v, const uint8_t *msg, size_t len, int64_t at_us, int64_t now_us);

/* Applies the notes now due and moves the rows. True if the frame changed. */
bool viz_update(viz_t *v, int64_t now_us);

/* The column of a note, 0-7. */
int viz_column(uint8_t note);
