/*
 * The metronome (FROG toggles it, 2026-11-06, the user's ask). Pure (no
 * ESP-IDF), host-tested.
 *
 * A woodblock on its own channel (METRO_CH), so the looper never records it and
 * the beat pot doesn't touch it; pot 6 is its fader (CC11, main/mixer.h). The
 * first beat of a bar clicks higher. It follows the grid main.c gives it every
 * scan: the beat's while the beat plays, else its own from the press at the
 * style's tempo. Sent MIDI_LINK_LEAD_US ahead like the beat: ctx points to
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

typedef struct {
    bool on;
    int64_t origin_us; /* its own grid's start (no beat) */
    int64_t last_us;   /* the last click sent */
    int64_t due_us;    /* when the message being sent should sound */
} metro_t;

/* Turns it on (its own grid starts at at_us) or off. */
void metro_toggle(metro_t *m, int64_t at_us, midi_send_fn send, void *ctx);

/* Sends the clicks due by until_us on the grid: origin_us, beat_us apart,
 * beats_per_bar to a bar. ctx must point to m->due_us. */
void metro_tick(metro_t *m, int64_t until_us, int64_t origin_us, int64_t beat_us, int beats_per_bar,
                midi_send_fn send, void *ctx);
