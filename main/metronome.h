/*
 * The metronome (FROG toggles it, 2026-11-06, the user's ask). Pure (no
 * ESP-IDF), host-tested.
 *
 * A woodblock on its own channel (METRO_CH), so the looper never records it and
 * the beat pot doesn't touch it; dry: no reverb, no filter (the user). The first
 * beat of a bar clicks higher. Pot 6 sets its tempo, METRO_MIN_BPM to
 * METRO_MAX_BPM, pot 2 its volume (the user, 2026-11-06). While the
 * beat plays it follows the beat's grid instead, which main.c gives it every scan. Sent MIDI_LINK_LEAD_US ahead like the beat: ctx points to
 * due_us, set before each message.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "midi_out.h"

#define METRO_CH       8   /* MIDI channel 9: free (keys 1, DEMO 2, loop 3-6, drums 10) */
#define METRO_PROGRAM  115 /* General MIDI woodblock */
#define METRO_BAR_NOTE 84  /* beat 1 */
#define METRO_NOTE     77
#define METRO_BAR_VEL  100
#define METRO_VEL      80
#define METRO_CLICK_US 50000 /* note off this long after the click */
#define METRO_MIN_BPM  60
#define METRO_MAX_BPM  180
#define METRO_BEATS    4 /* a bar, on its own grid */

typedef struct {
    bool on;
    int64_t origin_us; /* its own grid's start (no beat) */
    int64_t last_us;   /* the last click sent (it sounds then: sent ahead) */
    bool last_bar;     /* it was a beat 1 */
    int64_t prev_us;   /* the one before */
    bool prev_bar;
    int64_t due_us; /* when the message being sent should sound */
    int bpm;        /* its own tempo (pot 6); 0: not read yet, 120 */
} metro_t;

/* Turns it on (its own grid starts at at_us) or off. */
void metro_toggle(metro_t *m, int64_t at_us, midi_send_fn send, void *ctx);

/* Its channel set up: the woodblock, dry. Sent on, and again
 * when the PC link comes up. */
void metro_setup(midi_send_fn send, void *ctx);

/* A pot level (0-127) as a tempo. */
int metro_bpm_for(int level);

/* Pot 6 moved: a running metronome carries on from its last click at the new tempo. */
void metro_set_bpm(metro_t *m, int bpm);

/* One beat on its own grid. */
int64_t metro_beat_us(const metro_t *m);

#define METRO_FLASH_US 120000 /* the screen's dot lights this long a click */

/* For the screen at now_us: 0 no click sounding, 1 a click, 2 a beat 1. */
int metro_flash(const metro_t *m, int64_t now_us);

/* Sends the clicks due by until_us on the grid: origin_us, beat_us apart,
 * beats_per_bar to a bar. ctx must point to m->due_us. */
void metro_tick(metro_t *m, int64_t until_us, int64_t origin_us, int64_t beat_us, int beats_per_bar,
                midi_send_fn send, void *ctx);
