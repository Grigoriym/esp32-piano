#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "midi_out.h"

/* DEMO button: the Megalovania intro (Toby Fox, Undertale), four bars played
 * twice on its own channel. Pure: the caller passes the time. */
#define DEMO_CH       1
#define DEMO_PROGRAM  80     /* square lead */
#define DEMO_STEP_US  150000 /* one 16th at 100 bpm */
#define DEMO_VELOCITY 70     /* under the keys (100), so playing along is heard */
#define DEMO_GAP_US   20000  /* each note stops this early: repeated notes sound as two */

typedef struct {
    bool on;
    size_t pos;       /* next note, 0..demo_length() */
    int64_t next_us;  /* when it starts */
    uint8_t sounding; /* note now on, 0: none */
} demo_t;

/* Number of notes in the whole piece. */
size_t demo_length(void);

/* Note pos of the piece and its length in 16ths. */
uint8_t demo_note(size_t pos, uint8_t *steps);

/* Starts the piece, or stops it when it's playing. */
void demo_toggle(demo_t *d, int64_t now_us, midi_send_fn send, void *ctx);

/* Call often (every scan): sends what is due by now_us. */
void demo_tick(demo_t *d, int64_t now_us, midi_send_fn send, void *ctx);
